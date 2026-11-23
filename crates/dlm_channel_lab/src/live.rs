use crate::{
    codec::{Event, Trace},
    config::Config,
    require, Cancellation, Error, Result, MAX_EVENTS,
};
use std::{
    io::{Read, Write},
    net::{TcpListener, TcpStream, UdpSocket},
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
    thread,
    time::{Duration, Instant},
};
pub struct Observation {
    pub trace: Trace,
    pub elapsed_us: u64,
    pub udp_events: u64,
    pub tcp_events: u64,
    pub body_bytes: u64,
}
fn wait(target: Instant, end: Instant, cancel: &Cancellation) -> Result<()> {
    loop {
        cancel.check()?;
        let now = Instant::now();
        require(now < end, 3, "loopback deadline expired")?;
        if now >= target {
            return Ok(());
        }
        let left = target - now;
        if left > Duration::from_micros(200) {
            thread::sleep((left - Duration::from_micros(100)).min(Duration::from_millis(1)));
        } else {
            thread::yield_now();
        }
    }
}
pub fn run(planned: &Trace, config: &Config, cancel: &Cancellation) -> Result<Observation> {
    planned.validate()?;
    config.validate()?;
    require(
        planned
            .events
            .iter()
            .all(|e| e.size <= 1200 && e.time_us < planned.plan.duration_us()),
        4,
        "physical emission event exceeds size or schedule bounds",
    )?;
    let mut noise = config.noise.clone();
    noise.seed = 1;
    require(
        noise == crate::model::Noise::default(),
        4,
        "physical loopback requires identity noise model; distort the captured trace separately",
    )?;
    require(
        planned.plan.duration_us() + 100_000 <= config.live_max_ms * 1000,
        7,
        "physical experiment exceeds configured short loopback deadline",
    )?;
    let udp = UdpSocket::bind((std::net::Ipv4Addr::LOCALHOST, 0))?;
    let address = udp.local_addr()?;
    udp.set_nonblocking(true)?;
    let listener = TcpListener::bind(address)?;
    let mut tcp_tx = TcpStream::connect(address)?;
    tcp_tx.set_nodelay(true)?;
    tcp_tx.set_nonblocking(true)?;
    let (mut tcp_rx, _) = listener.accept()?;
    tcp_rx.set_nonblocking(true)?;
    drop(listener);
    let udp_tx = UdpSocket::bind((std::net::Ipv4Addr::LOCALHOST, 0))?;
    udp_tx.connect(address)?;
    udp_tx.set_nonblocking(true)?;
    let source = udp_tx.local_addr()?;
    let epoch = Instant::now() + Duration::from_millis(100);
    let end = epoch + Duration::from_micros(planned.plan.duration_us() + 50_000);
    let stop = Arc::new(AtomicBool::new(false));
    let stopped = stop.clone();
    let token = cancel.clone();
    let plan = planned.plan.clone();
    let worker = thread::spawn(move || -> Result<Observation> {
        let mut events = Vec::with_capacity(plan.symbols() * plan.codec.mode.max_events());
        let mut pending = Vec::with_capacity(4096);
        let mut data = [0u8; 4096];
        let mut tcp_closed = false;
        let mut udp_events = 0;
        let mut tcp_events = 0;
        let mut bytes = 0;
        while Instant::now() < end && !stopped.load(Ordering::Acquire) {
            token.check()?;
            let mut progress = false;
            for _ in 0..128 {
                match udp.recv_from(&mut data) {
                    Ok((n, peer)) => {
                        if peer != source {
                            continue;
                        }
                        require(
                            n > 0 && n <= 1200 && data[..n].iter().all(|&b| b == 0xa5),
                            11,
                            "unexpected loopback UDP event payload",
                        )?;
                        if Instant::now() >= epoch {
                            require(events.len() < MAX_EVENTS, 7, "loopback event quota")?;
                            events.push(Event {
                                time_us: epoch.elapsed().as_micros() as u64,
                                size: n as u32,
                                kind: 0,
                            });
                            udp_events += 1;
                            bytes += n as u64;
                        }
                        progress = true;
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                    Err(e) => return Err(e.into()),
                }
            }
            if !tcp_closed {
                for _ in 0..128 {
                    match tcp_rx.read(&mut data) {
                        Ok(0) => {
                            tcp_closed = true;
                            break;
                        }
                        Ok(n) => {
                            require(
                                pending.len() + n <= 8192,
                                7,
                                "loopback TCP partial-frame quota",
                            )?;
                            pending.extend_from_slice(&data[..n]);
                            let mut offset = 0;
                            while pending.len() - offset >= 2 {
                                let size = u16::from_be_bytes(
                                    pending[offset..offset + 2].try_into().unwrap(),
                                ) as usize;
                                require(
                                    size > 0 && size <= 1200,
                                    11,
                                    "loopback TCP event size invalid",
                                )?;
                                if pending.len() - offset < 2 + size {
                                    break;
                                }
                                require(
                                    pending[offset + 2..offset + 2 + size]
                                        .iter()
                                        .all(|&b| b == 0xa5),
                                    11,
                                    "unexpected loopback TCP event payload",
                                )?;
                                if Instant::now() >= epoch {
                                    require(events.len() < MAX_EVENTS, 7, "loopback event quota")?;
                                    events.push(Event {
                                        time_us: epoch.elapsed().as_micros() as u64,
                                        size: size as u32,
                                        kind: 1,
                                    });
                                    tcp_events += 1;
                                    bytes += size as u64;
                                }
                                offset += 2 + size;
                            }
                            if offset > 0 {
                                pending.drain(..offset);
                            }
                            progress = true;
                        }
                        Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                        Err(e) => return Err(e.into()),
                    }
                }
            }
            if !progress {
                thread::sleep(Duration::from_micros(100));
            }
        }
        require(pending.is_empty(), 11, "truncated physical TCP event")?;
        events.sort_by_key(|e| e.time_us);
        let trace = Trace { plan, events };
        trace.validate()?;
        Ok(Observation {
            trace,
            elapsed_us: epoch.elapsed().as_micros() as u64,
            udp_events,
            tcp_events,
            body_bytes: bytes,
        })
    });
    let sent = (|| -> Result<()> {
        let mut packet = vec![0xa5; 1202];
        for event in &planned.events {
            wait(epoch + Duration::from_micros(event.time_us), end, cancel)?;
            let n = event.size as usize;
            if event.kind == 0 {
                loop {
                    cancel.check()?;
                    match udp_tx.send(&packet[2..n + 2]) {
                        Ok(count) => {
                            require(count == n, 17, "partial UDP datagram send")?;
                            break;
                        }
                        Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                            wait(Instant::now() + Duration::from_micros(100), end, cancel)?
                        }
                        Err(e) => return Err(e.into()),
                    }
                }
            } else {
                packet[..2].copy_from_slice(&(n as u16).to_be_bytes());
                let mut offset = 0;
                while offset < n + 2 {
                    cancel.check()?;
                    match tcp_tx.write(&packet[offset..n + 2]) {
                        Ok(0) => return Err(Error::new(17, "loopback TCP stream closed")),
                        Ok(count) => offset += count,
                        Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                            wait(Instant::now() + Duration::from_micros(100), end, cancel)?
                        }
                        Err(e) => return Err(e.into()),
                    }
                }
                packet[..2].fill(0xa5);
            }
        }
        Ok(())
    })();
    drop(tcp_tx);
    drop(udp_tx);
    if sent.is_err() {
        stop.store(true, Ordering::Release);
    }
    let received = worker
        .join()
        .map_err(|_| Error::new(99, "loopback receiver panicked"))?;
    sent?;
    received
}

//! v0.126: processes, environment and TCP for the interpreter. The native
//! runtime (`dvo_*`, `dvn_net_*` in `backend_c_runtime.c`) keeps the same state
//! (connection table ids from 1, a per-connection line buffer) and produces
//! the same texts: `cannot <what> (os error N)` with the OS `errno`.
use std::collections::BTreeMap;
use std::io::{Read, Write};
use std::net::{Shutdown, TcpListener, TcpStream, ToSocketAddrs};
use std::time::Duration;

/// Largest text a single receive returns (bytes).
pub const NET_MAX: usize = 1 << 26;

fn os(what: String, e: &std::io::Error) -> String {
    format!("{what} (os error {})", e.raw_os_error().unwrap_or(0))
}

/// `[exit code, stdout, stderr]`; exit code `128 + signal` for a signal.
pub fn proc_run(program: &str, args: &[String], input: &str) -> Result<Vec<String>, String> {
    use std::process::{Command, Stdio};
    let mut child = Command::new(program)
        .args(args)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| os(format!("cannot run `{program}`"), &e))?;
    let mut stdin = child.stdin.take().expect("piped stdin");
    let data = input.as_bytes().to_vec();
    let writer = std::thread::spawn(move || {
        let _ = stdin.write_all(&data);
    });
    let out = child.wait_with_output().map_err(|e| os(format!("cannot run `{program}`"), &e))?;
    let _ = writer.join();
    let code = match out.status.code() {
        Some(c) => c as i64,
        None => {
            #[cfg(unix)]
            {
                use std::os::unix::process::ExitStatusExt;
                128 + i64::from(out.status.signal().unwrap_or(0))
            }
            #[cfg(not(unix))]
            {
                1
            }
        }
    };
    let text = |b: Vec<u8>| String::from_utf8(b).map_err(|_| format!("output of `{program}` is not valid UTF-8"));
    Ok(vec![(code.max(0) as u64).to_string(), text(out.stdout)?, text(out.stderr)?])
}

/// Addresses of `host`, deduplicated and sorted as text.
pub fn resolve(host: &str) -> Result<Vec<String>, String> {
    let addrs = (host, 0u16).to_socket_addrs().map_err(|_| format!("cannot resolve `{host}`"))?;
    let mut out: Vec<String> = addrs.map(|a| a.ip().to_string()).collect();
    out.sort();
    out.dedup();
    if out.is_empty() {
        return Err(format!("cannot resolve `{host}`"));
    }
    Ok(out)
}

enum Slot {
    Listener(TcpListener),
    Conn { stream: TcpStream, pending: Vec<u8>, eof: bool },
}

#[derive(Default)]
pub struct NetTable {
    slots: BTreeMap<u128, Slot>,
    next: u128,
}

fn timeout(ms: u128) -> Option<Duration> {
    (ms > 0).then(|| Duration::from_millis(ms.min(i32::MAX as u128) as u64))
}

impl NetTable {
    fn add(&mut self, slot: Slot) -> u128 {
        self.next += 1;
        self.slots.insert(self.next, slot);
        self.next
    }

    pub fn listen(&mut self, host: &str, port: u128) -> Result<u128, String> {
        let port = u16::try_from(port).map_err(|_| format!("invalid port {port}"))?;
        let l = TcpListener::bind((host, port)).map_err(|e| os(format!("cannot listen on `{host}:{port}`"), &e))?;
        Ok(self.add(Slot::Listener(l)))
    }

    pub fn local_port(&self, id: u128) -> Result<u128, String> {
        let addr = match self.slots.get(&id) {
            Some(Slot::Listener(l)) => l.local_addr(),
            Some(Slot::Conn { stream, .. }) => stream.local_addr(),
            None => return Err(format!("no socket {id}")),
        };
        addr.map(|a| u128::from(a.port())).map_err(|e| os(format!("cannot query socket {id}"), &e))
    }

    pub fn accept(&mut self, id: u128, ms: u128) -> Result<u128, String> {
        let Some(Slot::Listener(l)) = self.slots.get(&id) else { return Err(format!("no listener {id}")) };
        if let Some(t) = timeout(ms) {
            l.set_nonblocking(true).map_err(|e| os(format!("cannot accept on socket {id}"), &e))?;
            let start = std::time::Instant::now();
            let accepted = loop {
                match l.accept() {
                    Ok((s, _)) => break Ok(s),
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                        if start.elapsed() >= t {
                            break Err(format!("accept on socket {id} timed out"));
                        }
                        std::thread::sleep(Duration::from_millis(1));
                    }
                    Err(e) => break Err(os(format!("cannot accept on socket {id}"), &e)),
                }
            };
            let _ = l.set_nonblocking(false);
            let stream = accepted?;
            let _ = stream.set_nonblocking(false);
            let _ = stream.set_read_timeout(Some(t));
            let _ = stream.set_write_timeout(Some(t));
            return Ok(self.add(Slot::Conn { stream, pending: Vec::new(), eof: false }));
        }
        let (stream, _) = l.accept().map_err(|e| os(format!("cannot accept on socket {id}"), &e))?;
        Ok(self.add(Slot::Conn { stream, pending: Vec::new(), eof: false }))
    }

    pub fn connect(&mut self, host: &str, port: u128, ms: u128) -> Result<u128, String> {
        let port16 = u16::try_from(port).map_err(|_| format!("invalid port {port}"))?;
        let addrs: Vec<_> = (host, port16).to_socket_addrs().map_err(|_| format!("cannot resolve `{host}`"))?.collect();
        let mut last = None;
        for a in addrs {
            let r = match timeout(ms) {
                Some(t) => TcpStream::connect_timeout(&a, t),
                None => TcpStream::connect(a),
            };
            match r {
                Ok(stream) => {
                    let _ = stream.set_read_timeout(timeout(ms));
                    let _ = stream.set_write_timeout(timeout(ms));
                    return Ok(self.add(Slot::Conn { stream, pending: Vec::new(), eof: false }));
                }
                Err(e) => last = Some(e),
            }
        }
        Err(match last {
            Some(e) => os(format!("cannot connect to `{host}:{port}`"), &e),
            None => format!("cannot resolve `{host}`"),
        })
    }

    fn conn(&mut self, id: u128) -> Result<(&mut TcpStream, &mut Vec<u8>, &mut bool), String> {
        match self.slots.get_mut(&id) {
            Some(Slot::Conn { stream, pending, eof }) => Ok((stream, pending, eof)),
            _ => Err(format!("no connection {id}")),
        }
    }

    pub fn send(&mut self, id: u128, text: &str) -> Result<u128, String> {
        let (stream, _, _) = self.conn(id)?;
        stream.write_all(text.as_bytes()).map_err(|e| os(format!("cannot send on connection {id}"), &e))?;
        Ok(text.len() as u128)
    }

    fn fill(id: u128, stream: &mut TcpStream, pending: &mut Vec<u8>, eof: &mut bool) -> Result<bool, String> {
        if pending.len() > NET_MAX { return Err(format!("connection {id} sent more than {NET_MAX} bytes")); }
        if *eof {
            return Ok(false);
        }
        let mut chunk = [0u8; 65536];
        match stream.read(&mut chunk) {
            Ok(0) => {
                *eof = true;
                Ok(false)
            }
            Ok(n) => {
                pending.extend_from_slice(&chunk[..n]);
                if pending.len() > NET_MAX {
                    return Err(format!("connection {id} sent more than {NET_MAX} bytes"));
                }
                Ok(true)
            }
            Err(e) if matches!(e.kind(), std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut) => Err(format!("receive on connection {id} timed out")),
            Err(e) => Err(os(format!("cannot receive on connection {id}"), &e)),
        }
    }

    fn text(id: u128, bytes: Vec<u8>) -> Result<String, String> {
        String::from_utf8(bytes).map_err(|_| format!("connection {id} received invalid UTF-8"))
    }

    /// The next line without `\n` (`\r\n` accepted); `none` at the end.
    pub fn recv_line(&mut self, id: u128) -> Result<Option<String>, String> {
        let (stream, pending, eof) = self.conn(id)?;
        if pending.len() > NET_MAX { return Err(format!("connection {id} sent more than {NET_MAX} bytes")); }
        loop {
            if let Some(i) = pending.iter().position(|&c| c == b'\n') {
                let mut line: Vec<u8> = pending.drain(..=i).collect();
                line.pop();
                if line.last() == Some(&b'\r') {
                    line.pop();
                }
                return Self::text(id, line).map(Some);
            }
            if !Self::fill(id, stream, pending, eof)? {
                if pending.is_empty() {
                    return Ok(None);
                }
                return Self::text(id, std::mem::take(pending)).map(Some);
            }
        }
    }

    /// Everything until the peer closes its side.
    pub fn recv_all(&mut self, id: u128) -> Result<String, String> {
        let (stream, pending, eof) = self.conn(id)?;
        while Self::fill(id, stream, pending, eof)? {}
        Self::text(id, std::mem::take(pending))
    }

    pub fn shutdown(&mut self, id: u128) -> Result<u128, String> {
        let (stream, _, _) = self.conn(id)?;
        stream.shutdown(Shutdown::Write).map_err(|e| os(format!("cannot shut down connection {id}"), &e))?;
        Ok(0)
    }

    pub fn close(&mut self, id: u128) -> Result<u128, String> {
        self.slots.remove(&id).map(|_| 0).ok_or_else(|| format!("no socket {id}"))
    }
}

/// v0.126: background child processes; stdin, stdout and stderr are temporary
/// files, so a child never blocks on a full pipe while the program runs.
struct ProcSlot {
    child: std::process::Child,
    files: [std::path::PathBuf; 3],
    code: Option<u128>,
}

#[derive(Default)]
pub struct ProcTable {
    slots: BTreeMap<u128, ProcSlot>,
    next: u128,
    temps: u64,
}

fn exit_code(status: std::process::ExitStatus) -> u128 {
    match status.code() {
        Some(c) => c.max(0) as u128,
        None => {
            #[cfg(unix)]
            {
                use std::os::unix::process::ExitStatusExt;
                128 + status.signal().unwrap_or(0).max(0) as u128
            }
            #[cfg(not(unix))]
            {
                1
            }
        }
    }
}

impl ProcTable {
    fn temp(&mut self, tag: &str) -> std::io::Result<(std::path::PathBuf, std::fs::File)> {
        let dir = std::env::var_os("TMPDIR").filter(|d| !d.is_empty()).map(std::path::PathBuf::from).unwrap_or_else(std::env::temp_dir);
        for _ in 0..128 {
            self.temps = self.temps.wrapping_add(1);
            let path = dir.join(format!("dlm-{}-{}-{tag}", std::process::id(), self.temps));
            let mut options = std::fs::OpenOptions::new();
            options.read(true).write(true).create_new(true);
            #[cfg(unix)] {
                use std::os::unix::fs::OpenOptionsExt;
                options.mode(0o600);
            }
            match options.open(&path) {
                Ok(file) => return Ok((path, file)),
                Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => continue,
                Err(e) => return Err(e),
            }
        }
        Err(std::io::Error::from(std::io::ErrorKind::AlreadyExists))
    }

    pub fn spawn(&mut self, program: &str, args: &[String], input: &str) -> Result<u128, String> {
        use std::process::{Command, Stdio};
        let mut files = [std::path::PathBuf::new(), std::path::PathBuf::new(), std::path::PathBuf::new()];
        let fail = |e: &std::io::Error, files: &[std::path::PathBuf; 3]| {
            files.iter().for_each(|f| {
                let _ = std::fs::remove_file(f);
            });
            os(format!("cannot run `{program}`"), e)
        };
        let opened = (|| -> std::io::Result<(std::fs::File, std::fs::File, std::fs::File)> {
            use std::io::{Seek, SeekFrom};
            let (path, mut i) = self.temp("in")?;
            files[0] = path;
            let (path, o) = self.temp("out")?;
            files[1] = path;
            let (path, e) = self.temp("err")?;
            files[2] = path;
            i.write_all(input.as_bytes())?;
            i.seek(SeekFrom::Start(0))?;
            Ok((i, o, e))
        })();
        let (i, o, e) = opened.map_err(|e| fail(&e, &files))?;
        let child = Command::new(program).args(args).stdin(Stdio::from(i)).stdout(Stdio::from(o)).stderr(Stdio::from(e)).spawn().map_err(|e| fail(&e, &files))?;
        self.next += 1;
        self.slots.insert(self.next, ProcSlot { child, files, code: None });
        Ok(self.next)
    }

    /// `none` while the child runs, its exit code once it has exited.
    pub fn poll(&mut self, id: u128) -> Result<Option<u128>, String> {
        let slot = self.slots.get_mut(&id).ok_or_else(|| format!("no process {id}"))?;
        if slot.code.is_none() {
            if let Some(status) = slot.child.try_wait().map_err(|e| os(format!("cannot wait for process {id}"), &e))? {
                slot.code = Some(exit_code(status));
            }
        }
        Ok(slot.code)
    }

    /// Waits for the child: `[exit code, stdout, stderr]`; the id is released.
    pub fn wait(&mut self, id: u128) -> Result<Vec<String>, String> {
        let mut slot = self.slots.remove(&id).ok_or_else(|| format!("no process {id}"))?;
        let code = match slot.code {
            Some(c) => c,
            None => exit_code(slot.child.wait().map_err(|e| os(format!("cannot wait for process {id}"), &e))?),
        };
        slot.code = Some(code);
        let out = std::fs::read(&slot.files[1]).map_err(|e| os(format!("cannot read output of process {id}"), &e))?;
        let err = std::fs::read(&slot.files[2]).map_err(|e| os(format!("cannot read output of process {id}"), &e))?;
        let text = |b: Vec<u8>| String::from_utf8(b).map_err(|_| format!("output of `process {id}` is not valid UTF-8"));
        Ok(vec![code.to_string(), text(out)?, text(err)?])
    }

    pub fn kill(&mut self, id: u128) -> Result<u128, String> {
        let slot = self.slots.get_mut(&id).ok_or_else(|| format!("no process {id}"))?;
        if slot.code.is_none() {
            slot.child.kill().map_err(|e| os(format!("cannot kill process {id}"), &e))?;
        }
        Ok(0)
    }
}

impl Drop for ProcSlot {
    fn drop(&mut self) {
        if self.code.is_none() {
            let _ = self.child.kill();
            let _ = self.child.wait();
        }
        self.files.iter().for_each(|f| { let _ = std::fs::remove_file(f); });
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;

    #[test]
    fn missing_process_output_is_reported_and_all_temporary_files_are_removed() {
        let mut table = ProcTable::default();
        let id = table.spawn("sh", &["-c".into(), "printf data".into()], "").unwrap();
        let paths = table.slots[&id].files.clone();
        std::fs::remove_file(&paths[1]).unwrap();
        assert_eq!(table.wait(id), Err(format!("cannot read output of process {id} (os error 2)")));
        assert_eq!(table.poll(id), Err(format!("no process {id}")));
        assert!(paths.iter().all(|p| !p.exists()));
    }
}

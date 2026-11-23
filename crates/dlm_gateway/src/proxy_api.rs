use super::*;
use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};
const PROXY: u64 = 0x4450524f58593031;
const PROXY_SERVER: u64 = 0x4450525853525631;
const CANCEL: u64 = 0x44505258434e4331;
struct Proxy {
    magic: u64,
    tunnel: crate::proxy::Tunnel,
    pending: Option<Vec<u8>>,
}
struct ProxyServer {
    magic: u64,
    server: crate::proxy::Server,
}
struct Cancel {
    magic: u64,
    flag: Arc<AtomicBool>,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct IpAssignment {
    family: u32,
    prefix: u32,
    address: [u8; 16],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct IpRoute {
    family: u32,
    protocol: u32,
    first: [u8; 16],
    last: [u8; 16],
}
unsafe fn proxy<'a>(p: *mut c_void) -> Result<&'a mut Proxy> {
    ensure(!p.is_null(), 4, "null proxy handle")?;
    let v = &mut *p.cast::<Proxy>();
    ensure(v.magic == PROXY, 4, "invalid proxy handle")?;
    Ok(v)
}
unsafe fn proxy_server<'a>(p: *mut c_void) -> Result<&'a ProxyServer> {
    ensure(!p.is_null(), 4, "null proxy server handle")?;
    let v = &*p.cast::<ProxyServer>();
    ensure(v.magic == PROXY_SERVER, 4, "invalid proxy server handle")?;
    Ok(v)
}
unsafe fn retained(value: &mut Proxy, out: *mut u8, capacity: usize) -> Result<Transfer> {
    output_valid(out, capacity)?;
    let data = value
        .pending
        .as_ref()
        .ok_or(Error::new(1, "no retained proxy payload"))?;
    let mut r = Transfer {
        required: data.len(),
        value: data.len() as u64,
        outcome: 3,
        ..Default::default()
    };
    if capacity < data.len() {
        r.status = 10;
        return Ok(r);
    }
    if !data.is_empty() {
        ptr::copy_nonoverlapping(data.as_ptr(), out, data.len());
    }
    r.copied = data.len();
    value.pending = None;
    Ok(r)
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_open(
    config: *const u8,
    count: usize,
    mode: u32,
    target: *const u8,
    length: usize,
) -> Opened {
    opened(guarded(|| {
        let config = config::proxy_client(text(config, count, 65536)?, Path::new("."))?;
        let mode = crate::proxy::Mode::parse(mode)?;
        let tunnel =
            crate::proxy::Tunnel::connect(&config, mode, text(target, length, 1024)?, None)?;
        Ok(Box::into_raw(Box::new(Proxy {
            magic: PROXY,
            tunnel,
            pending: None,
        }))
        .cast())
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_close(p: *mut c_void) {
    let _ = guarded(|| {
        proxy(p)?;
        (*p.cast::<Proxy>()).magic = 0;
        drop(Box::from_raw(p.cast::<Proxy>()));
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_write(
    p: *mut c_void,
    bytes: *const u8,
    count: usize,
) -> Io {
    io(guarded(|| {
        let data = input(bytes, count, 65535)?;
        let value = proxy(p)?;
        value.tunnel.send(data)?;
        Ok(count)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_read(
    p: *mut c_void,
    maximum: usize,
    out: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| {
        output_valid(out, capacity)?;
        ensure(
            maximum > 0 && maximum <= 65535,
            4,
            "proxy receive maximum invalid",
        )?;
        let value = proxy(p)?;
        ensure(
            value.pending.is_none(),
            18,
            "take retained proxy payload before reading again",
        )?;
        let data = value.tunnel.receive(maximum)?;
        value.pending = Some(data);
        retained(value, out, capacity)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_take(
    p: *mut c_void,
    out: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| retained(proxy(p)?, out, capacity)))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_cancel_open(p: *mut c_void) -> Opened {
    opened(guarded(|| {
        let flag = proxy(p)?.tunnel.cancellation();
        Ok(Box::into_raw(Box::new(Cancel {
            magic: CANCEL,
            flag,
        }))
        .cast())
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_cancel_request(p: *mut c_void) -> Io {
    io(guarded(|| {
        ensure(!p.is_null(), 4, "null proxy cancellation token")?;
        let token = &*p.cast::<Cancel>();
        ensure(token.magic == CANCEL, 4, "invalid proxy cancellation token")?;
        token.flag.store(true, Ordering::Release);
        Ok(0)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_cancel_close(p: *mut c_void) {
    let _ = guarded(|| {
        ensure(!p.is_null(), 4, "null proxy cancellation token")?;
        let token = &mut *p.cast::<Cancel>();
        ensure(token.magic == CANCEL, 4, "invalid proxy cancellation token")?;
        token.magic = 0;
        drop(Box::from_raw(p.cast::<Cancel>()));
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_server_open(config: *const u8, count: usize) -> Opened {
    opened(guarded(|| {
        let c = config::proxy_server(text(config, count, 65536)?, Path::new("."))?;
        let server = crate::proxy::Server::start(c)?;
        Ok(Box::into_raw(Box::new(ProxyServer {
            magic: PROXY_SERVER,
            server,
        }))
        .cast())
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_server_close(p: *mut c_void) {
    let _ = guarded(|| {
        proxy_server(p)?;
        (*p.cast::<ProxyServer>()).magic = 0;
        drop(Box::from_raw(p.cast::<ProxyServer>()));
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_server_stop(p: *mut c_void) -> Io {
    io(guarded(|| {
        proxy_server(p)?.server.stop();
        Ok(0)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_server_statistics(
    p: *mut c_void,
) -> crate::proxy::Statistics {
    guarded(|| Ok(proxy_server(p)?.server.statistics())).unwrap_or_default()
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_server_address(
    p: *mut c_void,
    out: *mut u8,
    capacity: usize,
) -> Io {
    io(guarded(|| {
        output_valid(out, capacity)?;
        let address = proxy_server(p)?.server.address.to_string();
        ensure(
            capacity >= address.len(),
            10,
            "proxy address output too short",
        )?;
        ptr::copy_nonoverlapping(address.as_ptr(), out, address.len());
        Ok(address.len())
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_carriers(out: *mut u8, capacity: usize) -> Transfer {
    transfer(guarded(|| {
        output_valid(out, capacity)?;
        let value = crate::proxy::capabilities();
        let mut r = Transfer {
            required: value.len(),
            value: value.len() as u64,
            outcome: 6,
            ..Default::default()
        };
        if capacity < value.len() {
            r.status = 10;
            return Ok(r);
        }
        ptr::copy_nonoverlapping(value.as_ptr(), out, value.len());
        r.copied = value.len();
        Ok(r)
    }))
}

#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_assignments(
    p: *mut c_void,
    out: *mut IpAssignment,
    capacity: usize,
) -> Io {
    let result = guarded(|| {
        ensure(
            capacity <= 16 && (capacity == 0 || !out.is_null()),
            4,
            "invalid IP assignment output",
        )?;
        let value = proxy(p)?;
        let count = value.tunnel.assignments.len();
        if capacity < count {
            return Ok(Io {
                count,
                status: 10,
                system: 0,
            });
        }
        for (i, n) in value.tunnel.assignments.iter().enumerate() {
            let mut r = IpAssignment {
                family: n.family() as u32,
                prefix: n.prefix as u32,
                ..Default::default()
            };
            match n.address {
                std::net::IpAddr::V4(a) => r.address[..4].copy_from_slice(&a.octets()),
                std::net::IpAddr::V6(a) => r.address.copy_from_slice(&a.octets()),
            }
            ptr::write(out.add(i), r);
        }
        Ok(Io {
            count,
            status: 0,
            system: 0,
        })
    });
    result.unwrap_or_else(|e| Io {
        count: 0,
        status: e.code,
        system: e.system,
    })
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_proxy_routes(
    p: *mut c_void,
    out: *mut IpRoute,
    capacity: usize,
) -> Io {
    let result = guarded(|| {
        ensure(
            capacity <= 32 && (capacity == 0 || !out.is_null()),
            4,
            "invalid IP route output",
        )?;
        let value = proxy(p)?;
        let bytes = &value.tunnel.routes;
        crate::proxy_ip::routes(bytes)?;
        let mut count = 0;
        let mut b = bytes.as_slice();
        while !b.is_empty() {
            let n = if b[0] == 4 { 4 } else { 16 };
            count += 1;
            b = &b[2 * n + 2..];
        }
        if capacity < count {
            return Ok(Io {
                count,
                status: 10,
                system: 0,
            });
        }
        b = bytes;
        let mut i = 0;
        while !b.is_empty() {
            let n = if b[0] == 4 { 4 } else { 16 };
            let mut r = IpRoute {
                family: b[0] as u32,
                protocol: b[1 + 2 * n] as u32,
                ..Default::default()
            };
            r.first[..n].copy_from_slice(&b[1..1 + n]);
            r.last[..n].copy_from_slice(&b[1 + n..1 + 2 * n]);
            ptr::write(out.add(i), r);
            b = &b[2 * n + 2..];
            i += 1;
        }
        Ok(Io {
            count,
            status: 0,
            system: 0,
        })
    });
    result.unwrap_or_else(|e| Io {
        count: 0,
        status: e.code,
        system: e.system,
    })
}

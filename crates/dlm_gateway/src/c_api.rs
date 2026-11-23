use crate::{
    config, ensure,
    gateway::{Client, GatewayGroup, Reply, Statistics},
    policy::TargetProtocol,
    Error, Result,
};
use std::{
    ffi::c_void,
    panic::{catch_unwind, AssertUnwindSafe},
    path::Path,
    ptr, slice,
};
const CLIENT: u64 = 0x44474e434c493031;
const SERVER: u64 = 0x44474e5352563031;
#[path = "proxy_api.rs"]
mod proxy_api;
#[repr(C)]
pub struct Opened {
    pub handle: *mut c_void,
    pub status: i32,
    pub system: i32,
}
#[repr(C)]
pub struct Io {
    pub count: usize,
    pub status: i32,
    pub system: i32,
}
#[repr(C)]
#[derive(Default)]
pub struct Transfer {
    pub required: usize,
    pub copied: usize,
    pub value: u64,
    pub outcome: u32,
    pub status: i32,
    pub system: i32,
}
struct ClientHandle {
    magic: u64,
    client: Client,
    pending: Option<(u32, Reply)>,
}
struct ServerHandle {
    magic: u64,
    server: GatewayGroup,
}
fn guarded<T>(body: impl FnOnce() -> Result<T>) -> Result<T> {
    catch_unwind(AssertUnwindSafe(body))
        .unwrap_or_else(|_| Err(Error::new(5, "gateway foreign call panicked")))
}
fn opened(result: Result<*mut c_void>) -> Opened {
    match result {
        Ok(handle) => Opened {
            handle,
            status: 0,
            system: 0,
        },
        Err(e) => Opened {
            handle: ptr::null_mut(),
            status: e.code,
            system: e.system,
        },
    }
}
fn io(result: Result<usize>) -> Io {
    match result {
        Ok(count) => Io {
            count,
            status: 0,
            system: 0,
        },
        Err(e) => Io {
            count: 0,
            status: e.code,
            system: e.system,
        },
    }
}
unsafe fn input<'a>(p: *const u8, n: usize, maximum: usize) -> Result<&'a [u8]> {
    ensure(
        n <= maximum && (n == 0 || !p.is_null()),
        4,
        "foreign input pointer/length invalid",
    )?;
    Ok(if n == 0 {
        &[]
    } else {
        slice::from_raw_parts(p, n)
    })
}
unsafe fn text<'a>(p: *const u8, n: usize, maximum: usize) -> Result<&'a str> {
    std::str::from_utf8(input(p, n, maximum)?)
        .map_err(|_| Error::new(4, "foreign input is not UTF-8"))
}
unsafe fn client<'a>(p: *mut c_void) -> Result<&'a mut ClientHandle> {
    ensure(!p.is_null(), 4, "null client handle")?;
    let value = &mut *p.cast::<ClientHandle>();
    ensure(value.magic == CLIENT, 4, "invalid client handle")?;
    Ok(value)
}
unsafe fn server<'a>(p: *mut c_void) -> Result<&'a ServerHandle> {
    ensure(!p.is_null(), 4, "null server handle")?;
    let value = &*p.cast::<ServerHandle>();
    ensure(value.magic == SERVER, 4, "invalid server handle")?;
    Ok(value)
}
fn transfer(result: Result<Transfer>) -> Transfer {
    match result {
        Ok(value) => value,
        Err(e) => Transfer {
            status: e.code,
            system: e.system,
            ..Default::default()
        },
    }
}
fn output_valid(output: *mut u8, capacity: usize) -> Result<()> {
    ensure(
        capacity <= 65536 && (capacity == 0 || !output.is_null()),
        4,
        "foreign output pointer/length invalid",
    )
}
unsafe fn take(value: &mut ClientHandle, output: *mut u8, capacity: usize) -> Result<Transfer> {
    ensure(
        capacity <= 65536 && (capacity == 0 || !output.is_null()),
        4,
        "foreign output pointer/length invalid",
    )?;
    let (_, reply) = value
        .pending
        .as_ref()
        .ok_or(Error::new(1, "no retained gateway response"))?;
    let mut result = Transfer {
        required: reply.data.len(),
        copied: 0,
        value: reply.value,
        outcome: reply.outcome as u32,
        status: 0,
        system: 0,
    };
    if capacity < result.required {
        result.status = 10;
        return Ok(result);
    }
    if result.required > 0 {
        ptr::copy_nonoverlapping(reply.data.as_ptr(), output, result.required);
    }
    result.copied = result.required;
    value.pending = None;
    Ok(result)
}

#[no_mangle]
pub extern "C" fn dln_gateway_abi() -> u32 {
    0x10000
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_client_open(bytes: *const u8, count: usize) -> Opened {
    opened(guarded(|| {
        let (configuration, proxy) =
            config::client_with_proxy(text(bytes, count, 65536)?, Path::new("."))?;
        let client = Client::connect_with_proxy(&configuration, None, proxy.as_ref())?;
        Ok(Box::into_raw(Box::new(ClientHandle {
            magic: CLIENT,
            client,
            pending: None,
        }))
        .cast())
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_client_close(p: *mut c_void) {
    let _ = guarded(|| {
        if p.is_null() {
            return Ok(());
        }
        client(p)?;
        let mut value = Box::from_raw(p.cast::<ClientHandle>());
        value.magic = 0;
        drop(value);
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_stream_open(
    p: *mut c_void,
    protocol: u32,
    bytes: *const u8,
    count: usize,
) -> Io {
    io(guarded(|| {
        let protocol = match protocol {
            1 => TargetProtocol::Tcp,
            2 => TargetProtocol::Udp,
            _ => return Err(Error::new(4, "invalid destination protocol")),
        };
        Ok(client(p)?.client.open(protocol, text(bytes, count, 255)?)? as usize)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_stream_write(
    p: *mut c_void,
    stream: u32,
    bytes: *const u8,
    count: usize,
) -> Io {
    io(guarded(|| {
        client(p)?.client.write(stream, input(bytes, count, 32768)?)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_stream_read(
    p: *mut c_void,
    stream: u32,
    maximum: usize,
    output: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| {
        output_valid(output, capacity)?;
        let client = client(p)?;
        if client.pending.is_none() {
            let reply = client.client.read(stream, maximum)?;
            client.pending = Some((stream, reply));
        }
        ensure(
            client.pending.as_ref().unwrap().0 == stream,
            18,
            "response retained for another stream",
        )?;
        take(client, output, capacity)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_datagram(
    p: *mut c_void,
    stream: u32,
    bytes: *const u8,
    count: usize,
    output: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| {
        output_valid(output, capacity)?;
        let client = client(p)?;
        ensure(
            client.pending.is_none(),
            18,
            "consume retained response before sending another datagram",
        )?;
        let data = client
            .client
            .datagram(stream, input(bytes, count, 32768)?)?;
        client.pending = Some((
            stream,
            Reply {
                request: 0,
                outcome: crate::gateway::Outcome::EndpointData,
                value: data.len() as u64,
                data,
            },
        ));
        take(client, output, capacity)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_take(
    p: *mut c_void,
    output: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| take(client(p)?, output, capacity)))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_stream_close(p: *mut c_void, stream: u32, half: bool) -> Io {
    io(guarded(|| {
        let value = client(p)?;
        ensure(
            value.pending.as_ref().is_none_or(|(id, _)| *id != stream),
            18,
            "stream has a retained response",
        )?;
        value.client.close(stream, half)?;
        Ok(0)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_store(
    p: *mut c_void,
    id: *const u8,
    expires: u64,
    bytes: *const u8,
    count: usize,
) -> Io {
    io(guarded(|| {
        let id = input(id, 16, 16)?.try_into().unwrap();
        client(p)?
            .client
            .store(id, expires, input(bytes, count, 32768)?)?;
        Ok(count)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_fetch(
    p: *mut c_void,
    id: *const u8,
    output: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| {
        output_valid(output, capacity)?;
        let client = client(p)?;
        ensure(client.pending.is_none(), 18, "response is already retained")?;
        let data = client
            .client
            .fetch(input(id, 16, 16)?.try_into().unwrap())?;
        client.pending = Some((
            0,
            Reply {
                request: 0,
                outcome: crate::gateway::Outcome::EndpointData,
                value: data.len() as u64,
                data,
            },
        ));
        take(client, output, capacity)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_server_open(bytes: *const u8, count: usize) -> Opened {
    opened(guarded(|| {
        let configuration = config::server_group(text(bytes, count, 65536)?, Path::new("."))?;
        let server = GatewayGroup::start(configuration)?;
        Ok(Box::into_raw(Box::new(ServerHandle {
            magic: SERVER,
            server,
        }))
        .cast())
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_server_close(p: *mut c_void) {
    let _ = guarded(|| {
        if p.is_null() {
            return Ok(());
        }
        server(p)?;
        let mut value = Box::from_raw(p.cast::<ServerHandle>());
        value.magic = 0;
        drop(value);
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_server_statistics(p: *mut c_void) -> Statistics {
    guarded(|| Ok(server(p)?.server.statistics())).unwrap_or_default()
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_server_address(
    p: *mut c_void,
    output: *mut u8,
    capacity: usize,
) -> Io {
    io(guarded(|| {
        let text = server(p)?.server.gateways[0].address.to_string();
        ensure(
            capacity <= 65536 && capacity >= text.len() && !output.is_null(),
            10,
            "server address output capacity",
        )?;
        ptr::copy_nonoverlapping(text.as_ptr(), output, text.len());
        Ok(text.len())
    }))
}

#[no_mangle]
pub unsafe extern "C" fn dln_gateway_consume(p: *mut c_void, id: *const u8) -> Io {
    io(guarded(|| {
        client(p)?
            .client
            .consume(input(id, 16, 16)?.try_into().unwrap())?;
        Ok(0)
    }))
}

#[path = "continuity_api.rs"]
mod continuity_api;

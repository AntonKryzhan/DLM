use super::*;
const POOL: u64 = 0x4447504f4f4c3031;
const OUTBOX: u64 = 0x44474f5554423031;
struct PoolHandle {
    magic: u64,
    pool: crate::path::Pool,
    pending: Option<(u32, Reply)>,
    download: Option<(crate::delivery::Descriptor, Vec<u8>)>,
}
struct OutboxHandle {
    magic: u64,
    journal: crate::journal::Journal,
    peer: [u8; 32],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct DeliveryDescriptor {
    pub id: [u8; 16],
    pub stream: u64,
    pub length: u64,
    pub expires: u64,
    pub digest: [u8; 32],
}
#[repr(C)]
#[derive(Default)]
pub struct DescriptorResult {
    pub descriptor: DeliveryDescriptor,
    pub status: i32,
    pub system: i32,
}
#[repr(C)]
#[derive(Default)]
pub struct DeliveryReceipt {
    pub descriptor: DeliveryDescriptor,
    pub committed: u64,
    pub state: u32,
    pub credit: u32,
    pub status: i32,
    pub system: i32,
}
impl From<crate::delivery::Descriptor> for DeliveryDescriptor {
    fn from(d: crate::delivery::Descriptor) -> Self {
        Self {
            id: d.id,
            stream: d.stream,
            length: d.length,
            expires: d.expires,
            digest: d.digest,
        }
    }
}
impl From<DeliveryDescriptor> for crate::delivery::Descriptor {
    fn from(d: DeliveryDescriptor) -> Self {
        Self {
            id: d.id,
            stream: d.stream,
            length: d.length,
            expires: d.expires,
            digest: d.digest,
        }
    }
}
unsafe fn pool<'a>(p: *mut c_void) -> Result<&'a mut PoolHandle> {
    ensure(!p.is_null(), 4, "null path pool handle")?;
    let value = &mut *p.cast::<PoolHandle>();
    ensure(value.magic == POOL, 4, "invalid path pool handle")?;
    Ok(value)
}
unsafe fn outbox<'a>(p: *mut c_void) -> Result<&'a mut OutboxHandle> {
    ensure(!p.is_null(), 4, "null outbox handle")?;
    let value = &mut *p.cast::<OutboxHandle>();
    ensure(value.magic == OUTBOX, 4, "invalid outbox handle")?;
    Ok(value)
}
fn delivery_receipt(r: Result<crate::delivery::Receipt>) -> DeliveryReceipt {
    match r {
        Ok(r) => {
            let committed = (0..r.descriptor.blocks())
                .filter(|&i| r.has(i))
                .map(|i| r.descriptor.block_len(i).unwrap_or(0) as u64)
                .sum();
            DeliveryReceipt {
                descriptor: r.descriptor.into(),
                committed,
                state: r.state as u32,
                credit: r.credit,
                status: 0,
                system: 0,
            }
        }
        Err(e) => DeliveryReceipt {
            status: e.code,
            system: e.system,
            ..Default::default()
        },
    }
}
unsafe fn pool_take(v: &mut PoolHandle, output: *mut u8, capacity: usize) -> Result<Transfer> {
    output_valid(output, capacity)?;
    let (_, r) = v
        .pending
        .as_ref()
        .ok_or(Error::new(1, "no retained pool response"))?;
    let mut result = Transfer {
        required: r.data.len(),
        copied: 0,
        value: r.value,
        outcome: r.outcome as u32,
        status: 0,
        system: 0,
    };
    if capacity < result.required {
        result.status = 10;
        return Ok(result);
    }
    if result.required > 0 {
        ptr::copy_nonoverlapping(r.data.as_ptr(), output, result.required);
    }
    result.copied = result.required;
    v.pending = None;
    Ok(result)
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_open(bytes: *const u8, count: usize) -> Opened {
    opened(guarded(|| {
        let pool = config::pool(text(bytes, count, 65536)?, Path::new("."))?;
        Ok(Box::into_raw(Box::new(PoolHandle {
            magic: POOL,
            pool,
            pending: None,
            download: None,
        }))
        .cast())
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_close(p: *mut c_void) {
    let _ = guarded(|| {
        if p.is_null() {
            return Ok(());
        }
        pool(p)?;
        let mut v = Box::from_raw(p.cast::<PoolHandle>());
        v.magic = 0;
        drop(v);
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_refresh(p: *mut c_void) -> Io {
    io(guarded(|| {
        let v = pool(p)?;
        v.pool.refresh()?;
        Ok(v.pool.statistics().ready_paths as usize)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_statistics(
    p: *mut c_void,
) -> crate::path::PoolStatistics {
    guarded(|| Ok(pool(p)?.pool.statistics())).unwrap_or_default()
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_metrics(
    p: *mut c_void,
    output: *mut crate::path::PathMetrics,
    capacity: usize,
) -> Io {
    match guarded(|| {
        ensure(
            capacity <= 6 && (capacity == 0 || !output.is_null()),
            4,
            "invalid path metric output buffer",
        )?;
        let values = pool(p)?.pool.metrics();
        if capacity < values.len() {
            return Ok(Io {
                count: values.len(),
                status: 10,
                system: 0,
            });
        }
        ptr::copy_nonoverlapping(values.as_ptr(), output, values.len());
        Ok(Io {
            count: values.len(),
            status: 0,
            system: 0,
        })
    }) {
        Ok(v) => v,
        Err(e) => Io {
            count: 0,
            status: e.code,
            system: e.system,
        },
    }
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_disconnect(p: *mut c_void, index: usize) -> Io {
    io(guarded(|| {
        pool(p)?.pool.disconnect(index)?;
        Ok(0)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_stream_open(
    p: *mut c_void,
    protocol: u32,
    bytes: *const u8,
    count: usize,
) -> Io {
    io(guarded(|| {
        let protocol = match protocol {
            1 => TargetProtocol::Tcp,
            2 => TargetProtocol::Udp,
            _ => return Err(Error::new(4, "invalid pool destination protocol")),
        };
        Ok(pool(p)?.pool.open(protocol, text(bytes, count, 255)?)? as usize)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_stream_write(
    p: *mut c_void,
    stream: u32,
    bytes: *const u8,
    count: usize,
) -> Io {
    io(guarded(|| {
        pool(p)?.pool.write(stream, input(bytes, count, 640)?)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_stream_read(
    p: *mut c_void,
    stream: u32,
    maximum: usize,
    output: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| {
        output_valid(output, capacity)?;
        let v = pool(p)?;
        if v.pending.is_none() {
            v.pending = Some((stream, v.pool.read(stream, maximum)?));
        }
        ensure(
            v.pending.as_ref().unwrap().0 == stream,
            18,
            "pool response retained for another stream",
        )?;
        pool_take(v, output, capacity)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_datagram(
    p: *mut c_void,
    stream: u32,
    bytes: *const u8,
    count: usize,
    output: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| {
        output_valid(output, capacity)?;
        let v = pool(p)?;
        ensure(
            v.pending.is_none(),
            18,
            "consume the retained pool response first",
        )?;
        let data = v.pool.datagram(stream, input(bytes, count, 640)?)?;
        v.pending = Some((
            stream,
            Reply {
                request: 0,
                outcome: crate::gateway::Outcome::EndpointData,
                value: data.len() as u64,
                data,
            },
        ));
        pool_take(v, output, capacity)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_take(
    p: *mut c_void,
    output: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| pool_take(pool(p)?, output, capacity)))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_stream_close(
    p: *mut c_void,
    stream: u32,
    half: bool,
) -> Io {
    io(guarded(|| {
        let v = pool(p)?;
        ensure(
            v.pending.as_ref().is_none_or(|(id, _)| *id != stream),
            18,
            "pool stream has a retained response",
        )?;
        v.pool.close(stream, half)?;
        Ok(0)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_delivery_make(
    id: *const u8,
    stream: u64,
    expires: u64,
    bytes: *const u8,
    count: usize,
) -> DescriptorResult {
    match guarded(|| {
        let d = crate::delivery::Descriptor::new(
            input(id, 16, 16)?.try_into().unwrap(),
            stream,
            expires,
            input(bytes, count, crate::delivery::MAX_TRANSFER)?,
        )?;
        Ok(d.into())
    }) {
        Ok(descriptor) => DescriptorResult {
            descriptor,
            status: 0,
            system: 0,
        },
        Err(e) => DescriptorResult {
            status: e.code,
            system: e.system,
            ..Default::default()
        },
    }
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_upload(
    p: *mut c_void,
    d: DeliveryDescriptor,
    bytes: *const u8,
    count: usize,
) -> DeliveryReceipt {
    delivery_receipt(guarded(|| {
        pool(p)?.pool.upload(
            &d.into(),
            input(bytes, count, crate::delivery::MAX_TRANSFER)?,
        )
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_delivery_status(
    p: *mut c_void,
    d: DeliveryDescriptor,
) -> DeliveryReceipt {
    delivery_receipt(guarded(|| pool(p)?.pool.status(&d.into())))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_consume(
    p: *mut c_void,
    d: DeliveryDescriptor,
) -> DeliveryReceipt {
    delivery_receipt(guarded(|| pool(p)?.pool.consume(&d.into())))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_download(
    p: *mut c_void,
    d: DeliveryDescriptor,
    output: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| {
        ensure(
            capacity <= crate::delivery::MAX_TRANSFER && (capacity == 0 || !output.is_null()),
            4,
            "invalid delivery output buffer",
        )?;
        let v = pool(p)?;
        let d: crate::delivery::Descriptor = d.into();
        d.validate()?;
        if let Some((known, _)) = &v.download {
            ensure(known == &d, 18, "download retained for another delivery")?;
        } else {
            let bytes = v.pool.download(&d)?;
            v.download = Some((d.clone(), bytes));
        }
        let bytes = &v.download.as_ref().unwrap().1;
        let mut r = Transfer {
            required: bytes.len(),
            copied: 0,
            value: d.length,
            outcome: crate::delivery::DeliveryState::Completed as u32,
            status: 0,
            system: 0,
        };
        if capacity < bytes.len() {
            r.status = 10;
            return Ok(r);
        }
        if !bytes.is_empty() {
            ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
        }
        r.copied = bytes.len();
        v.download = None;
        Ok(r)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_outbox_open(bytes: *const u8, count: usize) -> Opened {
    opened(guarded(|| {
        let (journal, peer) = config::outbox(text(bytes, count, 65536)?, Path::new("."))?;
        Ok(Box::into_raw(Box::new(OutboxHandle {
            magic: OUTBOX,
            journal,
            peer,
        }))
        .cast())
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_outbox_close(p: *mut c_void) {
    let _ = guarded(|| {
        if p.is_null() {
            return Ok(());
        }
        outbox(p)?;
        let mut v = Box::from_raw(p.cast::<OutboxHandle>());
        v.magic = 0;
        drop(v);
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_outbox_enqueue(
    p: *mut c_void,
    d: DeliveryDescriptor,
    bytes: *const u8,
    count: usize,
) -> DeliveryReceipt {
    delivery_receipt(guarded(|| {
        let v = outbox(p)?;
        crate::delivery::enqueue(
            &mut v.journal,
            v.peer,
            &d.into(),
            input(bytes, count, crate::delivery::MAX_TRANSFER)?,
        )
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_pool_flush(p: *mut c_void, queue: *mut c_void) -> Io {
    io(guarded(|| {
        let v = pool(p)?;
        let q = outbox(queue)?;
        ensure(
            q.peer == v.pool.peer(),
            16,
            "outbox peer differs from authenticated gateway",
        )?;
        v.pool.flush(&mut q.journal)
    }))
}
#[no_mangle]
pub unsafe extern "C" fn dln_gateway_server_listeners(
    p: *mut c_void,
    output: *mut u8,
    capacity: usize,
) -> Transfer {
    transfer(guarded(|| {
        output_valid(output, capacity)?;
        let listeners = server(p)?.server.addresses();
        let text = listeners
            .iter()
            .map(|(a, k)| format!("{},{}\n", k.name(), a))
            .collect::<String>();
        let mut r = Transfer {
            required: text.len(),
            copied: 0,
            value: listeners.len() as u64,
            outcome: 0,
            status: 0,
            system: 0,
        };
        if capacity < text.len() {
            r.status = 10;
            return Ok(r);
        }
        ptr::copy_nonoverlapping(text.as_ptr(), output, text.len());
        r.copied = text.len();
        Ok(r)
    }))
}

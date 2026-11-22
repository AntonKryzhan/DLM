module dpi.startup
import dpi.diagnostic
import dpi.capture
native {
    trust "WinDivert 2.2";
    extern "C" fn WinDivertOpen(filter: Ptr<U8>, layer: I32, priority: I16, flags: U64) -> OpaqueHandle from "WinDivert 2.2";
    extern "C" fn WinDivertClose(handle_: OpaqueHandle) -> I32 from "WinDivert 2.2" guarantees(NoUnwind);
    unsafe fn close_divert(value: &mut OpaqueHandle) -> Unit guarantees(NoUnwind) { if handle_valid(*value) { WinDivertClose(*value); } }
    unsafe fn startup_state(o: &Options, component: Utf8, error: U32, ready: Bool) -> Unit {
        let dir: Buffer<U16>=wide(utf8_bytes("logs"));CreateDirectoryW(data(&dir),null<U8>());let pid: U32=GetCurrentProcessId();
        let mut name: Buffer<U8>=bytes_copy(utf8_bytes("logs/startup-"));append_number(&mut name,cast<U64>(pid));let mut status: Buffer<U8>=bytes_copy(utf8_bytes("pid="));append_number(&mut status,cast<U64>(pid));bytes_append(&mut status,utf8_bytes("\ncomponent="));bytes_append(&mut status,utf8_bytes(component));bytes_append(&mut status,utf8_bytes("\nwin32_error="));append_number(&mut status,cast<U64>(error));bytes_append(&mut status,utf8_bytes("\nready="));if ready { push(&mut status,U8(49)); }else { push(&mut status,U8(48)); }push(&mut status,U8(10));
        let mut state_path: Buffer<U8>=bytes_copy(slice(&name,0,len(&name)));bytes_append(&mut state_path,utf8_bytes(".status"));write_file(slice(&state_path,0,len(&state_path)),slice(&status,0,len(&status)));
        let mut report: Buffer<U8>=bytes_copy(utf8_bytes("{\"schema\":1,\"event\":\"engine_startup\",\"component\":"));json_text(&mut report,utf8_bytes(component));json_uint(&mut report,utf8_bytes("pid"),cast<U64>(pid));json_uint(&mut report,utf8_bytes("win32_error"),cast<U64>(error));json_bool(&mut report,utf8_bytes("ready"),ready);json_bool(&mut report,utf8_bytes("read_only_driver_probe"),o.driver_check);bytes_append(&mut report,utf8_bytes("}\n"));
        let mut path: Buffer<U8>=bytes_copy(slice(&o.driver_report,0,len(&o.driver_report)));if len(&path) == 0 { path=move name;bytes_append(&mut path,utf8_bytes(".json")); }write_file(slice(&path,0,len(&path)),slice(&report,0,len(&report)));
    }
    unsafe fn startup_failure(o: &Options, d: &mut DiagnosticLog, component: Utf8, code: U32) -> Unit {
        startup_state(o,component,code,false);let mut b: Buffer<U8>=bytes_copy(utf8_bytes("{\"v\":2,\"event\":\"startup_failure\",\"component\":"));json_text(&mut b,utf8_bytes(component));json_uint(&mut b,utf8_bytes("win32_error"),cast<U64>(code));bytes_append(&mut b,utf8_bytes("}\n"));bytes_append(&mut d.buffer_,slice(&b,0,len(&b)));diagnostic_flush(d,true);
    }
    unsafe fn driver_health_check(o: &Options, log: OpaqueHandle) -> U32 {
        // Sniff + receive-only, first a false filter and then the actual filter.
        // Neither handle intercepts or modifies the user's packets.
        for stage in 0..2 {
            let mut filter: Buffer<U8>=zbytes(utf8_bytes("false"));let mut component: Utf8="WinDivertFalseProbe";if stage == 1 { filter=divert_filter(o);component="WinDivertFilterProbe"; }
            let handle_: OpaqueHandle=WinDivertOpen(data(&filter),0,0,5);let error: U32=GetLastError();let owner: Resource<OpaqueHandle,close_divert>=resource(handle_);
            if !handle_valid(handle_) { startup_state(o,component,error,false);log_error(log,utf8_bytes("Driver preflight failed, Win32 error: "),error);return 2; }
        }startup_state(o,"DriverCheckPassed",0,true);log_line(log,utf8_bytes("Read-only WinDivert preflight passed; no interception started."));return 0;
    }
}

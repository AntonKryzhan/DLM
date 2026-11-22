module dpi.win32
import dpi.bytes

native {
    trust "Win32";
    record Overlapped repr(C) { internal: USize; internal_high: USize; offset: U64; event: OpaqueHandle; }
    extern "C" fn CloseHandle(h: OpaqueHandle) -> I32 from "Win32" guarantees(NoUnwind);
    extern "C" fn GetLastError() -> U32 from "Win32" guarantees(NoUnwind);
    extern "C" fn SetLastError(error: U32) -> Unit from "Win32" guarantees(NoUnwind);
    extern "C" fn GetTickCount64() -> U64 from "Win32" guarantees(NoUnwind);
    extern "C" fn GetCurrentProcessId() -> U32 from "Win32" guarantees(NoUnwind);
    extern "C" fn GetModuleFileNameW(module_: OpaqueHandle, path: Ptr<U16>, count: U32) -> U32 from "Win32";
    extern "C" fn GetCommandLineW() -> Ptr<U16> from "Win32";
    extern "C" fn CommandLineToArgvW(command: Ptr<U16>, count: Ptr<I32>) -> Ptr<Ptr<U16>> from "Win32";
    extern "C" fn LocalFree(memory: OpaqueHandle) -> OpaqueHandle from "Win32" guarantees(NoUnwind);
    extern "C" fn MultiByteToWideChar(cp: U32, flags: U32, source: Ptr<U8>, count: I32, target: Ptr<U16>, capacity: I32) -> I32 from "Win32";
    extern "C" fn WideCharToMultiByte(cp: U32, flags: U32, source: Ptr<U16>, count: I32, target: Ptr<U8>, capacity: I32, default_: Ptr<U8>, used: Ptr<I32>) -> I32 from "Win32";
    extern "C" fn CreateFileW(path: Ptr<U16>, access: U32, share: U32, security: Ptr<U8>, creation: U32, attributes: U32, template_: OpaqueHandle) -> OpaqueHandle from "Win32";
    extern "C" fn GetFileSizeEx(file: OpaqueHandle, size: Ptr<I64>) -> I32 from "Win32";
    extern "C" fn ReadFile(file: OpaqueHandle, bytes: Ptr<U8>, capacity: U32, read: Ptr<U32>, overlapped: Ptr<Overlapped>) -> I32 from "Win32";
    extern "C" fn WriteFile(file: OpaqueHandle, bytes: Ptr<U8>, count: U32, written: Ptr<U32>, overlapped: Ptr<Overlapped>) -> I32 from "Win32";
    extern "C" fn FlushFileBuffers(file: OpaqueHandle) -> I32 from "Win32";
    extern "C" fn GetStdHandle(which: U32) -> OpaqueHandle from "Win32";
    extern "C" fn SetCurrentDirectoryW(path: Ptr<U16>) -> I32 from "Win32";
    extern "C" fn SetConsoleOutputCP(cp: U32) -> I32 from "Win32";
    extern "C" fn CreateEventW(security: Ptr<U8>, manual: I32, initial: I32, name: Ptr<U16>) -> OpaqueHandle from "Win32";
    extern "C" fn OpenEventW(access: U32, inherit: I32, name: Ptr<U16>) -> OpaqueHandle from "Win32";
    extern "C" fn SetEvent(event: OpaqueHandle) -> I32 from "Win32";
    extern "C" fn ResetEvent(event: OpaqueHandle) -> I32 from "Win32";
    extern "C" fn CreateMutexW(security: Ptr<U8>, owner: I32, name: Ptr<U16>) -> OpaqueHandle from "Win32";
    extern "C" fn WaitForSingleObject(object: OpaqueHandle, timeout: U32) -> U32 from "Win32";
    extern "C" fn WaitForMultipleObjects(count: U32, handles: Ptr<OpaqueHandle>, all_: I32, timeout: U32) -> U32 from "Win32";
    extern "C" fn GetOverlappedResult(file: OpaqueHandle, overlapped: Ptr<Overlapped>, count: Ptr<U32>, wait: I32) -> I32 from "Win32";
    extern "C" fn CancelIoEx(file: OpaqueHandle, overlapped: Ptr<Overlapped>) -> I32 from "Win32";
    extern "C" fn SetConsoleCtrlHandler(callback: FnPtr<U32,I32>, add: I32) -> I32 from "Win32";
    extern "C" fn ExitProcess(code: U32) -> Unit from "Win32" guarantees(NoUnwind);
    extern "C" fn GetExitCodeProcess(process: OpaqueHandle, code: Ptr<U32>) -> I32 from "Win32";
    extern "C" fn OpenProcess(access: U32, inherit: I32, pid: U32) -> OpaqueHandle from "Win32";
    extern "C" fn GetProcessId(process: OpaqueHandle) -> U32 from "Win32";
    extern "C" fn GetModuleHandleW(name: Ptr<U16>) -> OpaqueHandle from "Win32";
    extern "C" fn SetProcessDPIAware() -> I32 from "Win32";
    extern "C" fn MulDiv(number: I32, numerator: I32, denominator: I32) -> I32 from "Win32";

    unsafe fn handle_valid(h: OpaqueHandle) -> Bool { return address(h) != 0 && address(h) != USize(18446744073709551615); }
    unsafe fn close_handle(value: &mut OpaqueHandle) -> Unit guarantees(NoUnwind) { CloseHandle(*value); }
    unsafe fn free_local(value: &mut OpaqueHandle) -> Unit guarantees(NoUnwind) { LocalFree(*value); }
    unsafe fn wide(text: Slice<U8>) -> Buffer<U16> {
        let mut output: Buffer<U16>=buffer<U16>(1);if len(text) == 0 { return move output; }
        let count: I32=MultiByteToWideChar(65001,8,data(&text),cast<I32>(len(text)),null<U16>(),0);
        if count <= I32(0) { return move output; }resize(&mut output,cast<USize>(count)+1);
        if MultiByteToWideChar(65001,8,data(&text),cast<I32>(len(text)),data(&mut output),count) != count { return buffer<U16>(1); }return move output;
    }
    unsafe fn utf8_wide(source: Ptr<U16>, count: USize) -> Buffer<U8> {
        let mut output: Buffer<U8>=buffer<U8>(0);if count == 0 || count > 32767 { return move output; }
        let bytes: I32=WideCharToMultiByte(65001,128,source,cast<I32>(count),null<U8>(),0,null<U8>(),null<I32>());
        if bytes <= I32(0) { return move output; }resize(&mut output,cast<USize>(bytes));
        if WideCharToMultiByte(65001,128,source,cast<I32>(count),data(&mut output),bytes,null<U8>(),null<I32>()) != bytes { return buffer<U8>(0); }return move output;
    }
    unsafe fn wide_length(source: Ptr<U16>) -> USize {
        if address(source) == 0 { return 0; }let mut count: USize=0;while count < 32767 { if *ptr_offset(source,count) == U16(0) { break; }count=count+1; }return count;
    }
    unsafe fn command_arguments() -> Buffer<Buffer<U8>> {
        let mut count: I32=0;let raw: Ptr<Ptr<U16>>=CommandLineToArgvW(GetCommandLineW(),as_raw(borrowed_ptr(&mut count)));
        let owner: Resource<OpaqueHandle,free_local>=resource(handle(raw));let mut args: Buffer<Buffer<U8>>=buffer<Buffer<U8>>(0);
        if address(raw) == 0 { return move args; }for i in 0..cast<USize>(count) { let word: Ptr<U16>=*ptr_offset(raw,i);push(&mut args,utf8_wide(word,wide_length(word))); }return move args;
    }
    unsafe fn executable_path() -> Buffer<U8> {
        let mut path: Buffer<U16>=buffer<U16>(32768);let count: U32=GetModuleFileNameW(zero<OpaqueHandle>(),data(&mut path),32768);
        if count == U32(0) || count >= U32(32768) { return buffer<U8>(0); }return utf8_wide(data(&path),cast<USize>(count));
    }
    fn directory_path(path: Slice<U8>) -> Buffer<U8> {
        let mut last: USize=0;for i in 0..len(path) { if path[i] == U8(92) || path[i] == U8(47) { last=i; } }return bytes_copy(slice(&path,0,last));
    }
    unsafe fn set_application_directory() -> Bool {
        let path: Buffer<U8>=executable_path();let dir: Buffer<U8>=directory_path(slice(&path,0,len(&path)));let w: Buffer<U16>=wide(slice(&dir,0,len(&dir)));
        return SetCurrentDirectoryW(data(&w)) != I32(0);
    }
    unsafe fn read_file(path: Slice<U8>, maximum: USize) -> Buffer<U8> {
        let name: Buffer<U16>=wide(path);let owner: Resource<OpaqueHandle,close_handle>=resource(CreateFileW(data(&name),0x80000000,7,null<U8>(),3,0x80,zero<OpaqueHandle>()));
        let file: OpaqueHandle=*resource_get(&owner);let mut size: I64=0;
        if !handle_valid(file) || GetFileSizeEx(file,as_raw(borrowed_ptr(&mut size))) == I32(0) || size < I64(0) || cast<U64>(size) > cast<U64>(maximum) { return buffer<U8>(0); }
        let mut output: Buffer<U8>=buffer<U8>(cast<USize>(size));let mut position: USize=0;
        while position < len(&output) {
            let mut actual: U32=0;let remaining: U32=cast<U32>(len(&output)-position);
            if ReadFile(file,ptr_offset(data(&mut output),position),remaining,as_raw(borrowed_ptr(&mut actual)),null<Overlapped>()) == I32(0) || actual == U32(0) || actual > remaining { return buffer<U8>(0); }position=position+cast<USize>(actual);
        }return move output;
    }
    unsafe fn write_all(file: OpaqueHandle, b: Slice<U8>) -> Bool {
        let mut at: USize=0;while at < len(b) {
            let mut count: U32=0;let remaining: U32=cast<U32>(len(b)-at);
            if WriteFile(file,ptr_offset(data(&b),at),remaining,as_raw(borrowed_ptr(&mut count)),null<Overlapped>()) == I32(0) || count == U32(0) || count > remaining { return false; }at=at+cast<USize>(count);
        }return true;
    }
    unsafe fn write_file(path: Slice<U8>, b: Slice<U8>) -> Bool {
        let name: Buffer<U16>=wide(path);let owner: Resource<OpaqueHandle,close_handle>=resource(CreateFileW(data(&name),0x40000000,1,null<U8>(),2,0x80,zero<OpaqueHandle>()));
        let file: OpaqueHandle=*resource_get(&owner);return handle_valid(file) && write_all(file,b);
    }
    unsafe fn log_line(log: OpaqueHandle, text: Slice<U8>) -> Unit {
        let mut b: Buffer<U8>=bytes_copy(text);bytes_append(&mut b,utf8_bytes("\r\n"));
        if handle_valid(log) { write_all(log,slice(&b,0,len(&b))); }
        let console: OpaqueHandle=GetStdHandle(0xfffffff5);if handle_valid(console) { write_all(console,slice(&b,0,len(&b))); }
    }
    unsafe fn log_error(log: OpaqueHandle, text: Slice<U8>, code: U32) -> Unit {
        let mut b: Buffer<U8>=bytes_copy(text);append_number(&mut b,cast<U64>(code));log_line(log,slice(&b,0,len(&b)));
    }
    fn stop_name(pid: U32) -> Buffer<U8> { let mut b: Buffer<U8>=bytes_copy(utf8_bytes("Local\\NoDPI.Stop."));append_number(&mut b,cast<U64>(pid));return move b; }
    unsafe fn signal_stop(pid: U32) -> Bool {
        let name: Buffer<U8>=stop_name(pid);let w: Buffer<U16>=wide(slice(&name,0,len(&name)));
        let owner: Resource<OpaqueHandle,close_handle>=resource(OpenEventW(2,0,data(&w)));let event: OpaqueHandle=*resource_get(&owner);
        return handle_valid(event) && SetEvent(event) != I32(0);
    }
    unsafe fn console_control(kind: U32) -> I32 { if kind <= U32(2) || kind == U32(5) || kind == U32(6) { signal_stop(GetCurrentProcessId());return 1; }return 0; }
}

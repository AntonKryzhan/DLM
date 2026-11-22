module dpi.download
import dpi.win32

native {
    trust "WinHTTP";
    record UrlComponents repr(C) {
        size: U32; scheme: Ptr<U16>; scheme_length: U32; scheme_type: I32;
        host: Ptr<U16>; host_length: U32; port: U16; user: Ptr<U16>; user_length: U32;
        password: Ptr<U16>; password_length: U32; path: Ptr<U16>; path_length: U32; extra: Ptr<U16>; extra_length: U32;
    }
    extern "C" fn WinHttpOpen(agent: Ptr<U16>, access: U32, proxy: Ptr<U16>, bypass: Ptr<U16>, flags: U32) -> OpaqueHandle from "WinHTTP";
    extern "C" fn WinHttpCloseHandle(handle_: OpaqueHandle) -> I32 from "WinHTTP" guarantees(NoUnwind);
    extern "C" fn WinHttpSetTimeouts(handle_: OpaqueHandle, resolve: I32, connect: I32, send: I32, receive: I32) -> I32 from "WinHTTP";
    extern "C" fn WinHttpCrackUrl(url: Ptr<U16>, length: U32, flags: U32, components: Ptr<UrlComponents>) -> I32 from "WinHTTP";
    extern "C" fn WinHttpConnect(session: OpaqueHandle, host: Ptr<U16>, port: U16, reserved: U32) -> OpaqueHandle from "WinHTTP";
    extern "C" fn WinHttpOpenRequest(connection: OpaqueHandle, verb: Ptr<U16>, path: Ptr<U16>, version: Ptr<U16>, referrer: Ptr<U16>, types: Ptr<Ptr<U16>>, flags: U32) -> OpaqueHandle from "WinHTTP";
    extern "C" fn WinHttpSendRequest(request: OpaqueHandle, headers: Ptr<U16>, headers_length: U32, body: Ptr<U8>, length: U32, total: U32, context: USize) -> I32 from "WinHTTP";
    extern "C" fn WinHttpReceiveResponse(request: OpaqueHandle, reserved: Ptr<U8>) -> I32 from "WinHTTP";
    extern "C" fn WinHttpQueryHeaders(request: OpaqueHandle, level: U32, name: Ptr<U16>, buffer_: Ptr<U32>, size: Ptr<U32>, index: Ptr<U32>) -> I32 from "WinHTTP";
    extern "C" fn WinHttpQueryDataAvailable(request: OpaqueHandle, available: Ptr<U32>) -> I32 from "WinHTTP";
    extern "C" fn WinHttpReadData(request: OpaqueHandle, target: Ptr<U8>, capacity: U32, actual: Ptr<U32>) -> I32 from "WinHTTP";
    unsafe fn close_http(value: &mut OpaqueHandle) -> Unit guarantees(NoUnwind) { WinHttpCloseHandle(*value); }
    unsafe fn download(url: Slice<U8>, stop: OpaqueHandle) -> Buffer<U8> {
        let agent: Buffer<U16>=wide(utf8_bytes("NoDPI-DLM/1.0"));
        let session_owner: Resource<OpaqueHandle,close_http>=resource(WinHttpOpen(data(&agent),4,null<U16>(),null<U16>(),0));
        let session: OpaqueHandle=*resource_get(&session_owner);if !handle_valid(session) { return buffer<U8>(0); }WinHttpSetTimeouts(session,3000,3000,5000,5000);
        let w: Buffer<U16>=wide(url);let mut host: Buffer<U16>=buffer<U16>(256);let mut path: Buffer<U16>=buffer<U16>(4096);let mut extra: Buffer<U16>=buffer<U16>(4096);
        let mut uc: UrlComponents=zero<UrlComponents>();uc.size=cast<U32>(size_of<UrlComponents>());uc.host=data(&mut host);uc.host_length=256;
        uc.path=data(&mut path);uc.path_length=4096;uc.extra=data(&mut extra);uc.extra_length=4096;
        if WinHttpCrackUrl(data(&w),0,0,as_raw(borrowed_ptr(&mut uc))) == I32(0) || (uc.scheme_type != I32(1) && uc.scheme_type != I32(2)) || uc.host_length >= U32(256) || uc.path_length >= U32(4096) || uc.extra_length >= U32(4096) { return buffer<U8>(0); }
        host[cast<USize>(uc.host_length)]=0;
        let connection_owner: Resource<OpaqueHandle,close_http>=resource(WinHttpConnect(session,data(&host),uc.port,0));let connection: OpaqueHandle=*resource_get(&connection_owner);
        if !handle_valid(connection) { return buffer<U8>(0); }
        let mut target: Buffer<U16>=buffer<U16>(cast<USize>(uc.path_length+uc.extra_length)+1);
        for i in 0..cast<USize>(uc.path_length) { target[i]=path[i]; }for i in 0..cast<USize>(uc.extra_length) { target[cast<USize>(uc.path_length)+i]=extra[i]; }
        let verb: Buffer<U16>=wide(utf8_bytes("GET"));let mut flags: U32=0;if uc.scheme_type == I32(2) { flags=0x00800000; }
        let request_owner: Resource<OpaqueHandle,close_http>=resource(WinHttpOpenRequest(connection,data(&verb),data(&target),null<U16>(),null<U16>(),null<Ptr<U16>>(),flags));
        let request: OpaqueHandle=*resource_get(&request_owner);if !handle_valid(request) { return buffer<U8>(0); }
        if WinHttpSendRequest(request,null<U16>(),0,null<U8>(),0,0,0) == I32(0) || WinHttpReceiveResponse(request,null<U8>()) == I32(0) { return buffer<U8>(0); }
        let mut status: U32=0;let mut status_size: U32=4;
        if WinHttpQueryHeaders(request,0x20000013,null<U16>(),as_raw(borrowed_ptr(&mut status)),as_raw(borrowed_ptr(&mut status_size)),null<U32>()) == I32(0) || status != U32(200) { return buffer<U8>(0); }
        let deadline: U64=GetTickCount64()+U64(15000);let mut result: Buffer<U8>=buffer<U8>(0);
        while true {
            if GetTickCount64() > deadline || WaitForSingleObject(stop,0) == U32(0) { return buffer<U8>(0); }
            let mut available: U32=0;if WinHttpQueryDataAvailable(request,as_raw(borrowed_ptr(&mut available))) == I32(0) { return buffer<U8>(0); }if available == U32(0) { break; }
            if cast<USize>(available) > 67108864-len(&result) { return buffer<U8>(0); }let old: USize=len(&result);resize(&mut result,old+cast<USize>(available));
            let mut actual: U32=0;if WinHttpReadData(request,ptr_offset(data(&mut result),old),available,as_raw(borrowed_ptr(&mut actual))) == I32(0) || actual > available { return buffer<U8>(0); }
            resize(&mut result,old+cast<USize>(actual));if actual == U32(0) { break; }
        }return move result;
    }
}

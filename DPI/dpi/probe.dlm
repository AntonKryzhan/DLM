module dpi.probe
import dpi.download
import dpi.diagnostic

native {
    record SocketStorage repr(C,align=8) { bytes: Array<U8,128>; }
    record HttpConnectionInfo repr(C) { size: U32; local_: SocketStorage; remote: SocketStorage; }
    extern "C" fn WinHttpSetOption(request: OpaqueHandle, option: U32, value: Ptr<U32>, size: U32) -> I32 from "WinHTTP";
    extern "C" fn WinHttpQueryOption(request: OpaqueHandle, option: U32, value: Ptr<U8>, size: Ptr<U32>) -> I32 from "WinHTTP";
    record ProbeResult repr(C) {
        name: Utf8; host: Host; stage: U32; error: U32; status: U32; bytes: U64; elapsed_ms: U64; headers_ms: U64;
        completed: Bool; sampled: Bool; passed: Bool; critical: Bool; peer: Array<U8,16>; version: U8; peer_port: U16; local_port: U16; protocol_used: U32;
    }
    fn probe_stage(stage: U32) -> Utf8 {
        if stage == U32(11) { return "configure_request"; }if stage == U32(10) { return "https_redirect_to_mwo_login_not_account_authentication"; }if stage == U32(1) { return "parse_url"; }if stage == U32(2) { return "open_session"; }if stage == U32(3) { return "connect_handle"; }
        if stage == U32(4) { return "send_request_dns_tcp_tls"; }if stage == U32(5) { return "receive_headers"; }if stage == U32(6) { return "http_status"; }
        if stage == U32(9) { return "https_response_reachable_not_content_verified"; }if stage == U32(7) { return "read_body"; }if stage == U32(8) { return "http_verified"; }return "not_started";
    }
    fn probe_error(error: U32) -> Utf8 {
        if error == U32(0) { return "none"; }if error == U32(12007) { return "name_not_resolved"; }if error == U32(12029) { return "cannot_connect"; }
        if error == U32(12030) { return "connection_error"; }if error == U32(12002) { return "timeout"; }if error == U32(12175) { return "tls_certificate_or_handshake_failure"; }
        if error == U32(12152) { return "invalid_server_response"; }return "winhttp_or_os_error";
    }
    unsafe fn mwo_login_redirect(request: OpaqueHandle, status: U32) -> Bool {
        if status != U32(301) && status != U32(302) && status != U32(307) && status != U32(308) { return false; }let mut text: Buffer<U16>=buffer<U16>(2048);let mut size: U32=4096;
        if WinHttpQueryHeaders(request,33,null<U16>(),handle_ptr<U32>(handle(data(&mut text))),as_raw(borrowed_ptr(&mut size)),null<U32>()) == I32(0) || size >= U32(4096) || size%U32(2) != U32(0) { return false; }let b: Buffer<U8>=utf8_wide(data(&text),min_size(cast<USize>(size/U32(2)),wide_length(data(&text))));return mwo_redirect(status,slice(&b,0,len(&b)));
    }
    unsafe fn probe_url(name: Utf8, url: Slice<U8>, expected: U32, minimum: USize, maximum: USize, critical: Bool, timeout: U32, http2: Bool, media: Bool) -> ProbeResult {
        let mut r: ProbeResult=ProbeResult(name,zero<Host>(),1,0,0,0,0,0,false,false,false,critical,zero<Array<U8,16>>(),0,0,0,0xffffffff);let start: U64=GetTickCount64();
        let w: Buffer<U16>=wide(url);let mut host: Buffer<U16>=buffer<U16>(256);let mut path: Buffer<U16>=buffer<U16>(16384);let mut extra: Buffer<U16>=buffer<U16>(16384);
        let mut uc: UrlComponents=zero<UrlComponents>();uc.size=cast<U32>(size_of<UrlComponents>());uc.host=data(&mut host);uc.host_length=256;uc.path=data(&mut path);uc.path_length=16384;uc.extra=data(&mut extra);uc.extra_length=16384;
        if WinHttpCrackUrl(data(&w),0,0,as_raw(borrowed_ptr(&mut uc))) == I32(0) || uc.scheme_type != I32(2) || uc.host_length >= U32(256) || uc.path_length >= U32(16384) || uc.extra_length >= U32(16384) || uc.user_length > U32(0) || uc.password_length > U32(0) { r.error=GetLastError();return r; }
        host[cast<USize>(uc.host_length)]=0;let hostname: Buffer<U8>=utf8_wide(data(&host),cast<USize>(uc.host_length));r.host=domain(slice(&hostname,0,len(&hostname)),false);
        let agent: Buffer<U16>=wide(utf8_bytes("NoDPI-DLM-Diagnostics/2.4"));r.stage=2;
        // Direct mode intentionally tests the intercepted path, without a system HTTP proxy.
        let session_owner: Resource<OpaqueHandle,close_http>=resource(WinHttpOpen(data(&agent),1,null<U16>(),null<U16>(),0));let session: OpaqueHandle=*resource_get(&session_owner);
        if !handle_valid(session) { r.error=GetLastError();return r; }WinHttpSetTimeouts(session,cast<I32>(timeout),cast<I32>(timeout),cast<I32>(timeout),cast<I32>(timeout));r.stage=3;
        let connection_owner: Resource<OpaqueHandle,close_http>=resource(WinHttpConnect(session,data(&host),uc.port,0));let connection: OpaqueHandle=*resource_get(&connection_owner);
        if !handle_valid(connection) { r.error=GetLastError();return r; }
        let mut target: Buffer<U16>=buffer<U16>(cast<USize>(uc.path_length+uc.extra_length)+1);for i in 0..cast<USize>(uc.path_length) { target[i]=path[i]; }
        for i in 0..cast<USize>(uc.extra_length) { target[cast<USize>(uc.path_length)+i]=extra[i]; }
        let verb: Buffer<U16>=wide(utf8_bytes("GET"));let request_owner: Resource<OpaqueHandle,close_http>=resource(WinHttpOpenRequest(connection,data(&verb),data(&target),null<U16>(),null<U16>(),null<Ptr<U16>>(),0x00800000));
        let request: OpaqueHandle=*resource_get(&request_owner);if !handle_valid(request) { r.error=GetLastError();return r; }
        let mut disable: U32=7;r.stage=11;if WinHttpSetOption(request,63,as_raw(borrowed_ptr(&mut disable)),4) == I32(0) { r.error=GetLastError();r.elapsed_ms=GetTickCount64()-start;return r; }let mut protocols: U32=0;if http2 { protocols=1; }WinHttpSetOption(request,133,as_raw(borrowed_ptr(&mut protocols)),4);
        let mut headers: Buffer<U16>=buffer<U16>(0);if media { headers=wide(utf8_bytes("Range: bytes=0-1048575\r\n")); }
        r.stage=4;let mut header_pointer: Ptr<U16>=null<U16>();let mut header_length: U32=0;if media { header_pointer=data(&headers);header_length=0xffffffff; }
        if WinHttpSendRequest(request,header_pointer,header_length,null<U8>(),0,0,0) == I32(0) { r.error=GetLastError();r.elapsed_ms=GetTickCount64()-start;return r; }
        r.stage=5;if WinHttpReceiveResponse(request,null<U8>()) == I32(0) { r.error=GetLastError();r.elapsed_ms=GetTickCount64()-start;return r; }
        r.headers_ms=GetTickCount64()-start;let mut size: U32=4;r.stage=6;
        if WinHttpQueryHeaders(request,0x20000013,null<U16>(),as_raw(borrowed_ptr(&mut r.status)),as_raw(borrowed_ptr(&mut size)),null<U32>()) == I32(0) { r.error=GetLastError();r.elapsed_ms=GetTickCount64()-start;return r; }
        let mut info: HttpConnectionInfo=zero<HttpConnectionInfo>();info.size=cast<U32>(size_of<HttpConnectionInfo>());let mut info_size: U32=info.size;
        if WinHttpQueryOption(request,93,handle_ptr<U8>(handle(as_raw(borrowed_ptr(&mut info)))),as_raw(borrowed_ptr(&mut info_size))) != I32(0) {
            let family: U16=read_le<U16>(&info.remote.bytes,0);r.peer_port=read_be<U16>(&info.remote.bytes,2);r.local_port=read_be<U16>(&info.local_.bytes,2);
            if family == U16(2) { r.version=4;for i in 0..4 { r.peer[i]=info.remote.bytes[4+i]; } }
            if family == U16(23) { r.version=6;for i in 0..16 { r.peer[i]=info.remote.bytes[8+i]; } }
        }
        let mut protocol_size: U32=4;WinHttpQueryOption(request,134,handle_ptr<U8>(handle(as_raw(borrowed_ptr(&mut r.protocol_used)))),as_raw(borrowed_ptr(&mut protocol_size)));
        if bytes_at(utf8_bytes(name),0,utf8_bytes("baseline_")) { r.passed=r.status >= U32(200) && r.status < U32(500);r.stage=9;r.elapsed_ms=GetTickCount64()-start;return r; }
        if bytes_equal(utf8_bytes(name),utf8_bytes("mwo_ac_frontend")) && mwo_login_redirect(request,r.status) { r.passed=true;r.stage=10;r.elapsed_ms=GetTickCount64()-start;return r; }
        let ok_status: Bool=r.status == expected || (media && r.status == U32(206));
        if !ok_status { r.elapsed_ms=GetTickCount64()-start;return r; }
        r.stage=7;let mut buffer_: Buffer<U8>=buffer<U8>(16384);let mut body: Buffer<U8>=buffer<U8>(maximum);resize(&mut body,0);let deadline: U64=GetTickCount64()+U64(12000);
        while r.bytes < cast<U64>(maximum) {
            if GetTickCount64() > deadline { r.error=12002;break; }let amount: U32=cast<U32>(min_size(16384,maximum-cast<USize>(r.bytes)));let mut actual: U32=0;
            if WinHttpReadData(request,data(&mut buffer_),amount,as_raw(borrowed_ptr(&mut actual))) == I32(0) { r.error=GetLastError();break; }
            if actual > amount { r.error=13;break; }if actual == U32(0) { r.completed=true;break; }r.bytes=r.bytes+cast<U64>(actual);bytes_append(&mut body,slice(&buffer_,0,cast<USize>(actual)));
        }
        r.sampled=!r.completed && r.bytes == cast<U64>(maximum);r.elapsed_ms=GetTickCount64()-start;
        if r.error == U32(0) && r.bytes >= cast<U64>(minimum) && (r.completed || r.sampled) {
            r.passed=true;r.stage=8;
            if bytes_at(utf8_bytes(name),0,utf8_bytes("mwo_")) && !mwo_content(utf8_bytes(name),slice(&body,0,len(&body))) { r.passed=false; }
            if bytes_equal(utf8_bytes(name),utf8_bytes("discord_gateway_api")) && (bytes_find(slice(&body,0,len(&body)),0,utf8_bytes("wss://")) == len(&body)) { r.passed=false; }
        }return r;
    }
    fn probe_json(r: &ProbeResult) -> Buffer<U8> {
        let mut b: Buffer<U8>=bytes_copy(utf8_bytes("{\"target\":"));json_text(&mut b,utf8_bytes(r.name));bytes_append(&mut b,utf8_bytes(",\"host\":"));json_text(&mut b,slice(&r.host.bytes,0,r.host.length));
        bytes_append(&mut b,utf8_bytes(",\"stage\":"));json_text(&mut b,utf8_bytes(probe_stage(r.stage)));json_uint(&mut b,utf8_bytes("win32_error"),cast<U64>(r.error));bytes_append(&mut b,utf8_bytes(",\"error_group\":"));json_text(&mut b,utf8_bytes(probe_error(r.error)));
        json_uint(&mut b,utf8_bytes("http_status"),cast<U64>(r.status));json_uint(&mut b,utf8_bytes("body_bytes"),r.bytes);json_uint(&mut b,utf8_bytes("headers_ms"),r.headers_ms);json_uint(&mut b,utf8_bytes("elapsed_ms"),r.elapsed_ms);
        json_uint(&mut b,utf8_bytes("protocol_used_0_h1_1_h2_2_h3_max_unknown"),cast<U64>(r.protocol_used));json_bool(&mut b,utf8_bytes("completed"),r.completed);json_bool(&mut b,utf8_bytes("sample_limit_reached"),r.sampled);json_bool(&mut b,utf8_bytes("passed"),r.passed);json_bool(&mut b,utf8_bytes("critical"),r.critical);json_bool(&mut b,utf8_bytes("redirect_to_mwo_login"),r.stage == U32(10));
        if r.version != U8(0) { let ip: Buffer<U8>=ip_text(&r.peer,r.version);bytes_append(&mut b,utf8_bytes(",\"peer_ip\":"));json_text(&mut b,slice(&ip,0,len(&ip)));json_uint(&mut b,utf8_bytes("peer_port"),cast<U64>(r.peer_port));json_uint(&mut b,utf8_bytes("local_port"),cast<U64>(r.local_port)); }
        push(&mut b,U8(125));return move b;
    }
    unsafe fn probe_main() -> Unit {
        assert(size_of<HttpConnectionInfo>() == 264);set_application_directory();SetConsoleOutputCP(65001);let args: Buffer<Buffer<U8>>=command_arguments();let mut report_path: Buffer<U8>=bytes_copy(utf8_bytes("logs/probe-latest.json"));
        let mut label: Buffer<U8>=bytes_copy(utf8_bytes("manual"));let mut media_url: Buffer<U8>=buffer<U8>(0);let mut timeout: U32=4000;let mut http2: Bool=true;let mut baseline: Bool=false;let mut target: U32=2;
        for i in 1..len(&args) {
            let a: Slice<U8>=slice(&args[i],0,len(&args[i]));
            if bytes_at(a,0,utf8_bytes("--report=")) { report_path=bytes_copy(slice(&a,9,len(a)-9)); }
            else if bytes_at(a,0,utf8_bytes("--label=")) { label=bytes_copy(slice(&a,8,min_size(100,len(a)-8))); }
            else if bytes_at(a,0,utf8_bytes("--media-url-file=")) { let file: Buffer<U8>=read_file(slice(&a,17,len(a)-17),16384);let raw: Slice<U8>=slice(&file,0,len(&file));let value: Slice<U8>=trimmed(raw);media_url=bytes_copy(value);if len(&media_url) == 0 { print("Media URL file is empty or cannot be read");ExitProcess(2);return; } }
            else if bytes_at(a,0,utf8_bytes("--target=")) { if !decimal(slice(&a,9,len(a)-9),6,&mut target) { print("Invalid target: 0 YouTube, 1 Discord, 2 both, 3 Telegram, 4 YT+Discord+Telegram, 5 MWO/CDN, 6 all");ExitProcess(2);return; } }
            else if bytes_equal(a,utf8_bytes("--baseline")) { baseline=true; }
            else if bytes_equal(a,utf8_bytes("--http1")) { http2=false; }
            else if bytes_at(a,0,utf8_bytes("--timeout-ms=")) { if !decimal(slice(&a,13,len(a)-13),10000,&mut timeout) || timeout < U32(500) { print("Invalid timeout");ExitProcess(2);return; } }
            else { print("NoDPIProbe.exe [--baseline] [--report=FILE] [--label=TEXT] [--timeout-ms=500..10000] [--http1] [--media-url-file=FILE]");ExitProcess(2);return; }
        }
        let dir: Buffer<U16>=wide(utf8_bytes("logs"));CreateDirectoryW(data(&dir),null<U8>());let mut results: Buffer<ProbeResult>=buffer<ProbeResult>(0);
        print("Testing HTTPS paths with Windows Schannel. This does not test Discord voice or an authenticated browser session.");
        if baseline {
            print("Direct baseline / Russia and external control; no engine changes...");
            push(&mut results,probe_url("baseline_yandex",utf8_bytes("https://ya.ru/"),200,0,16384,false,timeout,http2,false));
            push(&mut results,probe_url("baseline_mail",utf8_bytes("https://mail.ru/"),200,0,16384,false,timeout,http2,false));
            push(&mut results,probe_url("baseline_wikipedia",utf8_bytes("https://www.wikipedia.org/"),200,0,16384,false,timeout,http2,false));
        }else {
        if target_youtube(target) {
        print("YouTube 204...");push(&mut results,probe_url("youtube_204",utf8_bytes("https://www.youtube.com/generate_204"),204,0,16384,true,timeout,http2,false));
        print("YouTube page / sustained sample...");push(&mut results,probe_url("youtube_page",utf8_bytes("https://www.youtube.com/"),200,1024,262144,true,timeout,http2,false));
        }if target_discord(target) {
        print("Discord Gateway API...");push(&mut results,probe_url("discord_gateway_api",utf8_bytes("https://discord.com/api/v10/gateway"),200,8,65536,true,timeout,http2,false));
        print("Discord web page...");push(&mut results,probe_url("discord_page",utf8_bytes("https://discord.com/"),200,1024,262144,true,timeout,http2,false));
        }
        if target_telegram(target) {
        print("Telegram Web A...");push(&mut results,probe_url("telegram_web_a",utf8_bytes("https://web.telegram.org/a/"),200,1024,262144,true,timeout,http2,false));
        print("Telegram Web K...");push(&mut results,probe_url("telegram_web_k",utf8_bytes("https://web.telegram.org/k/"),200,1024,262144,true,timeout,http2,false));
        }
        if target_mwo(target) {
        print("MWO public website...");push(&mut results,probe_url("mwo_website",utf8_bytes("https://mwomercs.com/"),200,1024,262144,true,timeout,http2,false));
        print("MWO public login form; no account login...");push(&mut results,probe_url("mwo_login",utf8_bytes("https://mwomercs.com/login"),200,1024,131072,true,timeout,http2,false));
        print("MWO ac HTTPS frontend...");push(&mut results,probe_url("mwo_ac_frontend",utf8_bytes("https://ac.mwomercs.com/login"),200,1024,131072,true,timeout,http2,false));
        print("MWO CDN sample beyond 16 KiB...");push(&mut results,probe_url("mwo_public_css",utf8_bytes("https://static.mwomercs.com/css/mwo.4.css"),200,32768,262144,true,timeout,http2,false));
        }
        print("Control / Wikipedia...");push(&mut results,probe_url("control_wikipedia",utf8_bytes("https://www.wikipedia.org/"),200,256,65536,false,timeout,http2,false));
        if target_youtube(target) && len(&media_url) > 0 { print("User-provided media URL / one MiB sample...");push(&mut results,probe_url("youtube_media_sample",slice(&media_url,0,len(&media_url)),200,65536,1048576,true,timeout,http2,true)); }
        print("Control / Yandex...");push(&mut results,probe_url("baseline_yandex",utf8_bytes("https://ya.ru/"),200,0,16384,false,timeout,http2,false));
        print("Control / Mail...");push(&mut results,probe_url("baseline_mail",utf8_bytes("https://mail.ru/"),200,0,16384,false,timeout,http2,false));
        }
        let mut output: Buffer<U8>=bytes_copy(utf8_bytes("{\"schema\":2,\"tool\":\"NoDPIProbe-2.4\",\"stack\":\"WinHTTP-Schannel\",\"label\":"));json_text(&mut output,slice(&label,0,len(&label)));
        json_uint(&mut output,utf8_bytes("target_0_youtube_1_discord_2_both"),cast<U64>(target));json_uint(&mut output,utf8_bytes("target"),cast<U64>(target));
        json_bool(&mut output,utf8_bytes("http2_requested"),http2);json_bool(&mut output,utf8_bytes("media_tested"),target_youtube(target) && len(&media_url) > 0);json_bool(&mut output,utf8_bytes("voice_tested"),false);json_bool(&mut output,utf8_bytes("mwo_account_authentication_tested"),false);
        let mut passed: U64=0;let mut critical: U64=0;let mut latency: U64=0;let mut baseline_passed: U64=0;let mut baseline_total: U64=0;json_bool(&mut output,utf8_bytes("baseline_only"),baseline);bytes_append(&mut output,utf8_bytes(",\"targets\":["));
        for i in 0..len(&results) {
            let r: ProbeResult=results[i];if baseline { baseline_total=baseline_total+U64(1);if r.passed { baseline_passed=baseline_passed+U64(1); } }if r.critical { critical=critical+U64(1);if r.passed { passed=passed+U64(1);latency=latency+r.elapsed_ms; } }
            let b: Buffer<U8>=probe_json(&r);if i > 0 { push(&mut output,U8(44)); }bytes_append(&mut output,slice(&b,0,len(&b)));log_line(zero<OpaqueHandle>(),slice(&b,0,len(&b)));
        }
        push(&mut output,U8(93));if baseline { json_uint(&mut output,utf8_bytes("baseline_passed"),baseline_passed);json_uint(&mut output,utf8_bytes("baseline_total"),baseline_total); }json_uint(&mut output,utf8_bytes("critical_passed"),passed);json_uint(&mut output,utf8_bytes("critical_total"),critical);json_uint(&mut output,utf8_bytes("successful_latency_ms"),latency);
        bytes_append(&mut output,utf8_bytes(",\"interpretation\":\"HTTP probes only; MWO account login/gameplay, playback and voice require manual verification\"}\n"));
        if !write_file(slice(&report_path,0,len(&report_path)),slice(&output,0,len(&output))) { print("Could not write probe report");ExitProcess(2);return; }
        print("Probe report saved. Collect-Diagnostics.cmd packages it with the engine logs.");if baseline { if baseline_passed == U64(0) { ExitProcess(1);return; }ExitProcess(0);return; }if passed != critical { ExitProcess(1);return; }ExitProcess(0);
    }
}

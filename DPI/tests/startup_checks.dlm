module tests.startup_checks
import dpi.startup_view
native {
    fn main() -> Unit {
        let failed: StartupView=startup_view(utf8_bytes("pid=42\ncomponent=WinDivertOpen\nwin32_error=58\nready=0\n"));assert(failed.found && failed.driver && !failed.ready && failed.error == U32(58));let caption: Buffer<U8>=startup_caption(failed.error);assert(bytes_find(slice(&caption,0,len(&caption)),0,utf8_bytes("58")) < len(&caption));
        let policy: StartupView=startup_view(utf8_bytes("component=PolicyConfiguration\nwin32_error=87\nready=0\n"));assert(policy.found && !policy.driver && !policy.ready);
        let success: StartupView=startup_view(utf8_bytes("component=EngineReady\nwin32_error=0\nready=1\n"));assert(success.found && success.ready && !success.driver);
        let empty: StartupView=startup_view(utf8_bytes(""));assert(!empty.found);let broken: StartupView=startup_view(utf8_bytes("win32_error=4294967296\nready=1\n"));assert(!broken.found);let incomplete: StartupView=startup_view(utf8_bytes("win32_error=58\nready=2\n"));assert(!incomplete.found);
        print("DPI 2.4 startup status and failure classification passed");
    }
}

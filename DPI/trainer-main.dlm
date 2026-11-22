module nodpi.trainer
import dpi.genetics
import dpi.mtproto
import dpi.win32

native {
    unsafe fn main() -> Unit {
        set_application_directory();let args: Buffer<Buffer<U8>>=command_arguments();
        if len(&args) == 4 && bytes_equal(slice(&args[1],0,len(&args[1])),utf8_bytes("--mtp-build")) {
            let context: Buffer<U8>=read_file(slice(&args[2],0,len(&args[2])),25);let request: Buffer<U8>=mtp_request(slice(&context,0,len(&context)));
            if len(&request) != 41 || !write_file(slice(&args[3],0,len(&args[3])),slice(&request,0,len(&request))) { print("Invalid MTProto probe context or output");ExitProcess(2);return; }ExitProcess(0);return;
        }
        if len(&args) == 5 && bytes_equal(slice(&args[1],0,len(&args[1])),utf8_bytes("--mtp-verify")) {
            let nonce: Buffer<U8>=read_file(slice(&args[2],0,len(&args[2])),17);let body: Buffer<U8>=read_file(slice(&args[3],0,len(&args[3])),1048577);let result: U32=mtp_verify(slice(&nonce,0,len(&nonce)),slice(&body,0,len(&body)));
            let mut report: Buffer<U8>=bytes_copy(utf8_bytes("{\"schema\":1,\"res_pq_valid\":"));if result == U32(0) { bytes_append(&mut report,utf8_bytes("true")); }else { bytes_append(&mut report,utf8_bytes("false")); }
            bytes_append(&mut report,utf8_bytes(",\"validation_code\":"));append_number(&mut report,cast<U64>(result));bytes_append(&mut report,utf8_bytes(",\"transport_error\":"));let mut error: U32=0;
            if result == U32(4) { error=wrapping_sub(U32(0),read_le<U32>(&body,0)); }append_number(&mut report,cast<U64>(error));bytes_append(&mut report,utf8_bytes("}\n"));
            if !write_file(slice(&args[4],0,len(&args[4])),slice(&report,0,len(&report))) { print("Could not save MTProto probe result");ExitProcess(2);return; }ExitProcess(0);return;
        }
        if len(&args) != 3 { print("DPITrainer.exe INPUT OUTPUT");ExitProcess(2);return; }
        let input: Buffer<U8>=read_file(slice(&args[1],0,len(&args[1])),1048576);if len(&input) == 0 { print("Evolution input is empty or unreadable");ExitProcess(2);return; }let mut items: Buffer<Individual>=buffer<Individual>(0);let mut state: U64=GetTickCount64() ^ cast<U64>(GetCurrentProcessId());let mut at: USize=0;
        while at < len(&input) { let end: USize=byte_find(slice(&input,0,len(&input)),at,U8(10));let raw: Slice<U8>=slice(&input,at,end-at);let line: Slice<U8>=trimmed(raw);at=end+1;if len(line) == 0 { continue; }
            let separator: USize=byte_find(line,0,U8(124));let mut gene: Gene=gene_default();let mut score: U32=0;
            if separator == len(line) || !gene_parse(slice(&line,0,separator),&mut gene) || !decimal(slice(&line,separator+1,len(line)-separator-1),1000000,&mut score) { print("Invalid evolution input");ExitProcess(2);return; }
            push(&mut items,Individual(gene,score,1));if len(&items) > 512 { print("Population input too large");ExitProcess(2);return; }
        }
        let genes: Buffer<Gene>=evolve(&items,&mut state,8);if len(&genes) != 8 { print("Could not produce a distinct population");ExitProcess(2);return; }
        let mut out: Buffer<U8>=buffer<U8>(0);for i in 0..len(&genes) { let text: Buffer<U8>=gene_text(&genes[i]);bytes_append(&mut out,slice(&text,0,len(&text)));push(&mut out,U8(10)); }
        if !write_file(slice(&args[2],0,len(&args[2])),slice(&out,0,len(&out))) { print("Could not save next population");ExitProcess(2);return; }ExitProcess(0);
    }
}

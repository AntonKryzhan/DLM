module dpi.mtproto
import dpi.bytes

native {
    // The probe stops at public resPQ. No account, authorization or session key.
    fn mtp_request(context: Slice<U8>) -> Buffer<U8> {
        let mut b: Buffer<U8>=buffer<U8>(0);if len(context) != 24 || (context[16] & U8(3)) != U8(0) || read_le<U64>(&context,16) == U64(0) { return move b; }
        resize(&mut b,41);b[0]=10;for i in 0..8 { b[9+i]=context[16+i]; }
        write_le<U32>(&mut b,17,20);write_le<U32>(&mut b,21,0xbe7e8ef1);for i in 0..16 { b[25+i]=context[i]; }return move b;
    }
    // 0 valid resPQ; 1 invalid envelope; 2 wrong nonce/constructor;
    // 3 malformed TL; 4 transport error. The input excludes abridged framing.
    fn mtp_verify(nonce: Slice<U8>, b: Slice<U8>) -> U32 {
        if len(b) == 4 && (b[3] & U8(128)) != U8(0) { return 4; }
        if len(nonce) != 16 || len(b) < 76 || len(b) > 1048576 || read_le<U64>(&b,0) != U64(0) || read_le<U32>(&b,16) != cast<U32>(len(b)-20) || (b[8] & U8(3)) != U8(1) { return 1; }
        if read_le<U32>(&b,20) != U32(0x05162463) || !bytes_at(b,24,nonce) { return 2; }
        let n: USize=cast<USize>(b[56]);if n == 0 || n > 8 { return 3; }
        let end: USize=(57+n+3) & USize(0xfffffffffffffffc);if end+8 > len(b) || read_le<U32>(&b,end) != U32(0x1cb5c415) { return 3; }
        let count: U32=read_le<U32>(&b,end+4);if count == U32(0) || count > U32(32) || len(b)-end-8 != cast<USize>(count)*8 { return 3; }return 0;
    }
}

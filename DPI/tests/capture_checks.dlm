module tests.capture_checks
import dpi.capture
native {
    fn main() -> Unit {
        for mode in 0..4 { for all in 0..2 { for game in 0..2 { for udp in 0..2 {
            let mut o: Options=default_options();o.quic=cast<U32>(mode);o.all_ports=all == 1;o.game=game == 1;o.udp_obfuscate=udp == 1;
            let b: Buffer<U8>=divert_filter(&o);let mut depth: I32=0;assert(b[len(&b)-1] == U8(0));
            for i in 0..len(&b)-1 { if b[i] == U8(40) { depth=depth+I32(1); }if b[i] == U8(41) { depth=depth-I32(1); }assert(depth >= I32(0)); }assert(depth == I32(0));
            assert(bytes_at(slice(&b,0,len(&b)),0,utf8_bytes("not loopback and not impostor and (ip or ipv6) and (")));
        } } } }print("DPI DLM 2.0: 32 capture filter combinations passed");
    }
}

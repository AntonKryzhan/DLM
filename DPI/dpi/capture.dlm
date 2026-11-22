module dpi.capture
import dpi.options

native {
    fn divert_filter(o: &Options) -> Buffer<U8> {
        let mut b: Buffer<U8>=bytes_copy(utf8_bytes("not loopback and not impostor and (ip or ipv6) and ((outbound and tcp"));
        if !o.all_ports { if o.telegram { bytes_append(&mut b,utf8_bytes(" and (tcp.DstPort == 80 or tcp.DstPort == 443 or tcp.DstPort == 5222)")); }else { bytes_append(&mut b,utf8_bytes(" and (tcp.DstPort == 80 or tcp.DstPort == 443)")); } }
        bytes_append(&mut b,utf8_bytes(" and (tcp.PayloadLength > 0 or tcp.Syn or tcp.Fin or tcp.Rst)) or (inbound and tcp"));
        if !o.all_ports { if o.telegram { bytes_append(&mut b,utf8_bytes(" and (tcp.SrcPort == 80 or tcp.SrcPort == 443 or tcp.SrcPort == 5222)")); }else { bytes_append(&mut b,utf8_bytes(" and (tcp.SrcPort == 80 or tcp.SrcPort == 443)")); } }
        bytes_append(&mut b,utf8_bytes(")"));
        if !o.game && o.quic != U32(0) {
            bytes_append(&mut b,utf8_bytes(" or (outbound and udp and udp.DstPort == 443)"));if o.quic == U32(1) { bytes_append(&mut b,utf8_bytes(" or (inbound and udp and udp.SrcPort == 443 and udp.PayloadLength >= 7 and udp.Payload[0] >= 0x80)")); }
        }
        if o.udp_obfuscate && !o.game { bytes_append(&mut b,utf8_bytes(" or (outbound and udp and (udp.DstPort == 3478 or udp.DstPort == 5349 or udp.DstPort >= 50000)) or (inbound and udp and (udp.SrcPort == 3478 or udp.SrcPort == 5349 or udp.SrcPort >= 50000))")); }
        bytes_append(&mut b,utf8_bytes(")"));push(&mut b,U8(0));return move b;
    }
}

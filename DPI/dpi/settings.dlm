module dpi.settings
import dpi.options

native {
    fn setting_bool(value: Slice<U8>) -> Bool { return bytes_equal(value,utf8_bytes("1")) || (len(value) == 4 && ascii_at(value,0,utf8_bytes("true"))); }
    fn ini_decode(text: Slice<U8>) -> Buffer<U8> {
        let mut b: Buffer<U8>=buffer<U8>(0);let mut p: USize=0;let mut end: USize=len(text);
        if end >= 2 && text[0] == U8(34) && text[end-1] == U8(34) { p=1;end=end-1; }
        while p < end {
            let c: U8=text[p];if c == U8(92) && p+1 < end && (text[p+1] == U8(92) || text[p+1] == U8(34)) { p=p+1; }
            push(&mut b,text[p]);p=p+1;
        }return move b;
    }
    fn load_settings(o: &mut Options, b: Slice<U8>) -> Bool {
        let mut p: USize=0;let mut active: Bool=false;let mut valid: Bool=true;let mut explicit_quic: Bool=false;
        if bytes_at(b,0,utf8_bytes("﻿")) { p=3; }
        while p < len(b) {
            let z: USize=byte_find(b,p,U8(10));let raw: Slice<U8>=slice(&b,p,z-p);let line: Slice<U8>=trimmed(raw);p=z+1;
            if len(line) == 0 || line[0] == U8(59) || line[0] == U8(35) { continue; }
            if line[0] == U8(91) { active=bytes_equal(line,utf8_bytes("[NoDPI]"));continue; }if !active { continue; }
            let eq: USize=byte_find(line,0,U8(61));if eq == len(line) { continue; }
            let key_raw: Slice<U8>=slice(&line,0,eq);let key: Slice<U8>=trimmed(key_raw);
            let value_raw: Slice<U8>=slice(&line,eq+1,len(line)-eq-1);let value_trim: Slice<U8>=trimmed(value_raw);
            let decoded: Buffer<U8>=ini_decode(value_trim);let value: Slice<U8>=slice(&decoded,0,len(&decoded));let flag: Bool=setting_bool(value);
            if bytes_equal(key,utf8_bytes("CloudCdnEnabled")) { o.cloud_cdn=flag; }
            else if bytes_equal(key,utf8_bytes("TelegramEnabled")) { o.telegram=flag; }
            else if bytes_equal(key,utf8_bytes("SafeAutomatic")) { o.safe_automatic=flag; }
            else if bytes_equal(key,utf8_bytes("AdaptiveStrategies")) { o.auto_strategy=flag; }
            else if bytes_equal(key,utf8_bytes("ServerWindowCandidates")) { o.server_window=flag; }
            else if bytes_equal(key,utf8_bytes("UdpObfuscate")) { o.udp_obfuscate=flag; }
            else if bytes_equal(key,utf8_bytes("DiagnosticLog")) { o.diagnostic=flag; }
            else if bytes_equal(key,utf8_bytes("Mode")) { o.all_ports=bytes_equal(value,utf8_bytes("AllPorts"));o.game=bytes_equal(value,utf8_bytes("GameMode")); }
            else if bytes_equal(key,utf8_bytes("AggressiveFrag")) { o.aggressive=flag; }
            else if bytes_equal(key,utf8_bytes("FragBySNI")) { o.sni=flag; }
            else if bytes_equal(key,utf8_bytes("ApplyECH")) { o.ech=flag; }
            else if bytes_equal(key,utf8_bytes("AdaptiveRetry")) { o.adaptive=flag; }
            else if bytes_equal(key,utf8_bytes("LearnStrategy")) { o.learn=flag; }
            else if bytes_equal(key,utf8_bytes("FakePackets")) { o.fake=flag; }
            else if bytes_equal(key,utf8_bytes("HttpManip")) { o.http=flag; }
            else if bytes_equal(key,utf8_bytes("ReverseFrag")) { o.reverse=flag; }
            else if bytes_equal(key,utf8_bytes("AwsWhitelistOnly")) { o.whitelist=flag; }
            else if bytes_equal(key,utf8_bytes("DebugVerbose")) { if flag { o.debug=2; } }
            else if bytes_equal(key,utf8_bytes("Debug")) { if flag && o.debug < U32(1) { o.debug=1; } }
            else if bytes_equal(key,utf8_bytes("BlockQUIC")) { if flag && !explicit_quic { o.quic=2; } }
            else {
                let mut name: Buffer<U8>=buffer<U8>(0);
                if bytes_equal(key,utf8_bytes("StrategyProfile")) { name=bytes_copy(utf8_bytes("--strategy-profile=")); }
                else if bytes_equal(key,utf8_bytes("HandshakeTimeoutMs")) { name=bytes_copy(utf8_bytes("--handshake-timeout-ms=")); }
                else if bytes_equal(key,utf8_bytes("QuicMode")) { explicit_quic=true;name=bytes_copy(utf8_bytes("--quic-mode=")); }
                else if bytes_equal(key,utf8_bytes("SplitMode")) { name=bytes_copy(utf8_bytes("--split-mode=")); }
                else if bytes_equal(key,utf8_bytes("SplitStrategy")) { name=bytes_copy(utf8_bytes("--split-strategy=")); }
                else if bytes_equal(key,utf8_bytes("FakeMode")) { name=bytes_copy(utf8_bytes("--fake-mode=")); }
                else if bytes_equal(key,utf8_bytes("FirstPackets")) { name=bytes_copy(utf8_bytes("--first-packets=")); }
                else if bytes_equal(key,utf8_bytes("HandshakeJitterMs")) { name=bytes_copy(utf8_bytes("--handshake-jitter-ms=")); }
                else if bytes_equal(key,utf8_bytes("QuicProbeMs")) { name=bytes_copy(utf8_bytes("--quic-probe-ms=")); }
                else if bytes_equal(key,utf8_bytes("AutoTTL")) { if len(value) > 0 { name=bytes_copy(utf8_bytes("--auto-ttl=")); } }
                else if bytes_equal(key,utf8_bytes("MinTTL")) { name=bytes_copy(utf8_bytes("--min-ttl=")); }
                else if bytes_equal(key,utf8_bytes("Hostlist")) { name=bytes_copy(utf8_bytes("--hostlist=")); }
                else if bytes_equal(key,utf8_bytes("HostlistExclude")) { name=bytes_copy(utf8_bytes("--hostlist-exclude=")); }
                else if bytes_equal(key,utf8_bytes("IPSet")) { name=bytes_copy(utf8_bytes("--ipset=")); }
                else if bytes_equal(key,utf8_bytes("IPSetExclude")) { name=bytes_copy(utf8_bytes("--ipset-exclude=")); }
                else if bytes_equal(key,utf8_bytes("AwsJson")) { name=bytes_copy(utf8_bytes("--aws-json=")); }
                else if bytes_equal(key,utf8_bytes("AwsServices")) { name=bytes_copy(utf8_bytes("--aws-services=")); }
                if len(&name) > 0 { bytes_append(&mut name,value);if !option(o,slice(&name,0,len(&name))) { valid=false; } }
            }
        }return valid;
    }
    fn ini_text(out: &mut Buffer<U8>, name: Slice<U8>, value: Slice<U8>) -> Unit {
        bytes_append(out,name);push(out,U8(61));push(out,U8(34));
        for i in 0..len(value) { if value[i] == U8(34) || value[i] == U8(92) { push(out,U8(92)); }if value[i] != U8(10) && value[i] != U8(13) { push(out,value[i]); } }
        bytes_append(out,utf8_bytes("\"\r\n"));
    }
    fn ini_bool(out: &mut Buffer<U8>, name: Slice<U8>, value: Bool) -> Unit {
        if value { ini_text(out,name,utf8_bytes("true")); }else { ini_text(out,name,utf8_bytes("false")); }
    }
    fn ini_number(out: &mut Buffer<U8>, name: Slice<U8>, value: U32) -> Unit { let mut b: Buffer<U8>=buffer<U8>(0);append_number(&mut b,cast<U64>(value));ini_text(out,name,slice(&b,0,len(&b))); }
    fn save_settings(o: &Options) -> Buffer<U8> {
        let mut b: Buffer<U8>=bytes_copy(utf8_bytes("[NoDPI]\r\n"));let mut mode: Utf8="Default";if o.all_ports { mode="AllPorts"; }if o.game { mode="GameMode"; }
        ini_text(&mut b,utf8_bytes("Mode"),utf8_bytes(mode));
        ini_bool(&mut b,utf8_bytes("CloudCdnEnabled"),o.cloud_cdn);ini_bool(&mut b,utf8_bytes("TelegramEnabled"),o.telegram);ini_bool(&mut b,utf8_bytes("SafeAutomatic"),o.safe_automatic);ini_bool(&mut b,utf8_bytes("AdaptiveStrategies"),o.auto_strategy);ini_bool(&mut b,utf8_bytes("ServerWindowCandidates"),o.server_window);
        ini_bool(&mut b,utf8_bytes("UdpObfuscate"),o.udp_obfuscate);ini_bool(&mut b,utf8_bytes("DiagnosticLog"),o.diagnostic);
        ini_number(&mut b,utf8_bytes("HandshakeTimeoutMs"),o.handshake_timeout);
        if o.forced_arm == U32(8) { ini_text(&mut b,utf8_bytes("StrategyProfile"),utf8_bytes("auto")); }else { ini_number(&mut b,utf8_bytes("StrategyProfile"),o.forced_arm); }
        ini_bool(&mut b,utf8_bytes("AggressiveFrag"),o.aggressive);ini_bool(&mut b,utf8_bytes("FragBySNI"),o.sni);ini_bool(&mut b,utf8_bytes("ApplyECH"),o.ech);
        ini_bool(&mut b,utf8_bytes("AdaptiveRetry"),o.adaptive);ini_bool(&mut b,utf8_bytes("LearnStrategy"),o.learn);ini_bool(&mut b,utf8_bytes("FakePackets"),o.fake);
        ini_bool(&mut b,utf8_bytes("HttpManip"),o.http);ini_bool(&mut b,utf8_bytes("ReverseFrag"),o.reverse);ini_bool(&mut b,utf8_bytes("AwsWhitelistOnly"),o.whitelist);
        ini_bool(&mut b,utf8_bytes("Debug"),o.debug > U32(0));ini_bool(&mut b,utf8_bytes("DebugVerbose"),o.debug > U32(1));
        let mut quic: Utf8="pass";if o.quic == U32(1) { quic="auto"; }if o.quic == U32(2) { quic="drop"; }if o.quic == U32(3) { quic="ipfrag"; }
        ini_text(&mut b,utf8_bytes("QuicMode"),utf8_bytes(quic));let mut split: Utf8="tcp";if o.split_mode == U32(1) { split="ip"; }if o.split_mode == U32(2) { split="both"; }
        ini_text(&mut b,utf8_bytes("SplitMode"),utf8_bytes(split));let mut strategy: Utf8="semantic";if o.strategy == U32(1) { strategy="semantic-multi"; }if o.strategy == U32(2) { strategy="legacy"; }
        ini_text(&mut b,utf8_bytes("SplitStrategy"),utf8_bytes(strategy));let mut fake: Utf8="tls-badseq";if o.fake_mode == U32(1) { fake="rst-ttl"; }ini_text(&mut b,utf8_bytes("FakeMode"),utf8_bytes(fake));
        ini_number(&mut b,utf8_bytes("FirstPackets"),o.first_packets);ini_number(&mut b,utf8_bytes("HandshakeJitterMs"),o.jitter_ms);
        ini_number(&mut b,utf8_bytes("QuicProbeMs"),o.probe_ms);ini_number(&mut b,utf8_bytes("MinTTL"),o.min_hops);
        let mut ttl: Buffer<U8>=buffer<U8>(0);if o.ttl_margin != U32(0) || o.ttl_spread != U32(0) || o.ttl_max != U32(0) {
            append_number(&mut ttl,cast<U64>(o.ttl_margin));push(&mut ttl,U8(44));append_number(&mut ttl,cast<U64>(o.ttl_spread));push(&mut ttl,U8(44));append_number(&mut ttl,cast<U64>(o.ttl_max));
        }ini_text(&mut b,utf8_bytes("AutoTTL"),slice(&ttl,0,len(&ttl)));
        ini_text(&mut b,utf8_bytes("Hostlist"),slice(&o.paths[0],0,len(&o.paths[0])));ini_text(&mut b,utf8_bytes("HostlistExclude"),slice(&o.paths[1],0,len(&o.paths[1])));
        ini_text(&mut b,utf8_bytes("IPSet"),slice(&o.paths[2],0,len(&o.paths[2])));ini_text(&mut b,utf8_bytes("IPSetExclude"),slice(&o.paths[3],0,len(&o.paths[3])));
        ini_text(&mut b,utf8_bytes("AwsJson"),slice(&o.paths[4],0,len(&o.paths[4])));ini_text(&mut b,utf8_bytes("AwsServices"),slice(&o.paths[5],0,len(&o.paths[5])));return move b;
    }
}

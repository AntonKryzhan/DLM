module dpi.options
import dpi.genetics

native {
    // Mode values are stable in settings and command line conversions:
    // QUIC pass=0, auto=1, drop=2, ipfrag=3; split tcp=0, ip=1, both=2;
    // strategy semantic=0, semantic-multi=1, legacy=2; fake badseq=0, rst-ttl=1.
    record Options repr(C,version=1) {
        aggressive: Bool; fake: Bool; http: Bool; sni: Bool; reverse: Bool; ech: Bool;
        adaptive: Bool; learn: Bool; all_ports: Bool; game: Bool; whitelist: Bool;
        quic: U32; probe_ms: U32; fake_mode: U32; split_mode: U32; strategy: U32;
        first_packets: U32; jitter_ms: U32; ttl_margin: U32; ttl_spread: U32; ttl_max: U32; min_hops: U32;
        debug: U32; help: Bool; error: Bool;
        auto_strategy: Bool; forced_arm: U32; server_window: Bool; udp_obfuscate: Bool; diagnostic: Bool; handshake_timeout: U32;
        safe_automatic: Bool; telegram: Bool; cloud_cdn: Bool; driver_check: Bool; driver_report: Buffer<U8>; parent_pid: U32; gene_mode: Bool; gene: Gene; gene_target: U32; trial_tag: Buffer<U8>;
        paths: Buffer<Buffer<U8>>;
    }
    fn default_options() -> Options {
        let mut o: Options=zero<Options>();
        o.cloud_cdn=true;o.telegram=true;o.gene=gene_default();o.auto_strategy=true;o.forced_arm=8;o.server_window=true;o.diagnostic=true;o.handshake_timeout=6000;
        o.aggressive=true;o.sni=true;o.ech=true;o.adaptive=true;o.learn=true;o.probe_ms=750;o.first_packets=3;
        for i in 0..8 { push(&mut o.paths,buffer<U8>(0)); }return move o;
    }
    fn auto_ttl(o: &mut Options, value: Slice<U8>) -> Bool {
        let a: USize=byte_find(value,0,U8(44));if a == len(value) { return false; }
        let b: USize=byte_find(value,a+1,U8(44));if b == len(value) { return false; }
        let mut x: U32=0;let mut y: U32=0;let mut z: U32=0;
        if !decimal(slice(&value,0,a),255,&mut x) || !decimal(slice(&value,a+1,b-a-1),255,&mut y) ||
            !decimal(slice(&value,b+1,len(value)-b-1),255,&mut z) { return false; }
        o.ttl_margin=x;o.ttl_spread=y;o.ttl_max=z;return true;
    }
    fn option(o: &mut Options, arg: Slice<U8>) -> Bool {
        let eq: USize=byte_find(arg,0,U8(61));let name: Slice<U8>=slice(&arg,0,eq);
        if eq == len(arg) {
            if bytes_equal(name,utf8_bytes("--driver-check")) { o.driver_check=true; }
            else if bytes_equal(name,utf8_bytes("--cloud-cdn")) { o.cloud_cdn=true; }
            else if bytes_equal(name,utf8_bytes("--no-cloud-cdn")) { o.cloud_cdn=false; }
            else if bytes_equal(name,utf8_bytes("--telegram")) { o.telegram=true; }
            else if bytes_equal(name,utf8_bytes("--no-telegram")) { o.telegram=false; }
            else if bytes_equal(name,utf8_bytes("--automatic")) { o.safe_automatic=true; }
            else if bytes_equal(name,utf8_bytes("--no-automatic")) { o.safe_automatic=false; }
            else if bytes_equal(name,utf8_bytes("--adaptive-strategies")) { o.auto_strategy=true; }
            else if bytes_equal(name,utf8_bytes("--no-adaptive-strategies")) { o.auto_strategy=false; }
            else if bytes_equal(name,utf8_bytes("--server-window")) { o.server_window=true; }
            else if bytes_equal(name,utf8_bytes("--no-server-window")) { o.server_window=false; }
            else if bytes_equal(name,utf8_bytes("--udp-obfuscate")) { o.udp_obfuscate=true; }
            else if bytes_equal(name,utf8_bytes("--diagnostic-log")) { o.diagnostic=true; }
            else if bytes_equal(name,utf8_bytes("--no-diagnostic-log")) { o.diagnostic=false; }
            else if bytes_equal(name,utf8_bytes("--aggressive-frag")) { o.aggressive=true; }
            else if bytes_equal(name,utf8_bytes("--no-aggressive-frag")) { o.aggressive=false; }
            else if bytes_equal(name,utf8_bytes("--frag-by-sni")) { o.sni=true; }
            else if bytes_equal(name,utf8_bytes("--no-frag-by-sni")) { o.sni=false; }
            else if bytes_equal(name,utf8_bytes("--apply-ech")) { o.ech=true; }
            else if bytes_equal(name,utf8_bytes("--no-apply-ech")) { o.ech=false; }
            else if bytes_equal(name,utf8_bytes("--adaptive-retry")) { o.adaptive=true; }
            else if bytes_equal(name,utf8_bytes("--no-adaptive-retry")) { o.adaptive=false; }
            else if bytes_equal(name,utf8_bytes("--learn-strategy")) { o.learn=true; }
            else if bytes_equal(name,utf8_bytes("--no-learn-strategy")) { o.learn=false; }
            else if bytes_equal(name,utf8_bytes("--fake-packets")) { o.fake=true; }
            else if bytes_equal(name,utf8_bytes("--http-manip")) { o.http=true; }
            else if bytes_equal(name,utf8_bytes("--reverse-frag")) { o.reverse=true; }
            else if bytes_equal(name,utf8_bytes("--block-quic")) { o.quic=2; }
            else if bytes_equal(name,utf8_bytes("--all-ports")) { o.all_ports=true; }
            else if bytes_equal(name,utf8_bytes("--game-mode")) { o.game=true; }
            else if bytes_equal(name,utf8_bytes("--debug")) { if o.debug < U32(1) { o.debug=1; } }
            else if bytes_equal(name,utf8_bytes("--debug-verbose")) { o.debug=2; }
            else if bytes_equal(name,utf8_bytes("--whitelist-only")) { o.whitelist=true; }
            else if bytes_equal(name,utf8_bytes("--auto-ttl")) { o.ttl_margin=2;o.ttl_spread=4;o.ttl_max=16; }
            else if bytes_equal(name,utf8_bytes("--help")) || bytes_equal(name,utf8_bytes("-h")) { o.help=true; }
            else { return false; } return true;
        }
        let value: Slice<U8>=slice(&arg,eq+1,len(arg)-eq-1);
        if bytes_equal(name,utf8_bytes("--driver-report")) { o.driver_report=bytes_copy(value);return true; }
        else if bytes_equal(name,utf8_bytes("--trial-tag")) { if len(value) > 64 { return false; }o.trial_tag=bytes_copy(value);return true; }
        else if bytes_equal(name,utf8_bytes("--gene")) { o.gene_mode=gene_parse(value,&mut o.gene);return o.gene_mode; }
        else if bytes_equal(name,utf8_bytes("--gene-target")) { return decimal(value,6,&mut o.gene_target); }
        else if bytes_equal(name,utf8_bytes("--parent-pid")) { return decimal(value,4294967295,&mut o.parent_pid); }
        else if bytes_equal(name,utf8_bytes("--strategy-profile")) {
            if ascii_equal(value,utf8_bytes("auto")) { o.forced_arm=8;o.auto_strategy=true;return true; }
            return decimal(value,7,&mut o.forced_arm);
        } else if bytes_equal(name,utf8_bytes("--handshake-timeout-ms")) { return decimal(value,30000,&mut o.handshake_timeout) && o.handshake_timeout >= U32(1000); }
        else if bytes_equal(name,utf8_bytes("--quic-mode")) {
            if ascii_at(value,0,utf8_bytes("pass")) && len(value) == 4 { o.quic=0; }
            else if ascii_at(value,0,utf8_bytes("auto")) && len(value) == 4 { o.quic=1; }
            else if ascii_at(value,0,utf8_bytes("drop")) && len(value) == 4 { o.quic=2; }
            else if ascii_at(value,0,utf8_bytes("ipfrag")) && len(value) == 6 { o.quic=3; } else { return false; }
        } else if bytes_equal(name,utf8_bytes("--split-mode")) {
            if ascii_equal(value,utf8_bytes("tcp")) { o.split_mode=0; } else if ascii_equal(value,utf8_bytes("ip")) { o.split_mode=1; }
            else if ascii_equal(value,utf8_bytes("both")) { o.split_mode=2; } else { return false; }
        } else if bytes_equal(name,utf8_bytes("--split-strategy")) {
            if ascii_equal(value,utf8_bytes("semantic")) { o.strategy=0; } else if ascii_equal(value,utf8_bytes("semantic-multi")) { o.strategy=1; }
            else if ascii_equal(value,utf8_bytes("legacy")) { o.strategy=2; } else { return false; }
        } else if bytes_equal(name,utf8_bytes("--fake-mode")) {
            if ascii_equal(value,utf8_bytes("tls-badseq")) { o.fake_mode=0; } else if ascii_equal(value,utf8_bytes("rst-ttl")) { o.fake_mode=1; } else { return false; }
        } else if bytes_equal(name,utf8_bytes("--first-packets")) { return decimal(value,1024,&mut o.first_packets); }
        else if bytes_equal(name,utf8_bytes("--handshake-jitter-ms")) { return decimal(value,250,&mut o.jitter_ms); }
        else if bytes_equal(name,utf8_bytes("--quic-probe-ms")) { return decimal(value,5000,&mut o.probe_ms) && o.probe_ms >= U32(200); }
        else if bytes_equal(name,utf8_bytes("--min-ttl")) { return decimal(value,255,&mut o.min_hops); }
        else if bytes_equal(name,utf8_bytes("--auto-ttl")) { return auto_ttl(o,value); }
        else {
            let mut index: USize=8;
            if bytes_equal(name,utf8_bytes("--hostlist")) { index=0; }
            else if bytes_equal(name,utf8_bytes("--blacklist")) { index=6; }
            else if bytes_equal(name,utf8_bytes("--hostlist-exclude")) { index=1; }
            else if bytes_equal(name,utf8_bytes("--ipset")) { index=2; }
            else if bytes_equal(name,utf8_bytes("--ipset-exclude")) { index=3; }
            else if bytes_equal(name,utf8_bytes("--aws-json")) { index=4; }
            else if bytes_equal(name,utf8_bytes("--aws-services")) { index=5; }
            else if bytes_equal(name,utf8_bytes("--strategy-file")) { index=7; }
            if index == 8 { return false; }o.paths[index]=bytes_copy(value);
        }
        return true;
    }
    fn automatic_options(o: &mut Options) -> Unit {
        o.safe_automatic=true;o.auto_strategy=true;o.forced_arm=8;o.server_window=false;o.udp_obfuscate=false;o.quic=1;
        o.aggressive=true;o.sni=true;o.ech=true;o.adaptive=true;o.learn=true;o.fake=false;o.http=false;o.reverse=false;
        o.all_ports=false;o.game=false;o.whitelist=false;o.split_mode=0;o.strategy=0;o.first_packets=3;o.jitter_ms=0;
        o.ttl_margin=0;o.ttl_spread=0;o.ttl_max=0;o.min_hops=0;o.diagnostic=true;o.debug=1;o.handshake_timeout=6000;if o.gene_mode { o.adaptive=false; }
    }
    fn finish_options(o: &mut Options) -> Unit {
        if o.safe_automatic { automatic_options(o); }
        if len(&o.paths[0]) == 0 && len(&o.paths[6]) > 0 { let fallback: Buffer<U8>=bytes_copy(slice(&o.paths[6],0,len(&o.paths[6])));o.paths[0]=move fallback; }
        if o.game { o.quic=0;o.http=false; } }
}

module dpi.automatic
import dpi.adaptive

native {
    record AutomaticHost repr(C) { host: Host; used: Bool; touched: U64; direct_failures: U32; failures: U32; learned_until: U64; pause_until: U64; }
    fn host_suffix(h: &Host, suffix: Slice<U8>) -> Bool {
        if h.length < len(suffix) { return false; }let at: USize=h.length-len(suffix);
        return (at == 0 || h.bytes[at-1] == U8(46)) && ascii_equal(slice(&h.bytes,at,len(suffix)),suffix);
    }
    fn automatic_protected(h: &Host) -> Bool {
        return host_suffix(h,utf8_bytes("cloudflare-dns.com")) || host_suffix(h,utf8_bytes("cloudflare-gateway.com")) ||
            host_suffix(h,utf8_bytes("dns.google")) || host_suffix(h,utf8_bytes("dns.google.com")) ||
            host_suffix(h,utf8_bytes("quad9.net")) || host_suffix(h,utf8_bytes("doh.pub")) ||
            host_suffix(h,utf8_bytes("dns.alidns.com")) || host_suffix(h,utf8_bytes("dns.nextdns.io")) ||
            host_suffix(h,utf8_bytes("dns.adguard.com")) || host_suffix(h,utf8_bytes("dns.adguard-dns.com")) ||
            host_suffix(h,utf8_bytes("doh.opendns.com")) || host_suffix(h,utf8_bytes("doh.cleanbrowsing.org")) ||
            host_suffix(h,utf8_bytes("dns.yandex.net"));
    }
    fn telegram_host(h: &Host) -> Bool {
        return host_suffix(h,utf8_bytes("telegram.org")) || host_suffix(h,utf8_bytes("t.me")) || host_suffix(h,utf8_bytes("telegram.me")) || host_suffix(h,utf8_bytes("telegram-cdn.org"));
    }
    fn mwo_host(h: &Host) -> Bool { return host_suffix(h,utf8_bytes("mwomercs.com")); }
    fn cloud_host_family(h: &Host) -> U32 {
        if host_suffix(h,utf8_bytes("cloudfront.net")) { return 2; }
        if host_suffix(h,utf8_bytes("azurefd.net")) || host_suffix(h,utf8_bytes("azureedge.net")) || host_suffix(h,utf8_bytes("vo.msecnd.net")) { return 3; }
        if host_suffix(h,utf8_bytes("azurewebsites.net")) || host_suffix(h,utf8_bytes("blob.core.windows.net")) || host_suffix(h,utf8_bytes("web.core.windows.net")) || host_suffix(h,utf8_bytes("cloudapp.net")) || host_suffix(h,utf8_bytes("cloudapp.azure.com")) || host_suffix(h,utf8_bytes("trafficmanager.net")) { return 4; }return 0;
    }
    fn target_mwo(target: U32) -> Bool { return target == U32(5) || target == U32(6); }
    fn target_youtube(target: U32) -> Bool { return target == U32(0) || target == U32(2) || target == U32(4) || target == U32(6); }
    fn target_discord(target: U32) -> Bool { return target == U32(1) || target == U32(2) || target == U32(4) || target == U32(6); }
    fn target_telegram(target: U32) -> Bool { return target == U32(3) || target == U32(4) || target == U32(6); }
    fn gene_applies(h: &Host, target: U32) -> Bool {
        let youtube: Bool=host_suffix(h,utf8_bytes("youtube.com")) || host_suffix(h,utf8_bytes("youtu.be")) || host_suffix(h,utf8_bytes("googlevideo.com")) || host_suffix(h,utf8_bytes("ytimg.com")) || host_suffix(h,utf8_bytes("youtubei.googleapis.com")) || host_suffix(h,utf8_bytes("ggpht.com")) || host_suffix(h,utf8_bytes("youtube-nocookie.com"));
        let discord: Bool=host_suffix(h,utf8_bytes("discord.com")) || host_suffix(h,utf8_bytes("discord.gg")) || host_suffix(h,utf8_bytes("discordapp.com")) || host_suffix(h,utf8_bytes("discordapp.net")) || host_suffix(h,utf8_bytes("discord.media"));
        return (target_youtube(target) && youtube) || (target_discord(target) && discord) || (target_telegram(target) && telegram_host(h)) || (target_mwo(target) && (mwo_host(h) || cloud_host_family(h) > U32(0)));
    }
    fn automatic_known(h: &Host) -> Bool {
        return mwo_host(h) || telegram_host(h) || host_suffix(h,utf8_bytes("youtube.com")) || host_suffix(h,utf8_bytes("youtu.be")) ||
            host_suffix(h,utf8_bytes("googlevideo.com")) || host_suffix(h,utf8_bytes("ytimg.com")) ||
            host_suffix(h,utf8_bytes("youtubei.googleapis.com")) || host_suffix(h,utf8_bytes("youtube-nocookie.com")) ||
            host_suffix(h,utf8_bytes("ggpht.com")) || host_suffix(h,utf8_bytes("discord.com")) ||
            host_suffix(h,utf8_bytes("discord.gg")) || host_suffix(h,utf8_bytes("discordapp.com")) ||
            host_suffix(h,utf8_bytes("discordapp.net")) || host_suffix(h,utf8_bytes("discord.media"));
    }
    fn automatic_public(address_: &Array<U8,16>, version: U8) -> Bool {
        if version == U8(4) {
            let a: U8=address_[0];let b: U8=address_[1];
            return a != U8(0) && a != U8(10) && a != U8(127) && a < U8(224) &&
                !(a == U8(169) && b == U8(254)) && !(a == U8(172) && b >= U8(16) && b <= U8(31)) &&
                !(a == U8(192) && b == U8(168)) && !(a == U8(100) && b >= U8(64) && b <= U8(127));
        }
        if version != U8(6) || (address_[0] & U8(0xfe)) == U8(0xfc) || address_[0] == U8(0xff) ||
            (address_[0] == U8(0xfe) && (address_[1] & U8(0xc0)) == U8(0x80)) { return false; }
        let mut mapped: Bool=true;for i in 0..10 { mapped=mapped && address_[i] == U8(0); }
        if mapped && address_[10] == U8(255) && address_[11] == U8(255) { let mut v4: Array<U8,16>=zero<Array<U8,16>>();for i in 0..4 { v4[i]=address_[12+i]; }return automatic_public(&v4,4); }
        let mut nonzero: Bool=false;for i in 0..15 { nonzero=nonzero || address_[i] != U8(0); }
        return nonzero;
    }
    fn automatic_slot(items: &mut Buffer<AutomaticHost>, host: &Host, now: U64) -> USize {
        let base: USize=cast<USize>(host_hash(host) & U64(255))*4;let mut choice: USize=base;let mut oldest: U64=18446744073709551615;
        for way in 0..4 { let i: USize=base+way;if items[i].used && host_equal(&items[i].host,host) { items[i].touched=now;return i; }
            if !items[i].used || items[i].touched < oldest { oldest=items[i].touched;choice=i; } }
        items[choice]=zero<AutomaticHost>();items[choice].used=true;items[choice].host=*host;items[choice].touched=now;return choice;
    }
    fn automatic_candidate(items: &mut Buffer<AutomaticHost>, host: &Host, policy: &Policy, now: U64) -> Bool {
        if host.length == 0 || automatic_protected(host) { return false; }let i: USize=automatic_slot(items,host,now);
        if items[i].pause_until > now { return false; }
        return automatic_known(host) || domain_matches(&policy.hosts,host) || items[i].learned_until > now;
    }
    fn automatic_feedback(items: &mut Buffer<AutomaticHost>, host: &Host, arm: U32, result: U32, reason: U32, now: U64) -> Unit {
        if host.length == 0 || automatic_protected(host) || result == U32(3) { return; }let i: USize=automatic_slot(items,host,now);
        if result == U32(1) { items[i].failures=0;items[i].direct_failures=0;if arm == U32(0) { items[i].learned_until=0; }else { items[i].learned_until=now+U64(600000); }return; }
        if result != U32(2) { return; }
        if arm == U32(0) {
            // No SYN-ACK, client cancellation and HTTP application failures do not
            // promote a host. Require two independent TLS-response failures.
            if reason != U32(1) && reason != U32(7) { return; }
            items[i].direct_failures=saturating_add(items[i].direct_failures,U32(1));
            if items[i].direct_failures >= U32(2) { items[i].learned_until=now+U64(600000); }
        }else {
            items[i].failures=saturating_add(items[i].failures,U32(1));
            if items[i].failures >= U32(6) { items[i].pause_until=now+U64(120000);items[i].failures=0; }
        }
    }
}

// TCP and minimal HTTP/1.0 (v0.126, docs/OS.md): client requests and a sequential server
// driven by a typed unary handler (Fn or explicit FnEffect). No TLS.
module std.net
import std.process

theory StdNet extends StdProcess {
    record HttpResponse(status: Nat, headers: List<Text>, body: Text)

    // position of `pat` in `t` at or after `i` (`left` = remaining positions)
    fn find_text(t: Text, pat: Text, i: Nat, left: Nat) -> Option<Nat> decreasing left =
        if_then_else(nat_eq(left, 0), none(Nat), if_then_else(text_eq(substring(t, i, text_length(pat)), pat), some(i), find_text(t, pat, i + 1, pred(left))))
    fn index_of(t: Text, pat: Text) -> Option<Nat> = find_text(t, pat, 0, text_length(t) + 1)
    private fn nt_strip_cr(l: Text) -> Text = if_then_else(and(text_length(l) > 0, text_eq(substring(l, pred(text_length(l)), 1), "\r")), substring(l, 0, pred(text_length(l))), l)
    // `HTTP/1.x <status> ...` head, blank line, body
    fn parse_http(t: Text) -> Result<HttpResponse, Text> =
        match index_of(t, "\r\n\r\n") { none => err("malformed HTTP response", HttpResponse), some(k) =>
            let head = substring(t, 0, k) in let body = substring(t, k + 4, monus(text_length(t), k + 4)) in
            let hs = map(lines(head), fun(l: Text, nt_strip_cr(l))) in
            let status_line = words(unwrap_or(index(hs, 0), "")) in
            let version = unwrap_or(index(status_line, 0), "") in
            if_then_else(not(or(text_eq(version, "HTTP/1.0"), text_eq(version, "HTTP/1.1"))), err("malformed HTTP response", HttpResponse),
                match index(status_line, 1) { none => err("malformed HTTP response", HttpResponse), some(code) =>
                    match text_to_nat(code) { none => err("malformed HTTP status", HttpResponse), some(s) =>
                        if_then_else(or(not(nat_eq(text_length(code), 3)), or(100 > s, s > 599)), err("malformed HTTP status", HttpResponse), ok(HttpResponse(s, tail(hs), body), Text)) } }) }
    fn http_request(host: Text, port: Nat, request: Text, timeout_ms: Nat) -> Result<Text, Text> =
        match net_connect(host, port, timeout_ms) { err(e) => err(e, Text), ok(c) =>
            match net_send(c, request) { err(e) => let x = net_close(c) in err(e, Text), ok(n) =>
                let s = net_shutdown(c) in let r = net_recv_all(c) in let x = net_close(c) in r } }
    fn http_get(host: Text, port: Nat, path: Text, timeout_ms: Nat) -> Result<HttpResponse, Text> =
        match http_request(host, port, concat(concat(concat(concat("GET ", path), " HTTP/1.0\r\nHost: "), host), "\r\nConnection: close\r\n\r\n"), timeout_ms) {
            err(e) => err(e, HttpResponse), ok(t) => parse_http(t) }
    private fn nt_skip_headers(c: Nat) -> Result<Nat, Text> loop =
        match net_recv_line(c) { err(e) => err(e, Nat), ok(o) => match o { none => err("unexpected end of HTTP headers", Nat), some(l) => if_then_else(nat_eq(text_length(l), 0), ok(0, Text), nt_skip_headers(c)) } }
    // serves one request on listener `l`: the response body is `handler(path)`; the path
    fn http_serve_one<F: UnaryCallable>(l: Nat, timeout_ms: Nat, handler: F) -> Result<Text, Text> =
        match net_accept(l, timeout_ms) { err(e) => err(e, Text), ok(c) =>
            match net_recv_line(c) { err(e) => let x = net_close(c) in err(e, Text), ok(first) =>
                match first { none => let x = net_close(c) in err("empty HTTP request", Text), some(line) =>
                    match index(words(line), 1) { none => let x = net_close(c) in err("malformed HTTP request", Text), some(path) =>
                        match nt_skip_headers(c) { err(e) => let x = net_close(c) in err(e, Text), ok(h) =>
                            match net_send(c, concat("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n", handler(path))) {
                                err(e) => let x = net_close(c) in err(e, Text), ok(w) => let x = net_close(c) in ok(path, Text) } } } } } }
    // serves `count` requests one after another; the paths served, newest first
    fn http_serve<F: UnaryCallable>(l: Nat, count: Nat, timeout_ms: Nat, handler: F, served: List<Text>) -> Result<List<Text>, Text> loop =
        if_then_else(nat_eq(count, 0), ok(served, Text), match http_serve_one(l, timeout_ms, handler) {
            err(e) => err(e, List(Text)), ok(p) => http_serve(l, pred(count), timeout_ms, handler, cons(p, served)) })

    theorem finds_header_end: equal(index_of("ab\r\n\r\ncd", "\r\n\r\n"), some(2))
}

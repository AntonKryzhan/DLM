module dpi.bytes

native {
    fn min_size(a: USize, b: USize) -> USize guarantees(NoAlloc, NoBlocking, NoIO, NoUnwind, BoundedWork, ThreadSafe) {
        if a < b { return a; } return b;
    }
    fn max_size(a: USize, b: USize) -> USize guarantees(NoAlloc, NoBlocking, NoIO, NoUnwind, BoundedWork, ThreadSafe) {
        if a > b { return a; } return b;
    }
    fn ascii_lower(c: U8) -> U8 guarantees(NoAlloc, NoBlocking, NoIO) {
        if c >= U8(65) && c <= U8(90) { return c + U8(32); } return c;
    }
    fn space(c: U8) -> Bool { return c == U8(32) || c == U8(9) || c == U8(10) || c == U8(13); }
    fn bytes_at(b: Slice<U8>, at: USize, text: Slice<U8>) -> Bool guarantees(NoAlloc, NoBlocking, NoIO) {
        if at > len(b) || len(text) > len(b) - at { return false; }
        for i in 0..len(text) { if b[at + i] != text[i] { return false; } } return true;
    }
    fn bytes_equal(a: Slice<U8>, b: Slice<U8>) -> Bool guarantees(NoAlloc, NoBlocking, NoIO) {
        return len(a) == len(b) && bytes_at(a, 0, b);
    }
    fn ascii_at(b: Slice<U8>, at: USize, text: Slice<U8>) -> Bool {
        if at > len(b) || len(text) > len(b) - at { return false; }
        for i in 0..len(text) { if ascii_lower(b[at+i]) != ascii_lower(text[i]) { return false; } } return true;
    }
    fn ascii_equal(a: Slice<U8>, b: Slice<U8>) -> Bool { return len(a) == len(b) && ascii_at(a,0,b); }
    fn bytes_find(b: Slice<U8>, start: USize, text: Slice<U8>) -> USize {
        let mut i: USize = start;
        while i < len(b) { if bytes_at(b,i,text) { return i; } i=i+1; } return len(b);
    }
    fn byte_find(b: Slice<U8>, start: USize, c: U8) -> USize {
        let mut i: USize = start; while i < len(b) { if b[i] == c { return i; } i=i+1; } return len(b);
    }
    fn trimmed(b: Slice<U8>) -> Slice<U8> borrows(b) {
        let mut a: USize=0;let mut z: USize=len(b);
        while a < z && space(b[a]) { a=a+1; } while z > a && space(b[z-1]) { z=z-1; }
        return slice(&b,a,z-a);
    }
    fn bytes_copy(b: Slice<U8>) -> Buffer<U8> {
        let mut result: Buffer<U8>=buffer<U8>(len(b));for i in 0..len(b) { result[i]=b[i]; } return move result;
    }
    fn bytes_append(b: &mut Buffer<U8>, text: Slice<U8>) -> Unit {
        let old: USize=len(b);resize(b,old+len(text));for i in 0..len(text) { b[old+i]=text[i]; }
    }
    fn decimal(b: Slice<U8>, limit: U32, value: &mut U32) -> Bool {
        if len(b) == 0 { return false; } let mut n: U32=0;
        for i in 0..len(b) {
            let c: U8=b[i];if c < U8(48) || c > U8(57) { return false; }
            let d: U32=cast<U32>(c)-U32(48);
            if n > limit/U32(10) || (n == limit/U32(10) && d > limit%U32(10)) { return false; }
            n=n*U32(10)+d;
        } *value=n;return true;
    }
    fn append_number(b: &mut Buffer<U8>, number: U64) -> Unit {
        let mut digits: Array<U8,20>=zero<Array<U8,20>>();let mut n: U64=number;let mut count: USize=0;
        while true { digits[count]=cast<U8>(n%U64(10))+U8(48);count=count+1;n=n/U64(10);if n == U64(0) { break; } }
        while count > 0 { count=count-1;push(b,digits[count]); }
    }
    fn zbytes(text: Slice<U8>) -> Buffer<U8> { let mut b: Buffer<U8>=bytes_copy(text);push(&mut b,U8(0));return move b; }
    fn be16(b: Slice<U8>, p: USize) -> U16 { return read_be<U16>(&b,p); }
    fn be32(b: Slice<U8>, p: USize) -> U32 { return read_be<U32>(&b,p); }
    record Host repr(C) { bytes: Array<U8,254>; length: USize; }
    fn domain(b: Slice<U8>, wildcard: Bool) -> Host {
        let mut result: Host=zero<Host>();let mut a: USize=0;let mut z: USize=len(b);
        if wildcard && bytes_at(b,0,utf8_bytes("*.")) { a=2; }
        if wildcard && a < z && b[a] == U8(46) { a=a+1; }
        if a < z && b[z-1] == U8(46) { z=z-1; }
        if z <= a || z-a > 253 { return result; }
        let mut label: USize=0;let mut previous: U8=0;
        for i in a..z {
            let c: U8=ascii_lower(b[i]);
            if c == U8(46) { if label == 0 || previous == U8(45) { return zero<Host>(); } label=0; }
            else {
                if !((c >= U8(97) && c <= U8(122)) || (c >= U8(48) && c <= U8(57)) || c == U8(45)) { return zero<Host>(); }
                if label == 0 && c == U8(45) { return zero<Host>(); } label=label+1;if label > 63 { return zero<Host>(); }
            }
            result.bytes[i-a]=c;previous=c;
        }
        if label == 0 || previous == U8(45) { return zero<Host>(); } result.length=z-a;return result;
    }
    fn host_hash(h: &Host) -> U64 {
        let mut value: U64=14695981039346656037;
        for i in 0..h.length { value=wrapping_mul(value ^ cast<U64>(h.bytes[i]),U64(1099511628211)); }
        value=value ^ (value >> U64(33));value=wrapping_mul(value,U64(0xff51afd7ed558ccd));return value ^ (value >> U64(33));
    }
    fn host_equal(a: &Host, b: &Host) -> Bool {
        if a.length != b.length { return false; } for i in 0..a.length { if a.bytes[i] != b.bytes[i] { return false; } } return true;
    }
    fn middle_label(h: &Host) -> USize {
        let mut end: USize=h.length;let mut begin: USize=0;let mut last: USize=h.length;
        for i in 0..h.length { if h.bytes[i] == U8(46) { last=i; } }
        if last == h.length || last == 0 { return h.length/2; } end=last;
        for i in 0..end { if h.bytes[i] == U8(46) { begin=i+1; } }
        if h.length-last-1 == 2 && end-begin <= 3 && begin > 0 {
            end=begin-1;begin=0;for i in 0..end { if h.bytes[i] == U8(46) { begin=i+1; } }
        }
        return begin+(end-begin)/2;
    }
}

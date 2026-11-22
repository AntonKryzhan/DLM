module std.native_xml
import std.native_binary

native {
    // UTF-8 XML 1.0, without DTDs or external entities. Token offsets refer
    // to owned source bytes, so unknown elements and attributes survive edits.
    // Kinds: start=1,end=2,attribute=3,text=4,comment=5,PI=6,CDATA=7,declaration=8,empty=9.
    record XmlToken repr(C) {
        kind: U32; start: USize; end: USize;
        name_start: USize; name_length: USize;
        value_start: USize; value_length: USize; depth: USize;
    }
    record XmlDocument repr(C) {
        bytes: Buffer<U8>; tokens: Buffer<XmlToken>; valid: Bool; error_offset: USize;
    }
    fn xml_space(c: U8) -> Bool { return c == U8(32) || c == U8(9) || c == U8(10) || c == U8(13); }
    fn xml_char(c: U32) -> Bool {
        return c == U32(9) || c == U32(10) || c == U32(13) ||
            (c >= U32(32) && c <= U32(55295)) || (c >= U32(57344) && c <= U32(65533)) ||
            (c >= U32(65536) && c <= U32(1114111));
    }
    fn xml_name_start(c: U32) -> Bool {
        return c == U32(58) || c == U32(95) || (c >= U32(65) && c <= U32(90)) ||
            (c >= U32(97) && c <= U32(122)) || (c >= U32(192) && c <= U32(214)) ||
            (c >= U32(216) && c <= U32(246)) || (c >= U32(248) && c <= U32(767)) ||
            (c >= U32(880) && c <= U32(893)) || (c >= U32(895) && c <= U32(8191)) ||
            (c >= U32(8204) && c <= U32(8205)) || (c >= U32(8304) && c <= U32(8591)) ||
            (c >= U32(11264) && c <= U32(12271)) || (c >= U32(12289) && c <= U32(55295)) ||
            (c >= U32(63744) && c <= U32(64975)) || (c >= U32(65008) && c <= U32(65533)) ||
            (c >= U32(65536) && c <= U32(983039));
    }
    fn xml_name_end(b: Slice<U8>, start: USize) -> USize {
        let first: Utf8Step = utf8_next(b, start);
        if !first.valid || !xml_name_start(first.codepoint) { return start; }
        let mut p: USize = first.next;
        while p < len(b) {
            let s: Utf8Step = utf8_next(b, p); let c: U32 = s.codepoint;
            if !s.valid { return p; }
            if !xml_name_start(c) && c != U32(45) && c != U32(46) && c != U32(183) &&
                !(c >= U32(48) && c <= U32(57)) && !(c >= U32(768) && c <= U32(879)) &&
                !(c >= U32(8255) && c <= U32(8256)) { return p; }
            p = s.next;
        }
        return p;
    }
    fn xml_at(b: Slice<U8>, p: USize, text: Slice<U8>) -> Bool {
        if p > len(b) || len(text) > len(b) - p { return false; }
        for i in 0..len(text) { if b[p + i] != text[i] { return false; } }
        return true;
    }
    fn xml_same(b: Slice<U8>, p: USize, n: USize, q: USize, m: USize) -> Bool {
        if n != m { return false; }
        for i in 0..n { if b[p + i] != b[q + i] { return false; } }
        return true;
    }
    fn xml_skip_space(b: Slice<U8>, start: USize) -> USize {
        let mut p: USize = start; while p < len(b) && xml_space(b[p]) { p = p + 1; } return p;
    }
    fn xml_find(b: Slice<U8>, start: USize, text: Slice<U8>) -> USize {
        let mut p: USize = start; while p < len(b) { if xml_at(b,p,text) { return p; } p = p + 1; } return len(b);
    }
    fn xml_ref(b: Slice<U8>, start: USize) -> Utf8Step {
        if xml_at(b,start,utf8_bytes("&amp;")) { return Utf8Step(38,start+5,true); }
        if xml_at(b,start,utf8_bytes("&lt;")) { return Utf8Step(60,start+4,true); }
        if xml_at(b,start,utf8_bytes("&gt;")) { return Utf8Step(62,start+4,true); }
        if xml_at(b,start,utf8_bytes("&quot;")) { return Utf8Step(34,start+6,true); }
        if xml_at(b,start,utf8_bytes("&apos;")) { return Utf8Step(39,start+6,true); }
        if !xml_at(b,start,utf8_bytes("&#")) { return Utf8Step(0,start,false); }
        let mut p: USize = start+2; let mut base: U32 = 10;
        if p < len(b) && b[p] == U8(120) { base = 16; p = p+1; }
        let digits: USize = p; let mut value: U32 = 0;
        while p < len(b) && b[p] != U8(59) {
            let c: U32 = cast<U32>(b[p]); let mut d: U32 = 99;
            if c >= U32(48) && c <= U32(57) { d = c-U32(48); }
            if c >= U32(65) && c <= U32(70) { d = c-U32(55); }
            if c >= U32(97) && c <= U32(102) { d = c-U32(87); }
            if d >= base || value > (U32(1114111)-d)/base { return Utf8Step(0,start,false); }
            value=value*base+d;p=p+1;
        }
        if p == digits || p == len(b) || !xml_char(value) { return Utf8Step(0,start,false); }
        return Utf8Step(value,p+1,true);
    }
    fn xml_value_valid(b: Slice<U8>, first: USize, end: USize, attribute: Bool) -> Bool {
        let mut p: USize = first;
        while p < end {
            if b[p] == U8(60) || (!attribute && xml_at(b,p,utf8_bytes("]]>"))) { return false; }
            if b[p] == U8(38) { let s: Utf8Step=xml_ref(b,p); if !s.valid || s.next > end { return false; } p=s.next; }
            else { p=p+1; }
        }
        return true;
    }
    fn xml_lower(c: U8) -> U8 {
        if c >= U8(65) && c <= U8(90) { return c + U8(32); }
        return c;
    }
    fn xml_declaration(b: Slice<U8>, start: USize, end: USize) -> Bool {
        let mut p: USize=xml_skip_space(b,start);let mut stage: USize=0;
        if p == start { return false; }
        while p < end {
            let name: USize=p;p=xml_name_end(b,p);let count: USize=p-name;
            let mut field: USize=99;
            if count == 7 && xml_at(b,name,utf8_bytes("version")) { field=0; }
            if count == 8 && xml_at(b,name,utf8_bytes("encoding")) { field=1; }
            if count == 10 && xml_at(b,name,utf8_bytes("standalone")) { field=2; }
            if field == 99 || (stage == 0 && field != 0) || field < stage { return false; }
            p=xml_skip_space(b,p);if p >= end || b[p] != U8(61) { return false; }p=xml_skip_space(b,p+1);
            if p >= end || (b[p] != U8(34) && b[p] != U8(39)) { return false; }
            let quote: U8=b[p];let value: USize=p+1;p=value;while p < end && b[p] != quote { p=p+1; }
            if p >= end { return false; }let n: USize=p-value;
            if field == 0 && !(n == 3 && xml_at(b,value,utf8_bytes("1.0"))) { return false; }
            if field == 1 && !(n == 5 && xml_lower(b[value]) == U8(117) && xml_lower(b[value+1]) == U8(116) && xml_lower(b[value+2]) == U8(102) && b[value+3] == U8(45) && b[value+4] == U8(56)) { return false; }
            if field == 2 && !((n == 3 && xml_at(b,value,utf8_bytes("yes"))) || (n == 2 && xml_at(b,value,utf8_bytes("no")))) { return false; }
            stage=field+1;p=p+1;let next: USize=xml_skip_space(b,p);if next == p && p != end { return false; }p=next;
        }
        return stage > 0;
    }
    fn xml_parse(b: Slice<U8>) -> XmlDocument {
        let mut raw: Buffer<U8>=buffer<U8>(len(b));for i in 0..len(b) { raw[i]=b[i]; }
        let mut tokens: Buffer<XmlToken>=buffer<XmlToken>(0);let mut stack: Buffer<USize>=buffer<USize>(0);
        let mut p: USize=0;
        while p < len(b) { let s: Utf8Step=utf8_next(b,p);if !s.valid || !xml_char(s.codepoint) { return XmlDocument(move raw,move tokens,false,p); }p=s.next; }
        p=0;if xml_at(b,0,utf8_bytes("﻿")) { p=3; }
        let initial: USize=p;let mut root: Bool=false;let mut closed: Bool=false;
        while p < len(b) {
            let start: USize=p;let depth: USize=len(&stack);
            if b[p] != U8(60) {
                while p < len(b) && b[p] != U8(60) { p=p+1; }
                if !xml_value_valid(b,start,p,false) { return XmlDocument(move raw,move tokens,false,start); }
                if depth == 0 { for i in start..p { if !xml_space(b[i]) { return XmlDocument(move raw,move tokens,false,i); } } }
                push(&mut tokens,XmlToken(4,start,p,0,0,start,p-start,depth));continue;
            }
            if xml_at(b,p,utf8_bytes("<!--")) {
                let value: USize=p+4;let end: USize=xml_find(b,value,utf8_bytes("--"));
                if end == len(b) || !xml_at(b,end,utf8_bytes("-->")) { return XmlDocument(move raw,move tokens,false,p); }
                p=end+3;push(&mut tokens,XmlToken(5,start,p,0,0,value,end-value,depth));continue;
            }
            if xml_at(b,p,utf8_bytes("<![CDATA[")) {
                let value: USize=p+9;let end: USize=xml_find(b,value,utf8_bytes("]]>"));
                if depth == 0 || end == len(b) { return XmlDocument(move raw,move tokens,false,p); }
                p=end+3;push(&mut tokens,XmlToken(7,start,p,0,0,value,end-value,depth));continue;
            }
            if xml_at(b,p,utf8_bytes("<?")) {
                let name: USize=p+2;let name_end: USize=xml_name_end(b,name);let end: USize=xml_find(b,name_end,utf8_bytes("?>"));
                if name == name_end || end == len(b) || (name_end < end && !xml_space(b[name_end])) { return XmlDocument(move raw,move tokens,false,p); }
                let mut kind: U32=6;
                if name_end-name == 3 && (b[name] == U8(120) || b[name] == U8(88)) && (b[name+1] == U8(109) || b[name+1] == U8(77)) && (b[name+2] == U8(108) || b[name+2] == U8(76)) {
                    if start != initial || !xml_at(b,name,utf8_bytes("xml")) || !xml_declaration(b,name_end,end) { return XmlDocument(move raw,move tokens,false,p); }kind=8;
                }
                p=end+2;push(&mut tokens,XmlToken(kind,start,p,name,name_end-name,name_end,end-name_end,depth));continue;
            }
            if xml_at(b,p,utf8_bytes("</")) {
                let name: USize=p+2;let end: USize=xml_name_end(b,name);p=xml_skip_space(b,end);
                if depth == 0 || name == end || p >= len(b) || b[p] != U8(62) { return XmlDocument(move raw,move tokens,false,start); }
                let open: XmlToken=tokens[stack[depth-1]];
                if !xml_same(b,name,end-name,open.name_start,open.name_length) { return XmlDocument(move raw,move tokens,false,start); }
                let remaining: USize=depth-1;resize(&mut stack,remaining);p=p+1;if remaining == 0 { closed=true; }
                push(&mut tokens,XmlToken(2,start,p,name,end-name,0,0,remaining));continue;
            }
            if closed { return XmlDocument(move raw,move tokens,false,p); }
            let name: USize=p+1;let name_end: USize=xml_name_end(b,name);
            if name == name_end { return XmlDocument(move raw,move tokens,false,p); }
            let token: USize=len(&tokens);push(&mut tokens,XmlToken(1,start,0,name,name_end-name,0,0,depth));p=name_end;
            let mut empty: Bool=false;
            while true {
                let next: USize=xml_skip_space(b,p);let spaced: Bool=next>p;p=next;
                if p >= len(b) { return XmlDocument(move raw,move tokens,false,p); }
                if b[p] == U8(62) { p=p+1;break; }
                if xml_at(b,p,utf8_bytes("/>")) { empty=true;p=p+2;break; }
                if !spaced { return XmlDocument(move raw,move tokens,false,p); }
                let attr: USize=p;let attr_end: USize=xml_name_end(b,p);
                if attr == attr_end { return XmlDocument(move raw,move tokens,false,p); }
                for i in token+1..len(&tokens) { let old: XmlToken=tokens[i];if xml_same(b,attr,attr_end-attr,old.name_start,old.name_length) { return XmlDocument(move raw,move tokens,false,attr); } }
                p=xml_skip_space(b,attr_end);if p >= len(b) || b[p] != U8(61) { return XmlDocument(move raw,move tokens,false,p); }
                p=xml_skip_space(b,p+1);if p >= len(b) || (b[p] != U8(34) && b[p] != U8(39)) { return XmlDocument(move raw,move tokens,false,p); }
                let quote: U8=b[p];let value: USize=p+1;p=value;while p < len(b) && b[p] != quote { p=p+1; }
                if p == len(b) || !xml_value_valid(b,value,p,true) { return XmlDocument(move raw,move tokens,false,value); }
                p=p+1;push(&mut tokens,XmlToken(3,attr,p,attr,attr_end-attr,value,p-value-1,depth));
            }
            tokens[token].end=p;
            if empty { tokens[token].kind=9;if depth == 0 { closed=true; } }
            else { if depth >= 256 { return XmlDocument(move raw,move tokens,false,start); }push(&mut stack,token); }
            root=true;
        }
        let valid: Bool=root && len(&stack) == 0;return XmlDocument(move raw,move tokens,valid,p);
    }
    fn xml_append_bytes(out: &mut Buffer<U8>, b: Slice<U8>) -> Unit { for i in 0..len(b) { push(out,b[i]); } }
    fn xml_append_scalar(out: &mut Buffer<U8>, c: U32) -> Unit {
        assert(xml_char(c));
        if c < U32(128) { push(out,cast<U8>(c));return; }
        if c < U32(2048) { push(out,cast<U8>(U32(192)|(c>>U32(6)))); }
        else { if c < U32(65536) { push(out,cast<U8>(U32(224)|(c>>U32(12)))); }
            else { push(out,cast<U8>(U32(240)|(c>>U32(18))));push(out,cast<U8>(U32(128)|((c>>U32(12))&U32(63)))); }
            push(out,cast<U8>(U32(128)|((c>>U32(6))&U32(63)))); }
        push(out,cast<U8>(U32(128)|(c&U32(63))));
    }
    fn xml_value(doc: &XmlDocument, index: USize) -> Buffer<U8> {
        assert(doc.valid);let t: XmlToken=doc.tokens[index];let b: Slice<U8>=slice(&doc.bytes,0,len(&doc.bytes));
        let mut out: Buffer<U8>=buffer<U8>(0);let mut p: USize=t.value_start;let end: USize=p+t.value_length;
        while p < end {
            if (t.kind == U32(3) || t.kind == U32(4)) && b[p] == U8(38) { let s: Utf8Step=xml_ref(b,p);assert(s.valid);xml_append_scalar(&mut out,s.codepoint);p=s.next; }
            else { let mut c: U8=b[p];p=p+1;if c == U8(13) { if p < end && b[p] == U8(10) { p=p+1; }c=10; }if t.kind == U32(3) && (c == U8(9) || c == U8(10)) { c=32; }push(&mut out,c); }
        }
        return move out;
    }
    fn xml_write(doc: &XmlDocument) -> Buffer<U8> {
        assert(doc.valid);let mut out: Buffer<U8>=buffer<U8>(len(&doc.bytes));for i in 0..len(&doc.bytes) { out[i]=doc.bytes[i]; }return move out;
    }
    fn xml_replace_value(doc: &mut XmlDocument, index: USize, value: Slice<U8>) -> Unit {
        assert(doc.valid);let t: XmlToken=doc.tokens[index];assert(t.kind == U32(3) || t.kind == U32(4));
        let mut out: Buffer<U8>=buffer<U8>(0);for i in 0..t.value_start { push(&mut out,doc.bytes[i]); }
        let mut p: USize=0;while p < len(value) { let s: Utf8Step=utf8_next(value,p);assert(s.valid && xml_char(s.codepoint));p=s.next;let c: U32=s.codepoint;
            if c == U32(38) { xml_append_bytes(&mut out,utf8_bytes("&amp;")); }
            else { if c == U32(60) { xml_append_bytes(&mut out,utf8_bytes("&lt;")); }
            else { if c == U32(62) { xml_append_bytes(&mut out,utf8_bytes("&gt;")); }
            else { if c == U32(34) { xml_append_bytes(&mut out,utf8_bytes("&quot;")); }
            else { if c == U32(39) { xml_append_bytes(&mut out,utf8_bytes("&apos;")); }
            else { if c == U32(13) { xml_append_bytes(&mut out,utf8_bytes("&#13;")); }
            else { if t.kind == U32(3) && c == U32(9) { xml_append_bytes(&mut out,utf8_bytes("&#9;")); }
            else { if t.kind == U32(3) && c == U32(10) { xml_append_bytes(&mut out,utf8_bytes("&#10;")); }
            else { xml_append_scalar(&mut out,c); } } } } } } } }
        }
        for i in t.value_start+t.value_length..len(&doc.bytes) { push(&mut out,doc.bytes[i]); }
        let parsed: XmlDocument=xml_parse(slice(&out,0,len(&out)));assert(parsed.valid);*doc=move parsed;
    }
}

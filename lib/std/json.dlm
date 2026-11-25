module std.json
import std.native_binary

native {
    record JsonLimits repr(C) { bytes: USize; values: USize; depth: USize; }
    record JsonError repr(C) { code: U32; offset: USize; }
    record JsonNode repr(C) {
        kind: U32; offset: USize; count: USize; first: USize; next: USize;
        key: USize; key_count: USize;
    }
    record JsonDocument repr(C) { nodes: Buffer<JsonNode>; bytes: Buffer<U8>; root: USize; error: JsonError; }
    record JsonFrame repr(C) { id: USize; phase: U32; last: USize; key: USize; key_count: USize; }
    record JsonKey repr(C) { owner: USize; offset: USize; count: USize; hash: U64; }
    record JsonParser repr(C) {
        limits: JsonLimits; error: JsonError; seen: USize; root: USize;
        nodes: Buffer<JsonNode>; bytes: Buffer<U8>; stack: Buffer<JsonFrame>; keys: Buffer<JsonKey>;
        token: Buffer<U8>; token_at: USize; mode: U32; literal: U32; matched: USize; key: Bool; escaped: Bool;
    }
    record JsonInteger repr(C) { magnitude: U64; negative: Bool; valid: Bool; }
    record JsonFloat repr(C) { value: F64; valid: Bool; }
    record JsonEncoded repr(C) { bytes: Buffer<U8>; error: JsonError; }
    record JsonFieldSpec repr(C) { name: Buffer<U8>; kind: U32; optional: Bool; nullable: Bool; }
    record JsonFieldValue repr(C) { node: USize; kind: U32; truth: Bool; signed: I64; unsigned: U64; real: F64; }
    record JsonSchemaError repr(C) { code: U32; field: USize; node: USize; }
    record JsonRecord repr(C) { fields: Buffer<JsonFieldValue>; error: JsonSchemaError; }

    fn json_limits() -> JsonLimits { return JsonLimits(1048576, 32768, 64); }
    fn json_parser(limits: JsonLimits) -> JsonParser {
        let mut result: JsonParser = zero<JsonParser>();
        result.limits = limits;
        if limits.bytes == 0 || limits.bytes > 16777216 || limits.values == 0 || limits.values > 262144 || limits.depth == 0 || limits.depth > 128 {
            result.error = JsonError(3, 0); return move result;
        }
        push(&mut result.nodes, zero<JsonNode>());
        return move result;
    }
    fn json_space(c: U8) -> Bool { return c == 32 || c == 9 || c == 10 || c == 13; }
    fn json_digit(c: U8) -> Bool { return c >= 48 && c <= 57; }
    fn json_byte_equal(bytes: &Buffer<U8>, a: USize, b: USize, count: USize) -> Bool {
        for i in 0..count { if bytes[a+i] != bytes[b+i] { return false; } }
        return true;
    }
    fn json_hex4(input: &Buffer<U8>, first: USize) -> U32 {
        if first > len(input) || len(input)-first < 4 { return 4294967295; }
        let mut scalar: U32 = 0;
        for i in first..first+4 {
            let c: U8 = input[i]; let mut digit: U32 = 0;
            if json_digit(c) { digit = cast<U32>(c)-48; }
            else { if c >= 65 && c <= 70 { digit = cast<U32>(c)-55; }
            else { if c >= 97 && c <= 102 { digit = cast<U32>(c)-87; }
            else { return 4294967295; } } }
            scalar = scalar*16+digit;
        }
        return scalar;
    }
    fn json_utf8_push(output: &mut Buffer<U8>, c: U32) -> Unit {
        if c < 128 { push(output, cast<U8>(c)); return; }
        if c < 2048 { push(output, cast<U8>(U32(192) | (c >> 6))); }
        else {
            if c < 65536 { push(output, cast<U8>(U32(224) | (c >> 12))); }
            else { push(output, cast<U8>(U32(240) | (c >> 18))); push(output, cast<U8>(U32(128) | ((c >> 12) & U32(63)))); }
            push(output, cast<U8>(U32(128) | ((c >> 6) & U32(63))));
        }
        push(output, cast<U8>(U32(128) | (c & U32(63))));
    }
    fn json_unescape(input: &Buffer<U8>, output: &mut Buffer<U8>) -> U32 {
        let mut i: USize = 0;
        while i < len(input) {
            let c: U8 = input[i];
            if c == 92 {
                i = i+1;
                if i == len(input) { return 1; }
                let escape: U8 = input[i];
                if escape == 34 || escape == 47 || escape == 92 { push(output, escape); }
                else { if escape == 98 { push(output, U8(8)); }
                else { if escape == 102 { push(output, U8(12)); }
                else { if escape == 110 { push(output, U8(10)); }
                else { if escape == 114 { push(output, U8(13)); }
                else { if escape == 116 { push(output, U8(9)); }
                else { if escape == 117 {
                    let mut scalar: U32 = json_hex4(input,i+1);
                    if scalar > 65535 { return 2; }
                    i = i+4;
                    if scalar >= 55296 && scalar <= 56319 {
                        if len(input)-i < 7 || input[i+1] != U8(92) || input[i+2] != U8(117) { return 2; }
                        let tail: U32 = json_hex4(input,i+3);
                        if tail < 56320 || tail > 57343 { return 2; }
                        scalar = 65536+(scalar-55296)*1024+tail-56320; i = i+6;
                    } else { if scalar >= 56320 && scalar <= 57343 { return 2; } }
                    json_utf8_push(output,scalar);
                } else { return 1; } } } } } } }
                i = i+1;
            } else {
                if c < 32 || c == 34 { return 1; }
                let step: Utf8Step = utf8_next(slice(input,0,len(input)), i);
                if !step.valid { return 2; }
                for j in i..step.next { push(output,input[j]); }
                i = step.next;
            }
        }
        return 0;
    }
    fn json_number_valid(input: Slice<U8>) -> Bool {
        let end: USize = len(input);
        let mut i: USize = 0;
        if end == 0 { return false; }
        if input[0] == U8(45) { i = 1; }
        if i >= end || !json_digit(input[i]) { return false; }
        if input[i] == U8(48) { i = i+1; }
        else { while i < end && json_digit(input[i]) { i = i+1; } }
        if i < end && input[i] == U8(46) {
            i = i+1; let start: USize = i;
            while i < end && json_digit(input[i]) { i = i+1; }
            if i == start { return false; }
        }
        if i < end && (input[i] == U8(101) || input[i] == U8(69)) {
            i = i+1;
            if i < end && (input[i] == U8(43) || input[i] == U8(45)) { i = i+1; }
            let start: USize = i;
            while i < end && json_digit(input[i]) { i = i+1; }
            if i == start { return false; }
        }
        return i == end;
    }
    fn json_add(p: &mut JsonParser, kind: U32, offset: USize, count: USize) -> USize {
        let id: USize = len(&p.nodes);
        if id > p.limits.values { p.error = JsonError(3,p.seen); return 0; }
        let depth: USize = len(&p.stack);
        if depth == 0 { p.root = id; push(&mut p.nodes,JsonNode(kind,offset,count,0,0,0,0)); return id; }
        let top: USize = depth-1;
        let frame: JsonFrame = p.stack[top];
        if frame.last == 0 { p.nodes[frame.id].first = id; }
        else { p.nodes[frame.last].next = id; }
        p.nodes[frame.id].count = p.nodes[frame.id].count+1;
        p.stack[top].last = id;
        p.stack[top].phase = 2;
        push(&mut p.nodes,JsonNode(kind,offset,count,0,0,frame.key,frame.key_count));
        return id;
    }
    fn json_register_key(p: &mut JsonParser, offset: USize, count: USize) -> U32 {
        let top: USize = len(&p.stack)-1;
        let owner: USize = p.stack[top].id;
        if len(&p.keys) == 0 { resize(&mut p.keys,p.limits.values*2+1); }
        let capacity: USize = len(&p.keys);
        let mut hash: U64 = U64(14695981039346656037)^cast<U64>(owner);
        for i in offset..offset+count { hash = wrapping_mul(hash^cast<U64>(p.bytes[i]),U64(1099511628211)); }
        let mut pos: USize = cast<USize>(hash)%capacity;
        for i in 0..128 {
            let old: JsonKey = p.keys[pos];
            if old.owner == 0 {
                p.keys[pos] = JsonKey(owner,offset,count,hash);
                p.stack[top].key = offset; p.stack[top].key_count = count; p.stack[top].phase = 5;
                return 0;
            }
            if old.owner == owner && old.hash == hash && old.count == count && json_byte_equal(&p.bytes,old.offset,offset,count) { return 4; }
            pos = (pos+1)%capacity;
        }
        return 3;
    }
    fn json_finish_number(p: &mut JsonParser) -> Unit {
        let n: USize = len(&p.token);
        if !json_number_valid(slice(&p.token,0,n)) { p.error = JsonError(1,p.token_at); return; }
        let offset: USize = len(&p.bytes);
        for i in 0..n { push(&mut p.bytes,p.token[i]); }
        let id: USize = json_add(p,3,offset,n);
        resize(&mut p.token,0); p.mode = 0;
    }
    fn json_begin_value(p: &mut JsonParser, c: U8) -> Unit {
        p.token_at = p.seen;
        if c == 34 { p.mode = 1; p.key = false; p.escaped = false; return; }
        if c == 45 || json_digit(c) { p.mode = 2; push(&mut p.token,c); return; }
        if c == 116 || c == 102 || c == 110 {
            p.mode = 3; p.literal = cast<U32>(c); p.matched = 1; return;
        }
        if c == 91 || c == 123 {
            if len(&p.stack) == p.limits.depth { p.error = JsonError(3,p.seen); return; }
            let mut kind: U32 = 5; let mut phase: U32 = 0;
            if c == 123 { kind = 6; phase = 3; }
            let id: USize = json_add(p,kind,0,0);
            if id != 0 { push(&mut p.stack,JsonFrame(id,phase,0,0,0)); }
            return;
        }
        p.error = JsonError(1,p.seen);
    }
    fn json_structure(p: &mut JsonParser, c: U8) -> Unit {
        if json_space(c) { return; }
        let depth: USize = len(&p.stack);
        if depth == 0 {
            if p.root != 0 { p.error = JsonError(1,p.seen); }
            else { json_begin_value(p,c); }
            return;
        }
        let top: USize = depth-1;
        let frame: JsonFrame = p.stack[top];
        let object: Bool = p.nodes[frame.id].kind == U32(6);
        if (c == 93 && !object && (frame.phase == U32(0) || frame.phase == U32(2))) || (c == 125 && object && (frame.phase == U32(3) || frame.phase == U32(2))) {
            resize(&mut p.stack,top); return;
        }
        if frame.phase == 2 && c == 44 {
            p.stack[top].phase = 1;
            if object { p.stack[top].phase = 4; }
            return;
        }
        if frame.phase == 5 && c == 58 { p.stack[top].phase = 6; return; }
        if (frame.phase == U32(3) || frame.phase == U32(4)) && c == 34 {
            p.mode = 1; p.key = true; p.escaped = false; p.token_at = p.seen; return;
        }
        if frame.phase == 0 || frame.phase == 1 || frame.phase == 6 { json_begin_value(p,c); return; }
        p.error = JsonError(1,p.seen);
    }
    fn json_feed(p: &mut JsonParser, input: Slice<U8>) -> JsonError {
        for i in 0..len(input) {
            if p.error.code != 0 { break; }
            if p.seen == p.limits.bytes { p.error = JsonError(3,p.seen); break; }
            let c: U8 = input[i];
            if p.mode == 2 && !json_digit(c) && c != 46 && c != 43 && c != 45 && c != 69 && c != 101 { json_finish_number(p); }
            if p.error.code != 0 { break; }
            if p.mode == 0 { json_structure(p,c); }
            else { if p.mode == 2 { push(&mut p.token,c); }
            else { if p.mode == 1 {
                if p.escaped { p.escaped = false; push(&mut p.token,c); }
                else { if c == 34 {
                    let offset: USize = len(&p.bytes);
                    let err: U32 = json_unescape(&p.token,&mut p.bytes);
                    let n: USize = len(&p.bytes)-offset;
                    if err != 0 { p.error = JsonError(err,p.token_at); }
                    else { if p.key {
                        let key_status: U32 = json_register_key(p,offset,n);
                        if key_status != 0 { p.error = JsonError(key_status,p.token_at); }
                    } else { let id: USize = json_add(p,4,offset,n); } }
                    resize(&mut p.token,0); p.mode = 0;
                } else { p.escaped = c == U8(92); push(&mut p.token,c); } }
            } else {
                let mut word: Utf8 = "null";
                if p.literal == 116 { word = "true"; }
                if p.literal == 102 { word = "false"; }
                let literal: Slice<U8> = utf8_bytes(word);
                if c != literal[p.matched] { p.error = JsonError(1,p.seen); }
                else {
                    p.matched = p.matched+1;
                    if p.matched == len(literal) {
                        let mut kind: U32 = 2; let mut flag: USize = 0;
                        if p.literal == 110 { kind = 1; }
                        if p.literal == 116 { flag = 1; }
                        let id: USize = json_add(p,kind,flag,0); p.mode = 0;
                    }
                }
            } } }
            p.seen = p.seen+1;
        }
        return p.error;
    }
    fn json_finish(p: JsonParser) -> JsonDocument {
        let mut parser: JsonParser = move p;
        if parser.error.code == 0 && parser.mode == 2 { json_finish_number(&mut parser); }
        if parser.error.code == 0 && (parser.mode != 0 || parser.root == 0 || len(&parser.stack) != 0) {
            parser.error = JsonError(1,parser.seen);
        }
        if parser.error.code != 0 { return JsonDocument(buffer<JsonNode>(0),buffer<U8>(0),0,parser.error); }
        return JsonDocument(move parser.nodes,move parser.bytes,parser.root,parser.error);
    }
    fn json_parse(input: Slice<U8>, limits: JsonLimits) -> JsonDocument {
        let mut p: JsonParser = json_parser(limits);
        let error: JsonError = json_feed(&mut p,input);
        return json_finish(move p);
    }
    fn json_node(doc: &JsonDocument, id: USize) -> JsonNode {
        if id == 0 || id >= len(&doc.nodes) { return zero<JsonNode>(); }
        return doc.nodes[id];
    }
    fn json_member(doc: &JsonDocument, id: USize, name: Slice<U8>) -> USize {
        let object: JsonNode = json_node(doc,id);
        if object.kind != 6 { return 0; }
        let mut child: USize = object.first;
        for i in 0..object.count {
            let item: JsonNode = json_node(doc,child);
            if item.kind == 0 { return 0; }
            if item.key_count == len(name) && item.key <= len(&doc.bytes) && item.key_count <= len(&doc.bytes)-item.key {
                let mut equal: Bool = true;
                for j in 0..len(name) { if name[j] != doc.bytes[item.key+j] { equal = false; break; } }
                if equal { return child; }
            }
            child = item.next;
        }
        return 0;
    }
    fn json_text(doc: &JsonDocument, id: USize) -> Slice<U8> borrows(doc) {
        let node: JsonNode = json_node(doc,id);
        if (node.kind == U32(3) || node.kind == U32(4)) && node.offset <= len(&doc.bytes) && node.count <= len(&doc.bytes)-node.offset {
            return slice(&doc.bytes,node.offset,node.count);
        }
        return slice(&doc.bytes,0,0);
    }
    fn json_copy_text(doc: &JsonDocument, id: USize) -> Buffer<U8> {
        let input: Slice<U8> = json_text(doc,id);
        let mut output: Buffer<U8> = buffer<U8>(len(input));
        for i in 0..len(input) { output[i] = input[i]; }
        return move output;
    }
    fn json_integer(doc: &JsonDocument, id: USize) -> JsonInteger {
        if json_node(doc,id).kind != 3 { return JsonInteger(0,false,false); }
        let input: Slice<U8> = json_text(doc,id);
        if !json_number_valid(input) { return JsonInteger(0,false,false); }
        let negative: Bool = input[0] == U8(45);
        let mut i: USize = 0; if negative { i = 1; }
        let start: USize = i;
        let mut fraction: I64 = 0;
        let mut dot: Bool = false;
        let mut digits: I64 = 0;
        let mut first_nonzero: I64 = 0;
        let mut last_nonzero: I64 = 0;
        while i < len(input) && input[i] != U8(69) && input[i] != U8(101) {
            if input[i] == U8(46) { dot = true; }
            else { digits = digits+1; if dot { fraction = fraction+1; } if input[i] != U8(48) { if first_nonzero == 0 { first_nonzero = digits; } last_nonzero = digits; } }
            i = i+1;
        }
        let mantissa_end: USize = i;
        if last_nonzero == 0 { return JsonInteger(0,negative,true); }
        let mut exponent: I64 = 0;
        if i < len(input) {
            i = i+1;
            let minus: Bool = input[i] == U8(45);
            if minus || input[i] == U8(43) { i = i+1; }
            while i < len(input) && exponent < I64(16777217) { exponent = exponent*10+cast<I64>(input[i])-48; i = i+1; }
            if minus { exponent = -exponent; }
        }
        let scale: I64 = exponent-fraction;
        let remaining: I64 = digits+scale;
        if remaining < last_nonzero || remaining-first_nonzero >= I64(20) { return JsonInteger(0,negative,false); }
        let mut value: U64 = 0;
        let mut consumed: I64 = 0;
        for j in start..mantissa_end {
            if input[j] != U8(46) && consumed < remaining {
                let d: U64 = cast<U64>(input[j])-48;
                if value > (U64(18446744073709551615)-d)/10 { return JsonInteger(0,negative,false); }
                value = value*10+d; consumed = consumed+1;
            }
        }
        while consumed < remaining {
            if value > U64(1844674407370955161) { return JsonInteger(0,negative,false); }
            value = value*10; consumed = consumed+1;
        }
        return JsonInteger(value,negative,true);
    }
    fn json_float(doc: &JsonDocument, id: USize) -> JsonFloat {
        if json_node(doc,id).kind != 3 { return JsonFloat(F64(0),false); }
        let input: Slice<U8> = json_text(doc,id);
        if !json_number_valid(input) { return JsonFloat(F64(0),false); }
        let value: F64 = parse_decimal_f64(input);
        if !is_finite(value) { return JsonFloat(F64(0),false); }
        return JsonFloat(value,true);
    }
    fn json_field(name: Slice<U8>, kind: U32, optional: Bool, nullable: Bool) -> JsonFieldSpec {
        let mut bytes: Buffer<U8> = buffer<U8>(len(name));
        for i in 0..len(name) { bytes[i]=name[i]; }
        return JsonFieldSpec(move bytes,kind,optional,nullable);
    }
    fn json_decode_field(doc: &JsonDocument, id: USize, kind: U32) -> JsonFieldValue {
        let node: JsonNode = json_node(doc,id);
        if kind <= 6 && kind == node.kind { return JsonFieldValue(id,kind,kind == U32(2) && node.offset == USize(1),0,0,F64(0)); }
        if kind == 7 || kind == 8 {
            let integer: JsonInteger = json_integer(doc,id);
            if integer.valid {
                if kind == 8 && (!integer.negative || integer.magnitude == 0) { return JsonFieldValue(id,kind,false,0,integer.magnitude,F64(0)); }
                if kind == 7 {
                    if integer.negative && integer.magnitude == U64(9223372036854775808) { return JsonFieldValue(id,kind,false,-9223372036854775808,0,F64(0)); }
                    if integer.magnitude <= U64(9223372036854775807) {
                        let mut value: I64 = cast<I64>(integer.magnitude);
                        if integer.negative { value = -value; }
                        return JsonFieldValue(id,kind,false,value,0,F64(0));
                    }
                }
            }
        }
        if kind == 9 {
            let value: JsonFloat = json_float(doc,id);
            if value.valid { return JsonFieldValue(id,kind,false,0,0,value.value); }
        }
        return zero<JsonFieldValue>();
    }
    fn json_decode_record(doc: &JsonDocument, id: USize, fields: Slice<JsonFieldSpec>, exact: Bool) -> JsonRecord {
        let object: JsonNode = json_node(doc,id);
        if doc.error.code != 0 || object.kind != 6 { return JsonRecord(buffer<JsonFieldValue>(0),JsonSchemaError(1,0,id)); }
        if len(fields) > 512 { return JsonRecord(buffer<JsonFieldValue>(0),JsonSchemaError(2,0,id)); }
        let mut output: Buffer<JsonFieldValue> = buffer<JsonFieldValue>(len(fields));
        let mut found: USize = 0;
        for i in 0..len(fields) {
            let spec: &JsonFieldSpec = &fields[i];
            let name: Slice<U8> = slice(&spec.name,0,len(&spec.name));
            if spec.kind == 0 || spec.kind > 9 { return JsonRecord(buffer<JsonFieldValue>(0),JsonSchemaError(2,i,id)); }
            for j in 0..i {
                if len(&fields[j].name) == len(name) {
                    let mut same: Bool = true;
                    for k in 0..len(name) { if fields[j].name[k] != name[k] { same = false; break; } }
                    if same { return JsonRecord(buffer<JsonFieldValue>(0),JsonSchemaError(2,i,id)); }
                }
            }
            let node: USize = json_member(doc,id,name);
            if node == 0 {
                if !spec.optional { return JsonRecord(buffer<JsonFieldValue>(0),JsonSchemaError(3,i,id)); }
                continue;
            }
            found = found+1;
            if spec.nullable && json_node(doc,node).kind == 1 { output[i] = JsonFieldValue(node,1,false,0,0,F64(0)); }
            else {
                let value: JsonFieldValue = json_decode_field(doc,node,spec.kind);
                if value.node == 0 { return JsonRecord(buffer<JsonFieldValue>(0),JsonSchemaError(4,i,node)); }
                output[i] = value;
            }
        }
        if exact && found != object.count { return JsonRecord(buffer<JsonFieldValue>(0),JsonSchemaError(5,len(fields),id)); }
        return JsonRecord(move output,zero<JsonSchemaError>());
    }
    fn json_emit_byte(output: &mut Buffer<U8>, limit: USize, byte: U8) -> Bool {
        if len(output) >= limit { return false; }
        push(output,byte); return true;
    }
    fn json_emit_bytes(output: &mut Buffer<U8>, limit: USize, input: Slice<U8>) -> Bool {
        if len(output) > limit || len(input) > limit-len(output) { return false; }
        for i in 0..len(input) { push(output,input[i]); }
        return true;
    }
    fn json_emit_string(output: &mut Buffer<U8>, limit: USize, input: Slice<U8>) -> Bool {
        if !json_emit_byte(output,limit,34) { return false; }
        let hex: Slice<U8> = utf8_bytes("0123456789abcdef");
        let mut i: USize = 0;
        while i < len(input) {
            let c: U8 = input[i];
            if c == 34 || c == 92 {
                if !json_emit_byte(output,limit,92) || !json_emit_byte(output,limit,c) { return false; }
                i = i+1;
            } else { if c < 32 {
                if !json_emit_bytes(output,limit,utf8_bytes("\\u00")) || !json_emit_byte(output,limit,hex[cast<USize>(c)/16]) || !json_emit_byte(output,limit,hex[cast<USize>(c)%16]) { return false; }
                i = i+1;
            } else {
                let step: Utf8Step = utf8_next(input,i);
                if !step.valid { return false; }
                for j in i..step.next { if !json_emit_byte(output,limit,input[j]) { return false; } }
                i = step.next;
            } }
        }
        return json_emit_byte(output,limit,34);
    }
    fn json_emit_node(doc: &JsonDocument, id: USize, output: &mut Buffer<U8>, limit: USize, depth: USize) -> Bool {
        if depth > 128 { return false; }
        let node: JsonNode = json_node(doc,id);
        if node.kind == 1 { return json_emit_bytes(output,limit,utf8_bytes("null")); }
        if node.kind == 2 {
            if node.offset == 1 { return json_emit_bytes(output,limit,utf8_bytes("true")); }
            return node.offset == 0 && json_emit_bytes(output,limit,utf8_bytes("false"));
        }
        if node.kind == 3 { let number: Slice<U8> = json_text(doc,id); return json_number_valid(number) && json_emit_bytes(output,limit,number); }
        if node.kind == 4 { return json_emit_string(output,limit,json_text(doc,id)); }
        if node.kind != 5 && node.kind != 6 { return false; }
        let mut first: U8 = 91; let mut last: U8 = 93;
        if node.kind == 6 { first = 123; last = 125; }
        if !json_emit_byte(output,limit,first) { return false; }
        let mut child: USize = node.first;
        for i in 0..node.count {
            if child <= id || child >= len(&doc.nodes) { return false; }
            if i > 0 && !json_emit_byte(output,limit,44) { return false; }
            let item: JsonNode = doc.nodes[child];
            if node.kind == 6 {
                if item.key > len(&doc.bytes) || item.key_count > len(&doc.bytes)-item.key { return false; }
                if !json_emit_string(output,limit,slice(&doc.bytes,item.key,item.key_count)) || !json_emit_byte(output,limit,58) { return false; }
            }
            if !json_emit_node(doc,child,output,limit,depth+1) || (item.next != 0 && item.next <= child) { return false; }
            child = item.next;
        }
        return child == 0 && json_emit_byte(output,limit,last);
    }
    fn json_encode(doc: &JsonDocument, maximum: USize) -> JsonEncoded {
        if doc.error.code != 0 { return JsonEncoded(buffer<U8>(0),doc.error); }
        let mut output: Buffer<U8> = buffer<U8>(0);
        if maximum > 33554432 || !json_emit_node(doc,doc.root,&mut output,maximum,0) {
            return JsonEncoded(buffer<U8>(0),JsonError(3,0));
        }
        return JsonEncoded(move output,JsonError(0,0));
    }
}

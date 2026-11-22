module dpi.genetics
import dpi.bytes

native {
    record Gene repr(C) { reverse: U32; cut1: U32; cut2: U32; overlap: U32; guard: U32; repeats: U32; gap: U32; template_: U32; }
    record Individual repr(C) { gene: Gene; fitness: U32; trials: U32; }
    fn gene_default() -> Gene { return Gene(1,0,0,0,0,0,1048576,0); }
    fn gene_valid(g: &Gene) -> Bool {
        return g.reverse <= U32(1) && g.cut1 <= U32(1200) && g.cut2 <= U32(1200) && g.overlap <= U32(32) &&
            g.guard <= U32(2) && g.repeats <= U32(3) && g.gap >= U32(65536) && g.gap <= U32(16777216) && g.template_ <= U32(2) &&
            ((g.guard == U32(0) && g.repeats == U32(0)) || (g.guard > U32(0) && g.repeats > U32(0)));
    }
    fn gene_field(g: &Gene, i: USize) -> U32 {
        if i == 0 { return g.reverse; }if i == 1 { return g.cut1; }if i == 2 { return g.cut2; }if i == 3 { return g.overlap; }
        if i == 4 { return g.guard; }if i == 5 { return g.repeats; }if i == 6 { return g.gap; }return g.template_;
    }
    fn gene_set(g: &mut Gene, i: USize, n: U32) -> Unit {
        if i == 0 { g.reverse=n; }else if i == 1 { g.cut1=n; }else if i == 2 { g.cut2=n; }else if i == 3 { g.overlap=n; }
        else if i == 4 { g.guard=n; }else if i == 5 { g.repeats=n; }else if i == 6 { g.gap=n; }else { g.template_=n; }
    }
    fn gene_parse(text: Slice<U8>, g: &mut Gene) -> Bool {
        let mut value: Gene=zero<Gene>();let mut at: USize=0;
        for i in 0..8 { let end: USize=byte_find(text,at,U8(44));if (i < 7 && end == len(text)) || (i == 7 && end != len(text)) { return false; }
            let mut n: U32=0;if !decimal(slice(&text,at,end-at),16777216,&mut n) { return false; }gene_set(&mut value,i,n);at=end+1; }
        if !gene_valid(&value) { return false; }*g=value;return true;
    }
    fn gene_text(g: &Gene) -> Buffer<U8> {
        let mut b: Buffer<U8>=buffer<U8>(0);for i in 0..8 { if i > 0 { push(&mut b,U8(44)); }append_number(&mut b,cast<U64>(gene_field(g,i))); }return move b;
    }
    fn gene_hash(g: &Gene) -> U64 {
        let mut hash: U64=1469598103934665603;for i in 0..8 { hash=wrapping_mul(hash ^ cast<U64>(gene_field(g,i)),U64(1099511628211)); }return hash;
    }
    fn evolution_random(state: &mut U64) -> U32 { *state=wrapping_add(wrapping_mul(*state,U64(6364136223846793005)),U64(1442695040888963407));return cast<U32>(*state >> U64(32)); }
    fn gene_normalize(g: &mut Gene) -> Unit { if g.guard == U32(0) { g.repeats=0;g.template_=0;g.gap=1048576; }else if g.repeats == U32(0) { g.repeats=1; } }
    fn gene_mutate(g: &mut Gene, state: &mut U64) -> Unit {
        let anchors: Array<U32,12>=array<Array<U32,12>>(0,1,2,5,32,64,128,256,512,768,1024,1200);
        for turn in 0..2 {
            let i: USize=cast<USize>(evolution_random(state)%U32(8));let r: U32=evolution_random(state);
            if i == 0 { g.reverse=r%U32(2); }else if i == 1 || i == 2 { let mut cut: U32=anchors[cast<USize>(r%U32(12))];if (r & U32(16)) != U32(0) && cut > U32(8) { cut=min_u32(cut+r%U32(17),1200); }gene_set(g,i,cut); }
            else if i == 3 { g.overlap=r%U32(33); }else if i == 4 { g.guard=r%U32(3); }else if i == 5 { g.repeats=1+r%U32(3); }
            else if i == 6 { g.gap=U32(65536) << (r%U32(9)); }else { g.template_=r%U32(3); }
        }gene_normalize(g);
    }
    fn min_u32(a: U32, b: U32) -> U32 { if a < b { return a; }return b; }
    fn gene_cross(a: &Gene, b: &Gene, state: &mut U64) -> Gene {
        let mut g: Gene=*a;for i in 0..8 { if evolution_random(state)%U32(2) == U32(0) { gene_set(&mut g,i,gene_field(b,i)); } }gene_normalize(&mut g);return g;
    }
    fn tournament(items: &Buffer<Individual>, state: &mut U64) -> USize {
        let mut best: USize=cast<USize>(evolution_random(state))%len(items);for n in 0..2 { let i: USize=cast<USize>(evolution_random(state))%len(items);if items[i].fitness > items[best].fitness { best=i; } }return best;
    }
    fn evolve(items: &Buffer<Individual>, state: &mut U64, count: USize) -> Buffer<Gene> {
        let mut output: Buffer<Gene>=buffer<Gene>(0);let mut elite: USize=len(items);
        for i in 0..len(items) { if items[i].trials > U32(0) && (elite == len(items) || items[i].fitness > items[elite].fitness) { elite=i; } }
        if elite < len(items) { push(&mut output,items[elite].gene); }
        let mut attempts: USize=0;while len(&output) < count && attempts < count*USize(64) {
            attempts=attempts+1;let mut gene: Gene=gene_default();
            if len(items) > 0 { let a: USize=tournament(items,state);let b: USize=tournament(items,state);gene=gene_cross(&items[a].gene,&items[b].gene,state); }
            gene_mutate(&mut gene,state);let mut duplicate: Bool=false;for i in 0..len(&output) { duplicate=duplicate || gene_hash(&output[i]) == gene_hash(&gene); }
            if !duplicate && gene_valid(&gene) { push(&mut output,gene); }
        }return move output;
    }
    fn profile_gene(text: Slice<U8>, gene: &mut Gene, target: &mut U32) -> Bool {
        let mut schema: Bool=false;let mut found: Bool=false;let mut at: USize=0;let mut active: Bool=false;let mut target_seen: Bool=false;
        let mut parsed: Gene=gene_default();let mut scope: U32=0;if bytes_at(text,0,utf8_bytes("﻿")) { at=3; }
        while at < len(text) { let end: USize=byte_find(text,at,U8(10));let raw: Slice<U8>=slice(&text,at,end-at);let line: Slice<U8>=trimmed(raw);at=end+1;
            if bytes_equal(line,utf8_bytes("[DPIProfile]")) { active=true;continue; }if len(line) == 0 || line[0] == U8(35) || line[0] == U8(59) { continue; }
            if line[0] == U8(91) { active=false;continue; }if !active { continue; }let equal: USize=byte_find(line,0,U8(61));if equal == len(line) { return false; }
            let key: Slice<U8>=slice(&line,0,equal);let value: Slice<U8>=slice(&line,equal+1,len(line)-equal-1);
            if bytes_equal(key,utf8_bytes("Schema")) { if schema || !bytes_equal(value,utf8_bytes("1")) { return false; }schema=true; }
            if bytes_equal(key,utf8_bytes("Gene")) { if found || !gene_parse(value,&mut parsed) { return false; }found=true; }
            if bytes_equal(key,utf8_bytes("Target")) { if target_seen || !decimal(value,6,&mut scope) { return false; }target_seen=true; }
        }if !schema || !found || !target_seen { return false; }*gene=parsed;*target=scope;return true;
    }
}

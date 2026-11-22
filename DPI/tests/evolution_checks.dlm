module tests.evolution_checks
import tests.automatic_checks
import dpi.training_view

native {
    fn test_gene_codec() -> Unit {
        let mut gene: Gene=gene_default();assert(gene_valid(&gene));let text: Buffer<U8>=gene_text(&gene);let mut parsed: Gene=zero<Gene>();assert(gene_parse(slice(&text,0,len(&text)),&mut parsed));assert(gene_hash(&parsed) == gene_hash(&gene));
        assert(!gene_parse(utf8_bytes("1,1,5,8,1,2,1024,0"),&mut parsed));assert(!gene_parse(utf8_bytes("1,1201,5,8,1,2,65536,0"),&mut parsed));assert(!gene_parse(utf8_bytes("1,1,5,8,0,2,65536,0"),&mut parsed));assert(!gene_parse(utf8_bytes("1,1,5,8,1,4,65536,0"),&mut parsed));assert(!gene_parse(utf8_bytes("1,1,5,8,1,2,65536,0,9"),&mut parsed));
        let profile: Slice<U8>=utf8_bytes("﻿[DPIProfile]\nSchema=1\nGene=1,1,5,8,1,2,65536,0\nTarget=0\nValidated=0\n");let mut target: U32=9;assert(profile_gene(profile,&mut parsed,&mut target));assert(target == U32(0));assert(!profile_gene(utf8_bytes("[DPIProfile]\nSchema=2\nGene=1,1,5,8,1,2,65536,0\nTarget=0\n"),&mut parsed,&mut target));
    }
    fn test_evolution_population() -> Unit {
        let mut state: U64=123;let mut gene: Gene=gene_default();for i in 0..2000 { gene_mutate(&mut gene,&mut state);assert(gene_valid(&gene)); }
        let mut population: Buffer<Individual>=buffer<Individual>(0);let elite: Gene=Gene(1,32,512,16,2,1,1048576,1);push(&mut population,Individual(gene_default(),100,4));push(&mut population,Individual(elite,900000,8));
        let mut a: U64=789;let mut b: U64=789;let one: Buffer<Gene>=evolve(&population,&mut a,8);let two: Buffer<Gene>=evolve(&population,&mut b,8);assert(len(&one) == 8 && len(&two) == 8 && gene_hash(&one[0]) == gene_hash(&elite));
        for i in 0..8 { assert(gene_valid(&one[i]) && gene_hash(&one[i]) == gene_hash(&two[i]));for j in 0..i { assert(gene_hash(&one[i]) != gene_hash(&one[j])); } }
    }
    fn test_gene_packet_invariants() -> Unit {
        let body: Buffer<U8>=hello_for(utf8_bytes("www.youtube.com"));let mut state: U64=456;let mut gene: Gene=gene_default();
        for i in 0..80 { gene_mutate(&mut gene,&mut state);let mut o: Options=default_options();o.safe_automatic=true;o.gene_mode=true;o.gene=gene;finish_options(&mut o);assert(!o.adaptive);
            let policy: Policy=zero<Policy>();let mut e: Engine=engine_new(move o,move policy);let mut packet: Buffer<U8>=tcp_packet(slice(&body,0,len(&body)),4294967290,24,i%2 == 1);let view: Packet=parse_packet(slice(&packet,0,len(&packet)));
            let result: Dispatch=evaluate(&mut e,&mut packet,1,0,true,false,1000,false);assert(result.consumed && result.packets.count > 0);let mut offset: USize=0;let mut covered: Buffer<U8>=buffer<U8>(len(&body));
            for part in 0..result.packets.count { let n: USize=cast<USize>(result.packets.lengths[part]);let raw: Slice<U8>=slice(&result.packets.bytes,offset,n);let parsed: Packet=parse_packet(raw);assert(parsed.valid);let start: USize=cast<USize>(wrapping_sub(parsed.sequence,view.sequence));assert(start+parsed.payload_length <= len(&body));for j in 0..parsed.payload_length { assert(raw[parsed.payload+j] == body[start+j]);covered[start+j]=1; }offset=offset+n; }
            for j in 0..len(&body) { assert(covered[j] == U8(1)); }
            assert(result.fake.count <= 3);offset=0;for part in 0..result.fake.count { let n: USize=cast<USize>(result.fake.lengths[part]);let raw: Slice<U8>=slice(&result.fake.bytes,offset,n);let cover: Packet=parse_packet(raw);assert(cover.valid && wrapping_sub(view.sequence,cover.sequence) >= U32(65536));if gene.guard == U32(1) { let mut repaired: Buffer<U8>=bytes_copy(raw);let sum: U16=read_be<U16>(&repaired,cover.transport+16);assert(checksums(&mut repaired));assert(sum != read_be<U16>(&repaired,cover.transport+16)); }offset=offset+n; }
        }
    }
    fn test_target_quic() -> Unit {
        let mut initial: Buffer<U8>=buffer<U8>(1200);initial[0]=192;write_be<U32>(&mut initial,1,1);initial[5]=8;initial[14]=8;for i in 0..8 { initial[6+i]=cast<U8>(i+1);initial[15+i]=cast<U8>(i+11); }initial[24]=68;initial[25]=150;
        let mut packet: Buffer<U8>=udp_packet(slice(&initial,0,len(&initial)));let mut o: Options=default_options();automatic_options(&mut o);let p: Policy=zero<Policy>();let mut e: Engine=engine_new(move o,move p);
        let a: Dispatch=evaluate(&mut e,&mut packet,1,0,true,false,1000,false);let b: Dispatch=evaluate(&mut e,&mut packet,1,0,true,false,2000,false);assert(!a.consumed && !b.consumed);
        let target: Prefix=parse_prefix(utf8_bytes("192.0.2.1/32"));ip_add(&mut e.policy.quic_targets,&target);
        let c: Dispatch=evaluate(&mut e,&mut packet,1,0,true,false,3000,false);let d: Dispatch=evaluate(&mut e,&mut packet,1,0,true,false,4000,false);assert(!c.consumed && d.consumed && d.inbound);
    }
    fn test_metrics_truthfulness() -> Unit {
        let mut view: TrainingView=zero<TrainingView>();assert(training_view(utf8_bytes("phase=searching\nelapsed=61\nbudget=600\npassed=4\ntotal=4\nattributable=0\n"),&mut view));let absent: Buffer<U8>=training_caption(&view,false);assert(bytes_find(slice(&absent,0,len(&absent)),0,utf8_bytes("100%")) == len(&absent));
        assert(!training_view(utf8_bytes("passed=5\ntotal=4\n"),&mut view));assert(!training_view(utf8_bytes("elapsed=4294967295\n"),&mut view));
        assert(training_view(utf8_bytes("[DPIProfile]\nPassed=8\nTotal=8\nAttributable=2\nValidated=1\nControlsPassed=12\nControlsTotal=12\nMediaTested=0\n"),&mut view));let saved: Buffer<U8>=training_caption(&view,true);assert(bytes_find(slice(&saved,0,len(&saved)),0,utf8_bytes("100%")) < len(&saved));assert(view.validated == U32(1) && view.media == U32(0));
    }
    fn test_split_client_hello() -> Unit {
        let hello: Buffer<U8>=hello_for(utf8_bytes("www.youtube.com"));let mut o: Options=default_options();automatic_options(&mut o);let p: Policy=zero<Policy>();let mut e: Engine=engine_new(move o,move p);
        let mut first: Buffer<U8>=tcp_packet(slice(&hello,0,30),100,24,false);let a: Dispatch=evaluate(&mut e,&mut first,1,0,true,false,1000,false);assert(!a.consumed);
        let at: USize=automatic_index(&mut e,&first,1100);assert(at < len(&e.flows) && !e.flows[at].stopped && e.flows[at].host.length == 0);
        let mut second: Buffer<U8>=tcp_packet(slice(&hello,30,len(&hello)-30),130,24,false);let b: Dispatch=evaluate(&mut e,&mut second,1,0,true,false,1200,false);assert(e.flows[at].host.length == 15 && !e.flows[at].stopped && b.consumed);
    }
    fn main() -> Unit { test_gene_codec();test_evolution_population();test_gene_packet_invariants();test_target_quic();test_metrics_truthfulness();test_split_client_hello();print("DPI DLM 2.2: evolution, packet invariants, scoped QUIC and metrics tests passed"); }
}

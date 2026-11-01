//! Boundary regressions for raster drawing, font files and native parity.
use dlm_core::{font::{Font, FontTable}, gfx, vector, build_c_executable, default_c_compiler, emit_c, load_program_with_paths, lower_program, CheckPolicy, Checker, Runtime};
use std::{path::Path, process::Command};

#[test]
fn source_over_preserves_straight_rgba() {
    assert_eq!(gfx::blend(0, 0xFF000080), 0xFF000080);
    assert_eq!(gfx::blend(0x00FF0000, 0xFF000080), 0xFF000080);
    assert_eq!(gfx::blend(0x0000FF80, 0xFF000080), 0xAA0055C0);
    assert_eq!(gfx::blend(0x0000FFFF, 0xFF000080), 0x80007FFF);
    for a in 0..=255 {
        assert_eq!(gfx::blend(0, (0x12345600 | a) as u128), if a == 0 { 0 } else { 0x12345600 | a as u128 });
    }
}

#[test]
fn clipped_lines_match_the_original_bresenham_ties() {
    for x0 in 0i64..9 { for y0 in 0i64..9 { for x1 in 0i64..9 { for y1 in 0i64..9 {
        let mut expected = vec![0; 22]; expected[0] = 5; expected[1] = 4;
        let (mut x, mut y) = (x0, y0);
        let (dx, dy) = ((x1-x0).abs(), -(y1-y0).abs());
        let (sx, sy) = (if x0 < x1 {1} else {-1}, if y0 < y1 {1} else {-1});
        let mut err = dx + dy;
        loop {
            if x < 5 && y < 4 { expected[2 + (y*5+x) as usize] = 0xFFFFFFFF; }
            if x == x1 && y == y1 { break; }
            let e2 = 2*err;
            if e2 >= dy { err += dy; x += sx; }
            if e2 <= dx { err += dx; y += sy; }
        }
        assert_eq!(gfx::line(gfx::new(5,4,0).unwrap(), x0 as u128,y0 as u128,x1 as u128,y1 as u128,0xFFFFFFFF), expected);
    }}}}
}

#[test]
fn invisible_geometry_and_zero_area_images_have_bounded_work() {
    let image = gfx::new(1,1,0).unwrap();
    assert_eq!(gfx::text(image.clone(),0,0,&"M".repeat(100_000),255,64), [1,1,255]);
    assert_eq!(gfx::line(image.clone(), u128::MAX,u128::MAX,0,0,255), [1,1,255]);
    assert_eq!(gfx::circle(image.clone(),0,0,u128::MAX,255,true), [1,1,255]);
    assert_eq!(gfx::circle(image.clone(),u128::MAX,u128::MAX,u128::MAX,255,true), image);
    for (w,h) in [(0,u128::MAX),(u128::MAX,0)] {
        let empty = gfx::new(w,h,0).unwrap();
        assert_eq!(gfx::line(empty.clone(),0,0,u128::MAX,u128::MAX,255),empty);
        assert_eq!(gfx::fill_rect(empty.clone(),0,0,u128::MAX,u128::MAX,255),empty);
        assert_eq!(gfx::circle(empty.clone(),0,0,u128::MAX,255,true),empty);
        assert_eq!(gfx::blit(image.clone(),&empty,0,0),image);
        assert_eq!(vector::render_region(empty.clone(),&[],1,1,0,0,u128::MAX,u128::MAX),Some(empty));
    }
}

#[test]
fn vector_text_and_malformed_image_dimensions_do_not_overflow() {
    assert_eq!(vector::text_width("A", u128::MAX), (u128::MAX / 4) * 3 + 2);
    assert_eq!(vector::text_width("AAAA", u128::MAX), u128::MAX);
    let command = vector::text_command(u128::MAX, u128::MAX, u128::MAX, 16, &[0,255], "A");
    assert!(vector::render(&command,1,1,1,1).is_some());
    assert!(vector::svg(&command,u128::MAX,u128::MAX).is_some());
}

#[test]
fn shaping_is_linear_on_marks_and_respects_non_joiners() {
    let marks = vec![0x301; 100_000];
    assert_eq!(vector::visual(&marks), marks);
    let separated = vector::visual(&[0x628,0x200C,0x628]);
    assert_eq!(separated.iter().filter(|&&c| c == 0xFE8F).count(),2);
    let joined = vector::visual(&[0x628,0x200D,0x628]);
    assert!(joined.contains(&0xFE90) && joined.contains(&0xFE91));
}

fn put16(d: &mut [u8], at: usize, n: u16) { d[at..at+2].copy_from_slice(&n.to_be_bytes()); }
fn put32(d: &mut [u8], at: usize, n: u32) { d[at..at+4].copy_from_slice(&n.to_be_bytes()); }
fn table(data: &[u8], tag: &[u8;4]) -> (usize,usize) {
    for i in 0..7 { let rec = 12+16*i; if &data[rec..rec+4] == tag {
        return (rec,u32::from_be_bytes(data[rec+8..rec+12].try_into().unwrap()) as usize);
    }} panic!("missing table");
}
fn font_bytes() -> Vec<u8> {
    let mut head=vec![0;54];put16(&mut head,18,1000);put16(&mut head,50,1);
    let mut hhea=vec![0;36];put16(&mut hhea,4,800);put16(&mut hhea,6,(-200i16) as u16);put16(&mut hhea,34,3);
    let mut maxp=vec![0;6];put16(&mut maxp,4,3);
    let mut hmtx=vec![0;12];for i in 0..3 {put16(&mut hmtx,4*i,500);}
    let mut cmap=vec![0;40];put16(&mut cmap,2,1);put16(&mut cmap,4,3);put16(&mut cmap,6,10);put32(&mut cmap,8,12);
    put16(&mut cmap,12,12);put32(&mut cmap,16,28);put32(&mut cmap,24,1);put32(&mut cmap,28,65);put32(&mut cmap,32,65);put32(&mut cmap,36,2);
    let mut glyph=vec![0;30];put16(&mut glyph,0,1);put16(&mut glyph,10,2);glyph[14..17].fill(1);
    for (i,v) in [0i16,20,-20,0,0,20].iter().enumerate() {put16(&mut glyph,17+2*i,*v as u16);}
    let mut composite=vec![0;26];put16(&mut composite,0,0xFFFF);
    for (i,v) in [0x23,1,10,0,1,1,1,0].iter().enumerate() {put16(&mut composite,10+2*i,*v);}
    glyph.extend(composite);
    let mut loca=vec![0;16];put32(&mut loca,8,30);put32(&mut loca,12,56);
    let tables=[(b"head",head),(b"hhea",hhea),(b"maxp",maxp),(b"hmtx",hmtx),(b"cmap",cmap),(b"loca",loca),(b"glyf",glyph)];
    let mut data=vec![0;124];put32(&mut data,0,0x10000);put16(&mut data,4,7);
    for (i,(tag,bytes)) in tables.into_iter().enumerate() {
        let rec=12+16*i;let offset=data.len();data[rec..rec+4].copy_from_slice(tag);
        put32(&mut data,rec+8,offset as u32);put32(&mut data,rec+12,bytes.len() as u32);data.extend(bytes);
    }
    data
}

#[test]
fn composite_fonts_attach_points_and_reject_cycles() {
    let data=font_bytes();let f=Font::parse(data.clone()).unwrap();
    let outlines=f.outline(2);assert_eq!(outlines.len(),2);
    assert_eq!((outlines[0][1].x,outlines[0][1].y),(30,0));
    assert_eq!(outlines[1][0],outlines[0][1]);
    let (_,glyf)=table(&data,b"glyf");let mut cycle=data;put16(&mut cycle,glyf+30+12,2);
    assert!(Font::parse(cycle).unwrap().outline(2).is_empty());
    let fonts=FontTable{fonts:vec![f]};assert!(fonts.get(1).is_some());
    assert!(fonts.get((1u128 << usize::BITS)+1).is_none());
}

#[test]
fn font_tables_cannot_read_neighbouring_tables_or_wrap_glyph_ids() {
    let data=font_bytes();let (rec,_)=table(&data,b"head");let mut short=data.clone();put32(&mut short,rec+12,18);
    assert!(Font::parse(short).is_err());
    let (rec,_)=table(&data,b"glyf");let mut short=data.clone();put32(&mut short,rec+12,30);
    assert!(Font::parse(short).is_err());
    let (_,cmap)=table(&data,b"cmap");let mut overflow=data;
    put32(&mut overflow,cmap+32,67);put32(&mut overflow,cmap+36,u32::MAX);
    assert_eq!(Font::parse(overflow).unwrap().glyph_index(67),0);
}

#[test]
fn native_graphics_and_font_boundaries_match() {
    let dir=std::env::temp_dir().join(format!("dlm-review129-{}",std::process::id()));std::fs::create_dir_all(&dir).unwrap();
    let font=dir.join("fixture.ttf");std::fs::write(&font,font_bytes()).unwrap();
    let path=dir.join("main.dlm");
    let peer=std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port=peer.local_addr().unwrap().port();
    let worker=std::thread::spawn(move || {
        use std::io::Write;
        for _ in 0..2 { for request in ["", "GET / HTTP/1.0\r\n", "broken\r\n\r\n", "GET /ok HTTP/1.0\r\n\r\n"] {
            let (mut stream,_)=peer.accept().unwrap();stream.write_all(request.as_bytes()).unwrap();
        }}
    });
    let source=r#"module boundary
import std.vector
theory T extends StdVector {
    let m = 340282366920938463463374607431768211455
    let i = unwrap_or(gx_new(1,1,0),nv_empty())
    print_line(nat_to_text(unwrap_or(gx_get(gx_fill_rect(i,0,0,1,1,4278190208),0,0),0)))
    print_line(nat_to_text(unwrap_or(gx_get(gx_line(i,m,m,0,0,255),0,0),0)))
    print_line(nat_to_text(unwrap_or(gx_get(gx_circle(i,0,0,m,255,true),0,0),0)))
    let z = unwrap_or(gx_new(0,m,0),nv_empty())
    print_line(nat_to_text(nv_len(gx_fill_rect(z,0,0,m,m,255))))
    print_line(nat_to_text(nv_len(gx_blit(i,z,0,0))))
    print_line(nat_to_text(nv_len(unwrap_or(vx_render_region(z,nv_empty(),1,1,0,0,m,m),nv_empty()))))
    let scene = rrect(0,0,16,16,0,solid(255))
    let wide = unwrap_or(gx_new(1099511627776,0,0),nv_empty())
    print_line(nat_to_text(nv_len(unwrap_or(vx_render_region(wide,scene,1,1,0,0,m,m),nv_empty()))))
    print_line(nat_to_text(nv_len(unwrap_or(vx_render(scene,1099511627776,0,1,1),nv_empty()))))
    print_line(nat_to_text(gx_width(nvl(list(m,1)))))
    print_line(nat_to_text(vx_text_width("A",m)))
    print_line(nat_to_text(vx_text_width("AAAA",m)))
    print_line(if_then_else(is_some(vx_svg(nv_empty(),m,m)),"svg","bad"))
    let id = "ж +&?/"
    print_line(unwrap_or(url_decode(url_encode(id)),"bad"))
    print_line(if_then_else(is_some(index_of(to_html(default_theme(),Button(id,"x"),"demo"),url_encode(id))),"encoded","bad"))
    fn request() -> Text = match net_connect("127.0.0.1",PORT,1000) { err(e) => e, ok(c) =>
        let r = match ui_request(c) { ok(p) => p, err(e) => e } in let closed = net_close(c) in r }
    print_line(request())
    print_line(request())
    print_line(request())
    print_line(request())
    print_line(if_then_else(is_none(hit(layout(default_theme(), Scroll("clip",10,0,list(Button("hidden","x"))),0,0),2,20)),"clipped","bad"))
    let f = match file_font_load("FONT") { ok(n) => n, err(e) => 0 }
    print_line(if_then_else(vx_font_has(f,65),"glyph","missing"))
    print_line(if_then_else(vx_font_has(18446744073709551617,65),"alias","invalid"))
    let sc = vx_text_fonts(0,0,16000,0,nvl(list(f)),solid(255),"A")
    print_line(nat_to_text(gx_checksum(unwrap_or(vx_render(sc,80,80,1,1),nv_empty()))))
}
"#.replace("FONT",font.to_str().unwrap()).replace("PORT",&port.to_string());
    std::fs::write(&path,source).unwrap();let root=Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let program=load_program_with_paths(&path,&[root.join("lib")]).unwrap();
    let check=Checker::with_policy(CheckPolicy::trusted_only()).check_program(&program);assert!(check.ok(),"{:?}",check.diagnostics);
    let interpreted=Runtime::new().run_program(&program).unwrap().output;
    let ir=lower_program(&program,CheckPolicy::trusted_only()).unwrap();
    let c=emit_c(&ir);std::fs::write(dir.join("generated.c"),&c).unwrap();
    let exe=build_c_executable(&c,&dir.join("main.c"),&dir.join("main"),&default_c_compiler()).unwrap();
    let result=Command::new(exe).output().unwrap();assert!(result.status.success(),"{}",String::from_utf8_lossy(&result.stderr));
    let native:Vec<String>=String::from_utf8(result.stdout).unwrap().lines().map(str::to_string).collect();
    assert_eq!(interpreted,native);assert_eq!(&native[..6],["4278190208","255","255","2","3","2"]);
    assert_eq!(&native[6..8],["2","2"]);
    assert_eq!(&native[8..18],["0", "255211775190703847597530955573826158591", "340282366920938463463374607431768211455", "svg", "ж +&?/", "encoded", "empty HTTP request", "unexpected end of HTTP headers", "malformed HTTP request", "/ok"]);
    assert_eq!(&native[18..21],["clipped","glyph","invalid"]);
    worker.join().unwrap();
    std::fs::remove_dir_all(dir).unwrap();
}

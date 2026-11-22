module chordlike.live_editor
native {
    record Editor repr(C) { window: Window; width: USize; height: USize; }
    fn add_rect(scene: &mut Buffer<U64>, x: USize, y: USize, w: USize, h: USize, r: USize, color: U64) -> Unit {
        push(scene,U64(3));push(scene,cast<U64>(x)*U64(16));push(scene,cast<U64>(y)*U64(16));
        push(scene,cast<U64>(w)*U64(16));push(scene,cast<U64>(h)*U64(16));push(scene,cast<U64>(r)*U64(16));
        push(scene,U64(0));push(scene,color);
    }
    fn add_text(scene: &mut Buffer<U64>, x: USize, y: USize, size: USize, color: U64, label: Utf8) -> Unit {
        let chars: Slice<U8> = utf8_bytes(label);
        push(scene,U64(7));push(scene,cast<U64>(x)*U64(16));push(scene,cast<U64>(y)*U64(16));
        push(scene,cast<U64>(size)*U64(16));push(scene,U64(9));push(scene,U64(0));push(scene,color);
        push(scene,cast<U64>(len(chars)));
        for k in 0..len(chars) { push(scene,cast<U64>(chars[k])); }
    }
    fn add_percent(scene: &mut Buffer<U64>, x: USize, y: USize, value: USize, color: U64) -> Unit {
        let mut n: USize = value; if n > 100 { n = 100; }
        push(scene,U64(7));push(scene,cast<U64>(x)*U64(16));push(scene,cast<U64>(y)*U64(16));
        push(scene,U64(11)*U64(16));push(scene,U64(9));push(scene,U64(0));push(scene,color);
        push(scene,U64(4));push(scene,cast<U64>(48+n/100));push(scene,cast<U64>(48+(n/10)%10));
        push(scene,cast<U64>(48+n%10));push(scene,U64(37));
    }
    fn title_for(index: USize) -> Utf8 {
        if index == 0 { return "DSP Profile"; }
        if index == 1 { return "Custom FIR Length"; }
        if index == 2 { return "Oversampling"; }
        if index == 3 { return "Hugo 2 Filter"; }
        if index == 4 { return "Headphone Crossfeed"; }
        if index == 5 { return "Creative DAC Character"; }
        if index == 6 { return "Output Gain"; }
        if index == 7 { return "Dry Wet"; }
        if index == 8 { return "Pulse Array Model"; }
        if index == 9 { return "Reconstruction Phase"; }
        if index == 10 { return "LIVE Linear Optimization"; }
        if index == 11 { return "DAC Architecture"; }
        if index == 12 { return "DAC Element Mismatch"; }
        if index == 13 { return "Tube Valve Type"; }
        if index == 14 { return "Tube Output Topology"; }
        if index == 15 { return "Tube Drive"; }
        if index == 16 { return "Tube Bias"; }
        if index == 17 { return "Tube Cathode Sag"; }
        if index == 18 { return "Tube Stage Mix"; }
        if index == 19 { return "Valve Routing"; }
        if index == 20 { return "Low / Mid Crossover"; }
        if index == 21 { return "Mid / High Crossover"; }
        if index == 22 { return "Low Valve"; }
        if index == 23 { return "Low Topology"; }
        if index == 24 { return "Low Drive"; }
        if index == 25 { return "Low Bias"; }
        if index == 26 { return "Low Sag"; }
        if index == 27 { return "Low Wet"; }
        if index == 28 { return "Mid Valve"; }
        if index == 29 { return "Mid Topology"; }
        if index == 30 { return "Mid Drive"; }
        if index == 31 { return "Mid Bias"; }
        if index == 32 { return "Mid Sag"; }
        if index == 33 { return "Mid Wet"; }
        if index == 34 { return "High Valve"; }
        if index == 35 { return "High Topology"; }
        if index == 36 { return "High Drive"; }
        if index == 37 { return "High Bias"; }
        if index == 38 { return "High Sag"; }
        if index == 39 { return "High Wet"; }
        if index == 40 { return "Fast Tube CPU"; }
        if index == 41 { return "Auto Level Match"; }
        if index == 42 { return "True Peak Limiter"; }
        if index == 43 { return "Peak Ceiling"; }
        if index == 44 { return "WTA Focus"; }
        if index == 45 { return "Phase Motion"; }
        if index == 46 { return "HF Tone"; }
        if index == 47 { return "Pulse Color"; }
        return "Parameter";
    }
    fn ui_paint(e: &mut Editor, input: ForeignBuffer<F64>) -> Unit {
        let mut scene: Buffer<U64> = buffer<U64>(0);
        add_rect(&mut scene,0,0,1024,736,0,U64(269886207));
        add_rect(&mut scene,0,0,1024,68,0,U64(320678911));
        add_rect(&mut scene,0,66,1024,2,0,U64(1691740671));
        add_text(&mut scene,20,17,26,U64(3857907711),"CHORD-LIKE   LIVE");
        add_text(&mut scene,730,19,15,U64(1691740671),"DLM NATIVE EDITOR");
        add_text(&mut scene,22,54,9,U64(2662450687),"VECTOR ENGINE / 48 AUTOMATABLE PARAMETERS");
        for i in 0..48 {
            let col: USize = i % 4;
            let row: USize = i / 4;
            let x: USize = 18 + col * 250;
            let y: USize = 78 + row * 52;
            let v: F64 = input[i];
            let mut level: USize = 0;
            if v >= 1.0 { level = 100; } else { if v > 0.0 { level = cast<USize>(floor(v * 100.0 + 0.5)); } }
            let mut accent: U64 = U64(1691740671);
            if col == 1 { accent = U64(2262676991); }
            if col == 2 { accent = U64(3854072319); }
            if col == 3 { accent = U64(3047422463); }
            add_rect(&mut scene,x,y,238,46,7,U64(455555071));
            add_text(&mut scene,x+11,y+6,11,U64(3857907711),title_for(i));
            add_rect(&mut scene,x+11,y+31,180,5,3,U64(877092863));
            if level > 0 { add_rect(&mut scene,x+11,y+31,180*level/100,5,3,accent); }
            add_percent(&mut scene,x+195,y+28,level,U64(2662450687));
        }
        add_rect(&mut scene,0,712,1024,24,0,U64(320678911));
        add_text(&mut scene,22,720,10,U64(2662450687),"DLM 0.133  /  VST3  /  173 SAMPLES LATENCY");
        add_text(&mut scene,790,720,10,U64(1691740671),"DRAG SLIDERS TO EDIT");
        window_scene(&e.window,&scene,e.width,e.height,e.width,1024);
    }
    export unsafe fn live_ui_create(parent: USize) -> OpaqueHandle {
        let editor: Box<Editor> = box<Editor>(Editor(window_child(parent,"CHORD-like LIVE",1024,736),1024,736));
        return handle(into_raw(move editor));
    }
    export unsafe fn live_ui_handle(h: OpaqueHandle) -> USize {
        let e: Ptr<Editor> = handle_ptr<Editor>(h);
        return window_handle(&(*e).window);
    }
    export unsafe fn live_ui_resize(h: OpaqueHandle, width: USize, height: USize) -> Unit {
        let e: Ptr<Editor> = handle_ptr<Editor>(h);
        if width > 0 && height > 0 {
            (*e).width = width;(*e).height = height;
            window_resize(&(*e).window,width,height);
        }
    }
    export unsafe fn live_ui_draw(h: OpaqueHandle, values: Ptr<F64>, count: USize) -> Unit {
        if count < 48 { return; }
        let e: Ptr<Editor> = handle_ptr<Editor>(h);
        let input: ForeignBuffer<F64> = foreign(values,count);
        ui_paint(&mut (*e),move input);
    }
    export fn live_ui_hit(x: USize, y: USize, width: USize, height: USize) -> I32 {
        if width == 0 || height == 0 {return -1;}
        let u: USize=x*1024/width;
        let v: USize=y*736/height;
        if u < 18 || v < 78 { return -1; }
        let col: USize=(u-18)/250;
        let row: USize=(v-78)/52;
        if col>=4 || row>=12 {return -1;}
        if (u-18)%250>=238 || (v-78)%52>=46 {return -1;}
        return cast<I32>(row*4+col);
    }
    export fn live_ui_slider_value(x: USize, width: USize, index: USize) -> F64 {
        if width == 0 || index >= 48 { return 0.0; }
        let u: USize=x*1024/width;
        let left: USize=18+(index%4)*250+11;
        if u<=left { return 0.0; }
        if u>=left+180 { return 1.0; }
        return cast<F64>(u-left)/180.0;
    }
    export unsafe fn live_ui_destroy(h: OpaqueHandle) -> Unit {
        let e: Box<Editor> = from_raw(handle_ptr<Editor>(h));
    }
}

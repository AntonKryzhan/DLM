module native.embedded_editor
native {
    record Editor repr(C) { window: Window; }
    export unsafe fn editor_create(parent: USize, gpu: Bool) -> OpaqueHandle {
        let window: Window=window_child(parent,"DLM editor",64,64);
        let editor: Box<Editor>=box<Editor>(Editor(move window));
        if gpu { window_gpu(&editor.window); }
        return handle(into_raw(move editor));
    }
    export unsafe fn editor_attach(surface: USize) -> OpaqueHandle {
        let editor: Box<Editor>=box<Editor>(Editor(window_attach(surface,64,64)));
        return handle(into_raw(move editor));
    }
    export unsafe fn editor_handle(handle_: OpaqueHandle) -> USize { let e: Ptr<Editor>=handle_ptr<Editor>(handle_);return window_handle(&(*e).window); }
    export unsafe fn editor_resize(handle_: OpaqueHandle, width: USize, height: USize) -> Unit { let e: Ptr<Editor>=handle_ptr<Editor>(handle_);window_resize(&(*e).window,width,height); }
    export unsafe fn editor_focus(handle_: OpaqueHandle) -> Unit { let e: Ptr<Editor>=handle_ptr<Editor>(handle_);window_focus(&(*e).window); }
    export unsafe fn editor_gpu(handle_: OpaqueHandle) -> Unit { let e: Ptr<Editor>=handle_ptr<Editor>(handle_);window_gpu(&(*e).window); }
    export unsafe fn editor_dpi(handle_: OpaqueHandle) -> F64 { let e: Ptr<Editor>=handle_ptr<Editor>(handle_);return window_dpi(&(*e).window); }
    export unsafe fn editor_draw(handle_: OpaqueHandle, width: USize, height: USize, color: U64) -> Unit {
        let e: Ptr<Editor>=handle_ptr<Editor>(handle_);
        let scene: Array<U64,8>=array<Array<U64,8>>(U64(3),U64(0),U64(0),cast<U64>(width)*U64(16),cast<U64>(height)*U64(16),U64(0),U64(0),color);
        window_scene(&(*e).window,&scene,width,height,1,1);
    }
    export unsafe fn editor_input(handle_: OpaqueHandle, kind: USize, a: USize, b: USize, c: USize) -> Unit { let e: Ptr<Editor>=handle_ptr<Editor>(handle_);window_input(&(*e).window,kind,a,b,c); }
    export unsafe fn editor_poll(handle_: OpaqueHandle) -> U64 { let e: Ptr<Editor>=handle_ptr<Editor>(handle_);let event: Array<U64,4>=window_poll(&(*e).window,0);return event[0]; }
    export unsafe fn editor_pixel(handle_: OpaqueHandle, x: USize, y: USize) -> U64 { let e: Ptr<Editor>=handle_ptr<Editor>(handle_);let image: Buffer<U64>=window_read(&(*e).window);return image[2+y*cast<USize>(image[0])+x]; }
    export unsafe fn editor_destroy(handle_: OpaqueHandle) -> Unit { let e: Box<Editor>=from_raw(handle_ptr<Editor>(handle_)); }
}

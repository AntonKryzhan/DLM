module dpi.gui
import dpi.win32
import dpi.training_view
import dpi.startup_view

native {
    record WinClass repr(C) { size: U32; style: U32; procedure: FnPtr<OpaqueHandle,U32,USize,OpaqueHandle,I64>; class_extra: I32; window_extra: I32; instance: OpaqueHandle; icon: OpaqueHandle; cursor: OpaqueHandle; background: OpaqueHandle; menu: Ptr<U16>; name: Ptr<U16>; small_icon: OpaqueHandle; }
    record WinMessage repr(C) { window: OpaqueHandle; message: U32; wparam: USize; lparam: OpaqueHandle; time: U32; x: I32; y: I32; private_: U32; }
    record Rect repr(C) { left: I32; top: I32; right: I32; bottom: I32; }
    record CreateStruct repr(C) { parameter: OpaqueHandle; instance: OpaqueHandle; menu: OpaqueHandle; parent: OpaqueHandle; height: I32; width: I32; y: I32; x: I32; style: I32; name: Ptr<U16>; class_: Ptr<U16>; exstyle: U32; }
    record ShellExecute repr(C) { size: U32; mask: U32; window: OpaqueHandle; verb: Ptr<U16>; file: Ptr<U16>; parameters: Ptr<U16>; directory: Ptr<U16>; show: I32; instance: OpaqueHandle; idlist: Ptr<U8>; class_: Ptr<U16>; class_key: OpaqueHandle; hotkey: U32; icon: OpaqueHandle; process: OpaqueHandle; }
    record OpenFileName repr(C) { size: U32; owner: OpaqueHandle; instance: OpaqueHandle; filter: Ptr<U16>; custom_filter: Ptr<U16>; custom_size: U32; filter_index: U32; file: Ptr<U16>; file_capacity: U32; file_title: Ptr<U16>; title_capacity: U32; initial_dir: Ptr<U16>; title: Ptr<U16>; flags: U32; file_offset: U16; extension_offset: U16; default_extension: Ptr<U16>; userdata: OpaqueHandle; hook: OpaqueHandle; template_: Ptr<U16>; reserved: Ptr<U8>; reserved_word: U32; extended_flags: U32; }
    extern "C" fn GetOpenFileNameW(dialog: Ptr<OpenFileName>) -> I32 from "Win32";
    extern "C" fn GetSaveFileNameW(dialog: Ptr<OpenFileName>) -> I32 from "Win32";
    record PaintStruct repr(C) { dc: OpaqueHandle; erase: I32; rect: Rect; restore: I32; update: I32; reserved: Array<U8,32>; }
    record DrawItem repr(C) { type_: U32; control_id: U32; item_id: U32; action: U32; state: U32; window: OpaqueHandle; dc: OpaqueHandle; rect: Rect; data_: USize; }
    record MinMaxInfo repr(C) { reserved_x: I32; reserved_y: I32; max_x: I32; max_y: I32; position_x: I32; position_y: I32; minimum_x: I32; minimum_y: I32; track_x: I32; track_y: I32; }
    extern "C" fn RegisterClassExW(class_: Ptr<WinClass>) -> U16 from "Win32";
    extern "C" fn CreateWindowExW(exstyle: U32, class_: Ptr<U16>, title: Ptr<U16>, style: U32, x: I32, y: I32, width: I32, height: I32, parent: OpaqueHandle, menu: USize, instance: OpaqueHandle, parameter: OpaqueHandle) -> OpaqueHandle from "Win32";
    extern "C" fn DefWindowProcW(window: OpaqueHandle, message: U32, wparam: USize, lparam: OpaqueHandle) -> I64 from "Win32";
    extern "C" fn GetWindowLongPtrW(window: OpaqueHandle, index: I32) -> OpaqueHandle from "Win32";
    extern "C" fn SetWindowLongPtrW(window: OpaqueHandle, index: I32, value: OpaqueHandle) -> OpaqueHandle from "Win32";
    extern "C" fn SendMessageW(window: OpaqueHandle, message: U32, wparam: USize, lparam: USize) -> I64 from "Win32";
    extern "C" fn SetWindowTextW(window: OpaqueHandle, text: Ptr<U16>) -> I32 from "Win32";
    extern "C" fn OpenMutexW(access: U32, inherit: I32, name: Ptr<U16>) -> OpaqueHandle from "Win32";
    extern "C" fn EnableWindow(window: OpaqueHandle, enabled: I32) -> I32 from "Win32";
    extern "C" fn DestroyWindow(window: OpaqueHandle) -> I32 from "Win32";
    extern "C" fn ShowWindow(window: OpaqueHandle, command: I32) -> I32 from "Win32";
    extern "C" fn UpdateWindow(window: OpaqueHandle) -> I32 from "Win32";
    extern "C" fn GetMessageW(message: Ptr<WinMessage>, window: OpaqueHandle, minimum: U32, maximum: U32) -> I32 from "Win32";
    extern "C" fn TranslateMessage(message: Ptr<WinMessage>) -> I32 from "Win32";
    extern "C" fn DispatchMessageW(message: Ptr<WinMessage>) -> I64 from "Win32";
    extern "C" fn IsDialogMessageW(window: OpaqueHandle, message: Ptr<WinMessage>) -> I32 from "Win32";
    extern "C" fn PostQuitMessage(code: I32) -> Unit from "Win32";
    extern "C" fn LoadCursorW(instance: OpaqueHandle, id: USize) -> OpaqueHandle from "Win32";
    extern "C" fn GetClientRect(window: OpaqueHandle, rect: Ptr<Rect>) -> I32 from "Win32";
    extern "C" fn MoveWindow(window: OpaqueHandle, x: I32, y: I32, width: I32, height: I32, repaint: I32) -> I32 from "Win32";
    extern "C" fn SetTimer(window: OpaqueHandle, id: USize, timeout: U32, callback: OpaqueHandle) -> USize from "Win32";
    extern "C" fn KillTimer(window: OpaqueHandle, id: USize) -> I32 from "Win32";
    extern "C" fn ShellExecuteExW(execute: Ptr<ShellExecute>) -> I32 from "Win32";
    extern "C" fn TerminateProcess(process: OpaqueHandle, code: U32) -> I32 from "Win32";
    extern "C" fn GetSystemMetrics(index: I32) -> I32 from "Win32";
    extern "C" fn GetDpiForWindow(window: OpaqueHandle) -> U32 from "Win32";
    extern "C" fn InvalidateRect(window: OpaqueHandle, rect: Ptr<Rect>, erase: I32) -> I32 from "Win32";
    extern "C" fn CreateFontW(height: I32, width: I32, escapement: I32, orientation: I32, weight: I32, italic: U32, underline: U32, strike: U32, charset: U32, output: U32, clip: U32, quality: U32, pitch: U32, face: Ptr<U16>) -> OpaqueHandle from "Win32";
    extern "C" fn DeleteObject(object: OpaqueHandle) -> I32 from "Win32" guarantees(NoUnwind);
    extern "C" fn CreateSolidBrush(color: U32) -> OpaqueHandle from "Win32";
    extern "C" fn GetStockObject(id: I32) -> OpaqueHandle from "Win32";
    extern "C" fn SelectObject(dc: OpaqueHandle, object: OpaqueHandle) -> OpaqueHandle from "Win32";
    extern "C" fn SetTextColor(dc: OpaqueHandle, color: U32) -> U32 from "Win32";
    extern "C" fn SetBkMode(dc: OpaqueHandle, mode: I32) -> I32 from "Win32";
    extern "C" fn RoundRect(dc: OpaqueHandle, left: I32, top: I32, right: I32, bottom: I32, ellipse_width: I32, ellipse_height: I32) -> I32 from "Win32";
    extern "C" fn DrawTextW(dc: OpaqueHandle, text: Ptr<U16>, count: I32, rect: Ptr<Rect>, format: U32) -> I32 from "Win32";
    extern "C" fn FillRect(dc: OpaqueHandle, rect: Ptr<Rect>, brush: OpaqueHandle) -> I32 from "Win32";
    extern "C" fn DrawFocusRect(dc: OpaqueHandle, rect: Ptr<Rect>) -> I32 from "Win32";
    extern "C" fn BeginPaint(window: OpaqueHandle, paint: Ptr<PaintStruct>) -> OpaqueHandle from "Win32";
    extern "C" fn EndPaint(window: OpaqueHandle, paint: Ptr<PaintStruct>) -> I32 from "Win32";
    extern "C" fn DwmSetWindowAttribute(window: OpaqueHandle, attribute: U32, value: Ptr<U32>, size: U32) -> I32 from "Win32";
    unsafe fn delete_object(value: &mut OpaqueHandle) -> Unit guarantees(NoUnwind) { DeleteObject(*value); }
    record Control repr(C) { window: OpaqueHandle; id: USize; x: I32; y: I32; width: I32; height: I32; }
    record Gui repr(C) {
        controls: Buffer<Control>; window: OpaqueHandle; font: OpaqueHandle; title_font: OpaqueHandle; status_font: OpaqueHandle;
        background: OpaqueHandle; card: OpaqueHandle; accent: OpaqueHandle; secondary: OpaqueHandle;
        process: Resource<OpaqueHandle,close_handle>; preflight: Resource<OpaqueHandle,close_handle>;
        note: Buffer<U8>; phase: U32; deadline: U64; dpi: I32; training: Resource<OpaqueHandle,close_handle>; view: TrainingView; history: Bool; session: Buffer<U8>;
    }
    unsafe fn control(g: &Gui, id: USize) -> OpaqueHandle { for i in 0..len(&g.controls) { if g.controls[i].id == id { return g.controls[i].window; } }return zero<OpaqueHandle>(); }
    fn rgb(red: U32, green: U32, blue: U32) -> U32 { return red | (green << U32(8)) | (blue << U32(16)); }
    unsafe fn ui_log(g: &mut Gui, text: Slice<U8>) -> Unit { g.note=bytes_copy(text);InvalidateRect(g.window,null<Rect>(),1); }
    unsafe fn scale(g: &Gui, value: I32) -> I32 { return MulDiv(value,g.dpi,96); }
    unsafe fn width_at(g: &Gui, value: I32) -> I32 { let mut rect: Rect=zero<Rect>();GetClientRect(g.window,as_raw(borrowed_ptr(&mut rect)));return MulDiv(value,rect.right,1000); }
    unsafe fn rounded(dc: OpaqueHandle, rect: Rect, brush: OpaqueHandle, radius: I32) -> Unit {
        let old_brush: OpaqueHandle=SelectObject(dc,brush);let old_pen: OpaqueHandle=SelectObject(dc,GetStockObject(8));
        RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);SelectObject(dc,old_pen);SelectObject(dc,old_brush);
    }
    unsafe fn paint_text(g: &Gui, dc: OpaqueHandle, text: Slice<U8>, font: OpaqueHandle, color: U32, x: I32, y: I32, width: I32, height: I32) -> Unit {
        let old: OpaqueHandle=SelectObject(dc,font);SetBkMode(dc,1);SetTextColor(dc,color);let w: Buffer<U16>=wide(text);
        let mut rect: Rect=Rect(width_at(g,x),scale(g,y),width_at(g,x+width),scale(g,y+height));DrawTextW(dc,data(&w),-1,as_raw(borrowed_ptr(&mut rect)),0x810);SelectObject(dc,old);
    }
    unsafe fn gui_paint(g: &Gui) -> Unit {
        let mut paint: PaintStruct=zero<PaintStruct>();let dc: OpaqueHandle=BeginPaint(g.window,as_raw(borrowed_ptr(&mut paint)));let mut rect: Rect=zero<Rect>();GetClientRect(g.window,as_raw(borrowed_ptr(&mut rect)));FillRect(dc,as_raw(borrowed_ptr(&mut rect)),g.background);
        rounded(dc,Rect(width_at(g,32),scale(g,27),width_at(g,94),scale(g,81)),g.accent,scale(g,16));
        paint_text(g,dc,utf8_bytes("N"),g.title_font,rgb(255,255,255),47,34,60,44);
        paint_text(g,dc,utf8_bytes("NoDPI"),g.title_font,rgb(244,247,252),116,23,450,45);
        paint_text(g,dc,utf8_bytes("Автоматический подбор • DLM 0.133.0"),g.font,rgb(147,163,187),119,72,420,24);
        paint_text(g,dc,utf8_bytes("Сервис для подбора и проверки"),g.font,rgb(147,163,187),570,35,380,26);
        rounded(dc,Rect(width_at(g,28),scale(g,120),width_at(g,972),scale(g,380)),g.card,scale(g,20));
        let mut title: Utf8="Обработка выключена";let mut detail: Utf8="Нажмите «Включить». Программа сначала проверит исходное подключение.";let mut color: U32=rgb(244,247,252);
        if g.phase == U32(1) { title="Проверяю подключение…";detail="Движок остановлен. Проверяются российские и внешний контрольный сайт."; }
        if g.phase == U32(2) { title="Подготавливаю обработку…";detail="Подтвердите запрос Windows. Ожидаю готовности движка."; }
        if g.phase == U32(3) { title="Обработка включена";detail="Стратегии выбираются автоматически. Проверьте Telegram, видео YouTube и голос Discord.";color=rgb(81,219,167); }
        if g.phase == U32(5) { title="Поиск стратегии";detail="Сравниваю методы на вашем подключении."; }
        if g.phase == U32(6) { title="Ошибка запуска или проверки";detail="Нажмите «Проверить сервис»: откроется диагностика драйвера.";color=rgb(255,192,103); }
        if g.phase == U32(5) && g.view.phase == U32(5) { title="Проверяю драйвер…";detail="Проверка WinDivert до длительных сетевых проб; перехват не включён."; }
        if g.phase == U32(5) && g.view.phase == U32(6) { title="Исходные сетевые пробы…";detail="Обучение ещё не началось: собирается результат без обработки."; }
        if g.phase == U32(4) { title="Нужно проверить соединение";detail="Нажмите «Проверить сервис», затем «Собрать отчёт»: отчёт покажет этап сбоя.";color=rgb(255,192,103); }
        paint_text(g,dc,utf8_bytes(title),g.status_font,color,60,147,865,46);if g.phase != U32(4) && g.phase != U32(6) && (g.view.total > U32(0) || g.phase == U32(5)) { let caption: Buffer<U8>=training_caption(&g.view,g.history);paint_text(g,dc,slice(&caption,0,len(&caption)),g.font,rgb(166,181,202),60,207,860,58); }else { paint_text(g,dc,utf8_bytes(detail),g.font,rgb(166,181,202),60,207,860,53); }
        if g.view.budget > U32(0) || g.view.total > U32(0) { let clock: Buffer<U8>=training_clock(&g.view);paint_text(g,dc,slice(&clock,0,len(&clock)),g.font,rgb(147,163,187),60,342,860,38); }else { paint_text(g,dc,utf8_bytes("DNS и локальная сеть проходят без преобразований."),g.font,rgb(147,163,187),60,346,860,25); }
        if g.view.budget > U32(0) { let amount: U32=min_u32(g.view.elapsed,g.view.budget);let extent: I32=MulDiv(width_at(g,880),cast<I32>(amount),cast<I32>(g.view.budget));rounded(dc,Rect(width_at(g,60),scale(g,385),width_at(g,940),scale(g,391)),g.secondary,scale(g,6));if extent > I32(0) { rounded(dc,Rect(width_at(g,60),scale(g,385),width_at(g,60)+extent,scale(g,391)),g.accent,scale(g,6)); } }
        rounded(dc,Rect(width_at(g,28),scale(g,400),width_at(g,972),scale(g,540)),g.card,scale(g,20));
        paint_text(g,dc,slice(&g.note,0,len(&g.note)),g.font,rgb(147,163,187),32,556,940,58);
        paint_text(g,dc,utf8_bytes("2.4  /  Журналы сохраняются локально. Настройки сети Windows не меняются."),g.font,rgb(115,133,157),32,626,940,35);
        EndPaint(g.window,as_raw(borrowed_ptr(&mut paint)));
    }
    unsafe fn gui_layout(g: &Gui) -> Unit {
        for i in 0..len(&g.controls) { let c: Control=g.controls[i];MoveWindow(c.window,width_at(g,c.x),scale(g,c.y),width_at(g,c.width),scale(g,c.height),1); }InvalidateRect(g.window,null<Rect>(),1);
    }
    unsafe fn add_button(g: &mut Gui, title: Utf8, id: USize, x: I32, y: I32, width: I32, height: I32) -> Unit {
        let class_: Buffer<U16>=wide(utf8_bytes("BUTTON"));let text: Buffer<U16>=wide(utf8_bytes(title));let child: OpaqueHandle=CreateWindowExW(0,data(&class_),data(&text),0x5001000b,0,0,10,10,g.window,id,GetModuleHandleW(null<U16>()),zero<OpaqueHandle>());
        assert(handle_valid(child));SendMessageW(child,0x30,address(g.font),0);push(&mut g.controls,Control(child,id,x,y,width,height));
    }
    unsafe fn selected_target(g: &Gui) -> U32 {
        let selected: I64=SendMessageW(control(g,20),0x147,0,0);if selected < I64(0) || selected > I64(6) { return 5; }return cast<U32>(selected);
    }
    unsafe fn gui_build(g: &mut Gui) -> Unit {
        add_button(g,"Включить",10,60,280,330,48);add_button(g,"Обучать 10 минут",16,430,280,500,48);
        add_button(g,"Проверить сервис",13,52,416,280,46);add_button(g,"Собрать отчёт",14,356,416,280,46);add_button(g,"Журнал",15,660,416,280,46);
        add_button(g,"Сохранить профиль",17,52,478,430,42);add_button(g,"Загрузить профиль",18,512,478,430,42);
        let combo_class: Buffer<U16>=wide(utf8_bytes("COMBOBOX"));let empty: Buffer<U16>=wide(utf8_bytes(""));let combo: OpaqueHandle=CreateWindowExW(0,data(&combo_class),data(&empty),0x50210203,0,0,10,10,g.window,20,GetModuleHandleW(null<U16>()),zero<OpaqueHandle>());
        assert(handle_valid(combo));SendMessageW(combo,0x30,address(g.font),0);let names: Array<Utf8,7>=array<Array<Utf8,7>>("YouTube","Discord","YouTube + Discord","Telegram Web + Desktop","YouTube + Discord + Telegram","MWO + CDN / Cloudflare / Azure","Все сервисы");
        for i in 0..7 { let name: Buffer<U16>=wide(utf8_bytes(names[i]));SendMessageW(combo,0x143,0,address(data(&name))); }SendMessageW(combo,0x14e,5,0);push(&mut g.controls,Control(combo,20,570,69,380,200));gui_layout(g);
    }
    unsafe fn gui_draw_button(g: &Gui, item: &DrawItem) -> Unit {
        let mut brush: OpaqueHandle=g.secondary;let mut color: U32=rgb(229,237,249);if item.control_id == U32(10) { brush=g.accent;color=rgb(255,255,255); }
        if (item.state & U32(4)) != U32(0) { brush=g.card;color=rgb(115,133,157); }if (item.state & U32(1)) != U32(0) { brush=g.card; }
        rounded(item.dc,item.rect,brush,scale(g,12));let old: OpaqueHandle=SelectObject(item.dc,g.font);SetBkMode(item.dc,1);SetTextColor(item.dc,color);
        let mut text: Utf8="Включить";if item.control_id == U32(10) && (g.phase == U32(1) || g.phase == U32(2)) { text="Отменить"; }else if item.control_id == U32(10) && handle_valid(*resource_get(&g.process)) { text="Отключить"; }
        if item.control_id == U32(13) { text="Проверить сервис";if g.phase == U32(6) { text="Проверить драйвер"; } }if item.control_id == U32(14) { text="Собрать отчёт"; }if item.control_id == U32(15) { text="Журнал"; }if item.control_id == U32(16) { text="Обучать 10 минут";if handle_valid(*resource_get(&g.training)) { text="Остановить поиск"; } }if item.control_id == U32(17) { text="Сохранить профиль"; }if item.control_id == U32(18) { text="Загрузить профиль"; }
        let w: Buffer<U16>=wide(utf8_bytes(text));let mut rect: Rect=item.rect;DrawTextW(item.dc,data(&w),-1,as_raw(borrowed_ptr(&mut rect)),0x825);
        if (item.state & U32(16)) != U32(0) { rect.left=rect.left+I32(4);rect.top=rect.top+I32(4);rect.right=rect.right-I32(4);rect.bottom=rect.bottom-I32(4);DrawFocusRect(item.dc,as_raw(borrowed_ptr(&mut rect))); }SelectObject(item.dc,old);
    }
    unsafe fn launch(g: &Gui, filename: Slice<U8>, parameters: Slice<U8>, elevate: Bool, show: I32) -> OpaqueHandle {
        let own: Buffer<U8>=executable_path();let dir: Buffer<U8>=directory_path(slice(&own,0,len(&own)));let file: Buffer<U16>=wide(filename);let directory: Buffer<U16>=wide(slice(&dir,0,len(&dir)));let args: Buffer<U16>=wide(parameters);
        let mut verb_name: Utf8="open";if elevate { verb_name="runas"; }let verb: Buffer<U16>=wide(utf8_bytes(verb_name));let mut execute: ShellExecute=zero<ShellExecute>();
        execute.size=cast<U32>(size_of<ShellExecute>());execute.mask=0x40;execute.window=g.window;execute.verb=data(&verb);execute.file=data(&file);execute.parameters=data(&args);execute.directory=data(&directory);execute.show=show;
        if ShellExecuteExW(as_raw(borrowed_ptr(&mut execute))) == I32(0) { return zero<OpaqueHandle>(); }return execute.process;
    }
    unsafe fn local_path(name: Utf8) -> Buffer<U8> { let own: Buffer<U8>=executable_path();let mut path: Buffer<U8>=directory_path(slice(&own,0,len(&own)));push(&mut path,U8(92));bytes_append(&mut path,utf8_bytes(name));return move path; }
    unsafe fn running_ui(g: &Gui) -> Unit {
        let mut label: Utf8="Включить";if handle_valid(*resource_get(&g.process)) { label="Отключить"; }if g.phase == U32(1) || g.phase == U32(2) { label="Отменить"; }
        let text: Buffer<U16>=wide(utf8_bytes(label));SetWindowTextW(control(g,10),data(&text));let mut enable: I32=1;if g.phase == U32(1) || g.phase == U32(2) { enable=0; }if handle_valid(*resource_get(&g.training)) { enable=0; }let mut toggle: I32=1;if handle_valid(*resource_get(&g.training)) { toggle=0; }EnableWindow(control(g,10),toggle);EnableWindow(control(g,13),enable);EnableWindow(control(g,20),enable);EnableWindow(control(g,18),enable);InvalidateRect(control(g,10),null<Rect>(),1);InvalidateRect(g.window,null<Rect>(),1);
    }
    unsafe fn gui_stop(g: &mut Gui) -> Unit {
        let probe: OpaqueHandle=*resource_get(&g.preflight);if handle_valid(probe) { TerminateProcess(probe,3);WaitForSingleObject(probe,1000);g.preflight=resource(zero<OpaqueHandle>()); }
        let process: OpaqueHandle=*resource_get(&g.process);if handle_valid(process) {
            if !signal_stop(GetProcessId(process)) || WaitForSingleObject(process,3000) == U32(258) { TerminateProcess(process,3);WaitForSingleObject(process,1000); }
            if WaitForSingleObject(process,0) != U32(0) { g.phase=4;ui_log(g,utf8_bytes("Движок ещё не подтвердил остановку. Повторите отключение или закройте окно."));running_ui(g);return; }g.process=resource(zero<OpaqueHandle>());
        }g.phase=0;running_ui(g);ui_log(g,utf8_bytes("Обработка остановлена; перехват пакетов снят."));
    }
    unsafe fn start_engine(g: &mut Gui) -> Unit {
        let path: Buffer<U8>=local_path("NoDPI.exe");let mut args: Buffer<U8>=bytes_copy(utf8_bytes("--config=config.ini --automatic --parent-pid="));append_number(&mut args,cast<U64>(GetCurrentProcessId()));let profile: Buffer<U8>=read_file(utf8_bytes("active.profile"),65536);if len(&profile) > 0 { bytes_append(&mut args,utf8_bytes(" --strategy-file=active.profile")); }
        g.phase=2;running_ui(g);UpdateWindow(g.window);g.process=resource(launch(g,slice(&path,0,len(&path)),slice(&args,0,len(&args)),true,0));
        if !handle_valid(*resource_get(&g.process)) { g.phase=4;ui_log(g,utf8_bytes("Запуск отменён или отсутствует NoDPI.exe. Подробности — в журнале."));running_ui(g);return; }g.deadline=GetTickCount64()+U64(25000);
    }
    unsafe fn gui_start(g: &mut Gui) -> Unit {
        if handle_valid(*resource_get(&g.process)) || handle_valid(*resource_get(&g.preflight)) { return; }
        let lock_name: Buffer<U16>=wide(utf8_bytes("Local\\NoDPI.Engine.v5"));SetLastError(0);let existing: Resource<OpaqueHandle,close_handle>=resource(OpenMutexW(0x100000,0,data(&lock_name)));
        if handle_valid(*resource_get(&existing)) || GetLastError() == U32(5) { g.phase=4;ui_log(g,utf8_bytes("Другой движок NoDPI уже запущен. Остановите его перед включением этого окна."));running_ui(g);return; }
        let path: Buffer<U8>=local_path("Prepare-Auto.ps1");let mut args: Buffer<U8>=bytes_copy(utf8_bytes("-NoProfile -ExecutionPolicy Bypass -File \""));bytes_append(&mut args,slice(&path,0,len(&path)));bytes_append(&mut args,utf8_bytes("\" -CheckBaseline"));g.preflight=resource(launch(g,utf8_bytes("powershell.exe"),slice(&args,0,len(&args)),false,0));
        if !handle_valid(*resource_get(&g.preflight)) { g.phase=4;ui_log(g,utf8_bytes("Нет NoDPIProbe.exe. Распакуйте все файлы обновления."));running_ui(g);return; }
        g.phase=1;g.deadline=GetTickCount64()+U64(45000);ui_log(g,utf8_bytes("Проверка выполняется без NoDPI. При общем отказе перехват не включается."));running_ui(g);
    }
    unsafe fn ready(pid: U32) -> Bool {
        let mut name: Buffer<U8>=bytes_copy(utf8_bytes("Local\\NoDPI.Ready."));append_number(&mut name,cast<U64>(pid));let w: Buffer<U16>=wide(slice(&name,0,len(&name)));
        let owner: Resource<OpaqueHandle,close_handle>=resource(OpenEventW(0x100000,0,data(&w)));let event: OpaqueHandle=*resource_get(&owner);return handle_valid(event) && WaitForSingleObject(event,0) == U32(0);
    }
    unsafe fn gui_poll(g: &mut Gui) -> Unit {
        poll_training(g);
        let probe: OpaqueHandle=*resource_get(&g.preflight);if handle_valid(probe) {
            let mut code: U32=259;if GetExitCodeProcess(probe,as_raw(borrowed_ptr(&mut code))) == I32(0) { code=2; }
            if code != U32(259) { g.preflight=resource(zero<OpaqueHandle>());if code == U32(0) { start_engine(g); }else { g.phase=4;ui_log(g,utf8_bytes("Исходный HTTPS недоступен. Проверьте VPN / Kill Switch / DNS через «Проверить сеть». Движок не включён."));running_ui(g); } }
            else if GetTickCount64() >= g.deadline { gui_stop(g);g.phase=4;ui_log(g,utf8_bytes("Исходная проверка превысила время ожидания. Нужен отчёт проверки сети.")); }
        }
        let process: OpaqueHandle=*resource_get(&g.process);if handle_valid(process) {
            let mut code: U32=259;if GetExitCodeProcess(process,as_raw(borrowed_ptr(&mut code))) == I32(0) { code=2; }
            if code != U32(259) { let pid: U32=GetProcessId(process);g.process=resource(zero<OpaqueHandle>());g.phase=6;
                let mut path: Buffer<U8>=bytes_copy(utf8_bytes("logs/startup-"));append_number(&mut path,cast<U64>(pid));bytes_append(&mut path,utf8_bytes(".status"));let text: Buffer<U8>=read_file(slice(&path,0,len(&path)),16384);let info: StartupView=startup_view(slice(&text,0,len(&text)));
                let mut line: Buffer<U8>=bytes_copy(utf8_bytes("Движок завершён, код "));append_number(&mut line,cast<U64>(code));bytes_append(&mut line,utf8_bytes(". Откройте Check-Driver.cmd и соберите отчёт."));if info.found && info.driver && !info.ready && info.error > U32(0) { line=startup_caption(info.error); }ui_log(g,slice(&line,0,len(&line)));running_ui(g);
            }
            else if g.phase == U32(2) && ready(GetProcessId(process)) { g.phase=3;running_ui(g);ui_log(g,utf8_bytes("Движок готов. Пробы сайта не заменяют воспроизведение видео и голосовой звонок.")); }
            else if g.phase == U32(2) && GetTickCount64() >= g.deadline { gui_stop(g);g.phase=6;ui_log(g,utf8_bytes("Движок не подтвердил готовность и остановлен. Откройте журнал.")); }
        }
    }
    unsafe fn auxiliary(g: &mut Gui, filename: Utf8, parameters: Slice<U8>) -> Unit {
        let path: Buffer<U8>=local_path(filename);let owner: Resource<OpaqueHandle,close_handle>=resource(launch(g,slice(&path,0,len(&path)),parameters,bytes_equal(utf8_bytes(filename),utf8_bytes("Check-Driver.cmd")),1));
        if !handle_valid(*resource_get(&owner)) { ui_log(g,utf8_bytes("Не удалось открыть файл. Проверьте состав обновления.")); }
    }
    unsafe fn training_stop_path(g: &Gui) -> Buffer<U8> { let mut path: Buffer<U8>=bytes_copy(utf8_bytes("logs/training-"));bytes_append(&mut path,slice(&g.session,0,len(&g.session)));bytes_append(&mut path,utf8_bytes(".stop"));return move path; }
    unsafe fn stop_training(g: &mut Gui, wait: Bool) -> Unit {
        let process: OpaqueHandle=*resource_get(&g.training);if !handle_valid(process) { return; }let path: Buffer<U8>=training_stop_path(g);write_file(slice(&path,0,len(&path)),utf8_bytes("stop\n"));
        ui_log(g,utf8_bytes("Останавливаю поиск. Завершается текущая проба и снимается перехват."));
        if wait { if WaitForSingleObject(process,6000) == U32(258) { TerminateProcess(process,3);WaitForSingleObject(process,1000); }g.training=resource(zero<OpaqueHandle>());g.phase=0;running_ui(g); }
    }
    unsafe fn start_training(g: &mut Gui) -> Unit {
        gui_stop(g);if handle_valid(*resource_get(&g.process)) || handle_valid(*resource_get(&g.training)) { return; }
        g.session=buffer<U8>(0);append_number(&mut g.session,cast<U64>(GetCurrentProcessId()));push(&mut g.session,U8(45));append_number(&mut g.session,GetTickCount64());
        let path: Buffer<U8>=local_path("Train.ps1");let mut args: Buffer<U8>=bytes_copy(utf8_bytes("-NoProfile -ExecutionPolicy Bypass -File \""));bytes_append(&mut args,slice(&path,0,len(&path)));bytes_append(&mut args,utf8_bytes("\" -Seconds 600 -Target "));append_number(&mut args,cast<U64>(selected_target(g)));bytes_append(&mut args,utf8_bytes(" -OwnerPid "));append_number(&mut args,cast<U64>(GetCurrentProcessId()));bytes_append(&mut args,utf8_bytes(" -Session "));bytes_append(&mut args,slice(&g.session,0,len(&g.session)));
        g.training=resource(launch(g,utf8_bytes("powershell.exe"),slice(&args,0,len(&args)),true,0));
        if !handle_valid(*resource_get(&g.training)) { g.phase=4;ui_log(g,utf8_bytes("Обучение не запущено. Возможна отмена UAC или отсутствие Train.ps1."));return; }
        g.phase=5;g.history=false;g.view=zero<TrainingView>();g.view.budget=600;g.view.target=selected_target(g);running_ui(g);ui_log(g,utf8_bytes("Контроль без обработки, кандидаты, повторная проверка выбранного сервиса. Telegram проверяется по HTTPS, WSS и MTProto TCP."));
    }
    unsafe fn poll_training(g: &mut Gui) -> Unit {
        let process: OpaqueHandle=*resource_get(&g.training);if !handle_valid(process) { return; }
        let text: Buffer<U8>=read_file(utf8_bytes("logs/training.status"),16384);let mut session: Buffer<U8>=bytes_copy(utf8_bytes("session="));bytes_append(&mut session,slice(&g.session,0,len(&g.session)));
        let mut matched: Bool=false;
        if bytes_find(slice(&text,0,len(&text)),0,slice(&session,0,len(&session))) < len(&text) { let mut view: TrainingView=zero<TrainingView>();if training_view(slice(&text,0,len(&text)),&mut view) { g.view=view;matched=true; } }
        let mut code: U32=259;if GetExitCodeProcess(process,as_raw(borrowed_ptr(&mut code))) == I32(0) { code=2; }
        if code != U32(259) {
            g.training=resource(zero<OpaqueHandle>());g.phase=0;if code != U32(0) || !matched || g.view.phase == U32(4) { g.phase=6;ui_log(g,utf8_bytes("Поиск завершён с ошибкой. Соберите отчёт: он содержит training.status и результаты проб.")); }
            else if g.view.validated == U32(1) { ui_log(g,utf8_bytes("Улучшение сетевых проб подтверждено. Профиль активирован для следующего включения; видео и голос проверьте отдельно.")); }
            else { ui_log(g,utf8_bytes("Подтверждённого улучшения нет. Прежний активный профиль сохранён; можно сохранить исследованный кандидат отдельно.")); }running_ui(g);
            if matched { let at: USize=bytes_find(slice(&text,0,len(&text)),0,utf8_bytes("message="));if at < len(&text) { let end: USize=byte_find(slice(&text,0,len(&text)),at,U8(10));let mut note: Buffer<U8>=bytes_copy(slice(&g.note,0,len(&g.note)));bytes_append(&mut note,utf8_bytes("\n"));bytes_append(&mut note,slice(&text,at+8,min_size(end-at-8,300)));ui_log(g,slice(&note,0,len(&note))); } }
        }InvalidateRect(g.window,null<Rect>(),1);InvalidateRect(control(g,16),null<Rect>(),1);
    }
    unsafe fn profile_dialog(g: &mut Gui, save: Bool) -> Unit {
        let mut file: Buffer<U16>=buffer<U16>(32768);let extension: Buffer<U16>=wide(utf8_bytes("profile"));let filter: Buffer<U16>=wide(utf8_bytes("Профиль NoDPI (*.profile)\0*.profile\0Все файлы\0*.*\0\0"));
        let mut dialog: OpenFileName=zero<OpenFileName>();dialog.size=cast<U32>(size_of<OpenFileName>());dialog.owner=g.window;dialog.filter=data(&filter);dialog.file=data(&mut file);dialog.file_capacity=32768;dialog.default_extension=data(&extension);dialog.flags=0x81808;
        let mut selected: I32=0;if save { dialog.flags=0x8080a;selected=GetSaveFileNameW(as_raw(borrowed_ptr(&mut dialog))); }else { selected=GetOpenFileNameW(as_raw(borrowed_ptr(&mut dialog))); }if selected == I32(0) { return; }
        let path: Buffer<U8>=utf8_wide(data(&file),wide_length(data(&file)));let mut contents: Buffer<U8>=buffer<U8>(0);
        if save { if !g.history { contents=read_file(utf8_bytes("logs/training-best.profile"),65536); }if len(&contents) == 0 { contents=read_file(utf8_bytes("active.profile"),65536); } }else { contents=read_file(slice(&path,0,len(&path)),65536); }
        let mut gene: Gene=gene_default();let mut target: U32=0;if !profile_gene(slice(&contents,0,len(&contents)),&mut gene,&mut target) { ui_log(g,utf8_bytes("Нет корректного профиля для этой операции. Сначала выполните поиск или выберите файл Schema=1."));return; }
        if save { if write_file(slice(&path,0,len(&path)),slice(&contents,0,len(&contents))) { ui_log(g,utf8_bytes("Профиль и измеренная оценка проб сохранены. Это данные параметров, не исполняемый код.")); }else { ui_log(g,utf8_bytes("Не удалось сохранить профиль.")); } }
        else {
            let previous: Buffer<U8>=read_file(utf8_bytes("active.profile"),65536);if len(&previous) > 0 && !write_file(utf8_bytes("active.profile.previous"),slice(&previous,0,len(&previous))) { ui_log(g,utf8_bytes("Не удалось создать копию прежнего профиля."));return; }
            if !write_file(utf8_bytes("active.profile"),slice(&contents,0,len(&contents))) { ui_log(g,utf8_bytes("Не удалось установить профиль."));return; }
            SendMessageW(control(g,20),0x14e,cast<USize>(target),0);let mut view: TrainingView=zero<TrainingView>();if training_view(slice(&contents,0,len(&contents)),&mut view) { g.view=view;g.history=true; }
            ui_log(g,utf8_bytes("Профиль загружен. Оценка относится к сохранённым испытаниям; профиль применяется при следующем включении."));
        }
    }
    unsafe fn window_proc(window: OpaqueHandle, message: U32, wparam: USize, lparam: OpaqueHandle) -> I64 {
        if message == U32(129) { let create: Ptr<CreateStruct>=handle_ptr<CreateStruct>(lparam);SetWindowLongPtrW(window,-21,(*create).parameter);return 1; }
        let userdata: OpaqueHandle=GetWindowLongPtrW(window,-21);if !handle_valid(userdata) { return DefWindowProcW(window,message,wparam,lparam); }let g: Ptr<Gui>=handle_ptr<Gui>(userdata);
        if message == U32(15) { gui_paint(&*g);return 0; }if message == U32(20) { return 1; }if message == U32(5) { gui_layout(&*g);return 0; }
        if message == U32(36) { let info: Ptr<MinMaxInfo>=handle_ptr<MinMaxInfo>(lparam);(*info).minimum_x=scale(&*g,760);(*info).minimum_y=scale(&*g,720);return 0; }
        if message == U32(43) { let item: Ptr<DrawItem>=handle_ptr<DrawItem>(lparam);gui_draw_button(&*g,&*item);return 1; }
        if message == U32(273) && ((wparam >> USize(16)) & USize(65535)) == USize(0) {
            let id: USize=wparam & USize(65535);if id == USize(10) { if (*g).phase == U32(1) || handle_valid(*resource_get(&(*g).process)) { gui_stop(&mut *g); }else { gui_start(&mut *g); } }
            if id == USize(13) { if (*g).phase == U32(6) { auxiliary(&mut *g,"Check-Driver.cmd",utf8_bytes("")); }else if selected_target(&*g) == U32(5) || selected_target(&*g) == U32(6) { auxiliary(&mut *g,"Check-MWO.cmd",utf8_bytes("")); }else if selected_target(&*g) == U32(3) || selected_target(&*g) == U32(4) { auxiliary(&mut *g,"Check-Telegram.cmd",utf8_bytes("")); }else { auxiliary(&mut *g,"Check-Network.cmd",utf8_bytes("")); } }
            if id == USize(14) { auxiliary(&mut *g,"Collect-Diagnostics.cmd",utf8_bytes("")); }
            if id == USize(16) { if handle_valid(*resource_get(&(*g).training)) { stop_training(&mut *g,false); }else { start_training(&mut *g); } }
            if id == USize(17) { profile_dialog(&mut *g,true); }if id == USize(18) { profile_dialog(&mut *g,false); }
            if id == USize(15) { let path: Buffer<U8>=local_path("NoDPI.log");let mut args: Buffer<U8>=buffer<U8>(0);push(&mut args,U8(34));bytes_append(&mut args,slice(&path,0,len(&path)));push(&mut args,U8(34));let owner: Resource<OpaqueHandle,close_handle>=resource(launch(&*g,utf8_bytes("notepad.exe"),slice(&args,0,len(&args)),false,1)); }
            return 0;
        }
        if message == U32(275) { gui_poll(&mut *g);return 0; }
        if message == U32(16) { stop_training(&mut *g,true);gui_stop(&mut *g);DestroyWindow(window);return 0; }if message == U32(2) { KillTimer(window,1);PostQuitMessage(0);return 0; }
        return DefWindowProcW(window,message,wparam,lparam);
    }
    unsafe fn gui_main() -> Unit {
        SetProcessDPIAware();if !set_application_directory() { print("Cannot select application directory");return; }
        let background: Resource<OpaqueHandle,delete_object>=resource(CreateSolidBrush(rgb(12,17,27)));let card: Resource<OpaqueHandle,delete_object>=resource(CreateSolidBrush(rgb(21,30,44)));
        let accent: Resource<OpaqueHandle,delete_object>=resource(CreateSolidBrush(rgb(73,120,244)));let secondary: Resource<OpaqueHandle,delete_object>=resource(CreateSolidBrush(rgb(36,50,72)));
        let instance: OpaqueHandle=GetModuleHandleW(null<U16>());let class_name: Buffer<U16>=wide(utf8_bytes("NoDPI.DLM.AutoWindow"));let mut class_: WinClass=WinClass(cast<U32>(size_of<WinClass>()),3,fn_ptr(window_proc),0,0,instance,zero<OpaqueHandle>(),LoadCursorW(zero<OpaqueHandle>(),32512),*resource_get(&background),null<U16>(),data(&class_name),zero<OpaqueHandle>());
        if RegisterClassExW(as_raw(borrowed_ptr(&mut class_))) == U16(0) { return; }
        let mut state: Box<Gui>=box<Gui>(Gui(buffer<Control>(0),zero<OpaqueHandle>(),GetStockObject(17),GetStockObject(17),GetStockObject(17),*resource_get(&background),*resource_get(&card),*resource_get(&accent),*resource_get(&secondary),resource(zero<OpaqueHandle>()),resource(zero<OpaqueHandle>()),bytes_copy(utf8_bytes("Ручных настроек нет. Нажмите «Включить».")),0,0,96,resource(zero<OpaqueHandle>()),zero<TrainingView>(),false,buffer<U8>(0)));
        let title: Buffer<U16>=wide(utf8_bytes("NoDPI — Автоматический режим"));let window: OpaqueHandle=CreateWindowExW(0,data(&class_name),data(&title),0x02cf0000,80,40,900,760,zero<OpaqueHandle>(),0,instance,handle(as_raw(borrowed_ptr(&mut *state))));
        if !handle_valid(window) { return; }state.window=window;state.dpi=cast<I32>(GetDpiForWindow(window));if state.dpi <= I32(0) { state.dpi=96; }
        let mut window_width: I32=scale(&*state,900);let mut window_height: I32=scale(&*state,760);
        let screen_width: I32=GetSystemMetrics(0);let screen_height: I32=GetSystemMetrics(1);if window_width > screen_width-I32(40) { window_width=screen_width-I32(40); }if window_height > screen_height-I32(80) { window_height=screen_height-I32(80); }
        let fit_dpi: I32=MulDiv(window_height-I32(40),96,670);if state.dpi > fit_dpi && fit_dpi >= I32(96) { state.dpi=fit_dpi; }
        MoveWindow(window,20,20,window_width,window_height,0);
        let face: Buffer<U16>=wide(utf8_bytes("Segoe UI"));let font: Resource<OpaqueHandle,delete_object>=resource(CreateFontW(-scale(&*state,15),0,0,0,400,0,0,0,1,0,0,5,0,data(&face)));
        let title_font: Resource<OpaqueHandle,delete_object>=resource(CreateFontW(-scale(&*state,32),0,0,0,700,0,0,0,1,0,0,5,0,data(&face)));let status_font: Resource<OpaqueHandle,delete_object>=resource(CreateFontW(-scale(&*state,28),0,0,0,600,0,0,0,1,0,0,5,0,data(&face)));
        if handle_valid(*resource_get(&font)) { state.font=*resource_get(&font); }if handle_valid(*resource_get(&title_font)) { state.title_font=*resource_get(&title_font); }if handle_valid(*resource_get(&status_font)) { state.status_font=*resource_get(&status_font); }
        let mut dark: U32=1;DwmSetWindowAttribute(window,20,as_raw(borrowed_ptr(&mut dark)),4);let mut corners: U32=2;DwmSetWindowAttribute(window,33,as_raw(borrowed_ptr(&mut corners)),4);
        assert(size_of<WinClass>() == 80);assert(size_of<WinMessage>() == 48);assert(size_of<ShellExecute>() == 112);assert(size_of<OpenFileName>() == 152);assert(size_of<PaintStruct>() == 72);assert(size_of<DrawItem>() == 64);assert(size_of<MinMaxInfo>() == 40);
        gui_build(&mut *state);SetTimer(window,1,500,zero<OpaqueHandle>());ShowWindow(window,5);UpdateWindow(window);
        let mut message: WinMessage=zero<WinMessage>();while true { let result: I32=GetMessageW(as_raw(borrowed_ptr(&mut message)),zero<OpaqueHandle>(),0,0);if result <= I32(0) { break; }if IsDialogMessageW(window,as_raw(borrowed_ptr(&mut message))) == I32(0) { TranslateMessage(as_raw(borrowed_ptr(&mut message)));DispatchMessageW(as_raw(borrowed_ptr(&mut message))); } }stop_training(&mut *state,true);gui_stop(&mut *state);
    }
}

module dpi.startup_view
import dpi.bytes
native {
    record StartupView repr(C) { found: Bool; ready: Bool; driver: Bool; error: U32; }
    fn startup_view(text: Slice<U8>) -> StartupView {
        let mut v: StartupView=zero<StartupView>();let mut at: USize=0;
        while at < len(text) { let end: USize=byte_find(text,at,10);let raw: Slice<U8>=slice(&text,at,end-at);let line: Slice<U8>=trimmed(raw);at=end+1;let eq: USize=byte_find(line,0,61);if eq == len(line) { continue; }let key: Slice<U8>=slice(&line,0,eq);let value: Slice<U8>=slice(&line,eq+1,len(line)-eq-1);let mut n: U32=0;
            if bytes_equal(key,utf8_bytes("component")) { v.driver=bytes_at(value,0,utf8_bytes("WinDivert")); }
            if bytes_equal(key,utf8_bytes("win32_error")) { if !decimal(value,4294967295,&mut n) { return zero<StartupView>(); }v.error=n;v.found=true; }
            if bytes_equal(key,utf8_bytes("ready")) { if !decimal(value,1,&mut n) { return zero<StartupView>(); }v.ready=n == U32(1); }
        }return v;
    }
    fn startup_caption(code: U32) -> Buffer<U8> {
        let mut b: Buffer<U8>=bytes_copy(utf8_bytes("WinDivert: ошибка "));append_number(&mut b,cast<U64>(code));
        if code == U32(5) { bytes_append(&mut b,utf8_bytes(". Нужен запуск с правами администратора.")); }
        else if code == U32(58) { bytes_append(&mut b,utf8_bytes(" при открытии драйвера. Это сбой запуска перехвата, а не проверка доступности сайта. Запустите Check-Driver.cmd и соберите отчёт.")); }
        else if code == U32(577) || code == U32(1275) { bytes_append(&mut b,utf8_bytes(". Windows не разрешила загрузить драйвер. Подробности — в Check-Driver.cmd.")); }
        else if code == U32(654) { bytes_append(&mut b,utf8_bytes(". Загружена несовместимая версия драйвера. Закройте другие DPI-программы и перезапустите Windows.")); }
        else if code == U32(1753) { bytes_append(&mut b,utf8_bytes(". Недоступна служба фильтрации BFE; см. Check-Driver.cmd.")); }
        else { bytes_append(&mut b,utf8_bytes(". См. Check-Driver.cmd и журнал запуска.")); }return move b;
    }
}

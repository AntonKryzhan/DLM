"""Native DLM vector-editor smoke test under Xvfb on Linux (no VST3 host required).

Run from project root after building the UI shared library:
    xvfb-run -a python3 tests/vst3/test_editor_native.py dist/linux-x86_64/libchordliveui.so
"""
import ctypes as c
from pathlib import Path
import sys

module = Path(sys.argv[1]).resolve()
u = c.CDLL(str(module))
for name, result, args in (
    ('live_ui_create', c.c_void_p, [c.c_size_t]),
    ('live_ui_handle', c.c_size_t, [c.c_void_p]),
    ('live_ui_resize', None, [c.c_void_p, c.c_size_t, c.c_size_t]),
    ('live_ui_draw', None, [c.c_void_p, c.POINTER(c.c_double), c.c_size_t]),
    ('live_ui_hit', c.c_int32, [c.c_size_t] * 4),
    ('live_ui_slider_value', c.c_double, [c.c_size_t] * 3),
    ('live_ui_destroy', None, [c.c_void_p]),
):
    f = getattr(u, name)
    f.restype, f.argtypes = result, args

# Pure layout tests, also cover first and last interactive values.
assert u.live_ui_hit(18, 78, 1024, 736) == 0
assert u.live_ui_hit(18 + 3 * 250 + 10, 78 + 11 * 52 + 15, 1024, 736) == 47
assert u.live_ui_hit(0, 0, 1024, 736) == -1
assert u.live_ui_slider_value(18 + 11, 1024, 0) == 0.0
assert u.live_ui_slider_value(18 + 11 + 180, 1024, 0) == 1.0

x = c.CDLL('libX11.so.6')
x.XOpenDisplay.argtypes = [c.c_char_p]
x.XOpenDisplay.restype = c.c_void_p
x.XDefaultScreen.argtypes = [c.c_void_p]
x.XDefaultScreen.restype = c.c_int
x.XRootWindow.argtypes = [c.c_void_p, c.c_int]
x.XRootWindow.restype = c.c_ulong
x.XCreateSimpleWindow.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint, c.c_uint, c.c_ulong, c.c_ulong]
x.XCreateSimpleWindow.restype = c.c_ulong
x.XMapWindow.argtypes = [c.c_void_p, c.c_ulong]
x.XFlush.argtypes = [c.c_void_p]
x.XDestroyWindow.argtypes = [c.c_void_p, c.c_ulong]
x.XCloseDisplay.argtypes = [c.c_void_p]
d = x.XOpenDisplay(None)
assert d, 'DISPLAY/Xvfb is required'
root = x.XRootWindow(d, x.XDefaultScreen(d))
parent = x.XCreateSimpleWindow(d, root, 0, 0, 1080, 800, 0, 0, 0)
assert parent
x.XMapWindow(d, parent)
x.XFlush(d)
windows = []
try:
    for k in range(2):
        ui = u.live_ui_create(parent)
        assert ui, 'DLM window_child failed'
        windows.append(ui)
        hwnd = u.live_ui_handle(ui)
        assert hwnd
        values = (c.c_double * 48)(*[float(i + k) / 49 for i in range(48)])
        u.live_ui_draw(ui, values, 48)
        u.live_ui_resize(ui, 1024, 736)
        u.live_ui_draw(ui, values, 48)
    for ui in reversed(windows):
        u.live_ui_destroy(ui)
    windows.clear()
finally:
    for ui in windows:
        u.live_ui_destroy(ui)
    x.XDestroyWindow(d, parent)
    x.XCloseDisplay(d)
print('PASS: DLM vector GUI, 48 controls, resize, two editors, independent destruction')

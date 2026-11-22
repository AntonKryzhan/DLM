"""Real Win32 IPlugView / DLM vector-editor smoke test.

Run in an interactive Windows desktop (not a service or headless CI):
    py -3 tools\\test_vst3_editor_windows.py
The test creates an invisible host parent HWND and edits one continuous parameter.
"""
import ctypes as c
from pathlib import Path
import os
import sys

if os.name != 'nt':
    sys.exit('This test requires a Windows desktop')

bundle = Path('dist/vst3-windows/CHORDlike LIVE.vst3/Contents/x86_64-win')
plugin = (Path(sys.argv[1]) if len(sys.argv) > 1 else bundle / 'CHORDlike LIVE.vst3').resolve()
if not plugin.is_file():
    sys.exit(f'VST3 module not found: {plugin}')
path_guard = os.add_dll_directory(str(plugin.parent))
lib = c.CDLL(str(plugin))
lib.GetPluginFactory.restype = c.c_void_p
ptr, i32, u32 = c.c_void_p, c.c_int32, c.c_uint32

class ClassInfo(c.Structure):
    _fields_ = [('cid', c.c_char * 16), ('cardinality', i32), ('category', c.c_char * 32), ('name', c.c_char * 64)]

class ParamInfo(c.Structure):
    _fields_ = [('id', u32), ('title', c.c_uint16 * 128), ('shortTitle', c.c_uint16 * 128),
                ('units', c.c_uint16 * 128), ('steps', i32), ('default', c.c_double),
                ('unitId', i32), ('flags', i32)]

class Rect(c.Structure):
    _fields_ = [('left', i32), ('top', i32), ('right', i32), ('bottom', i32)]

def method(obj, index, ret, *args):
    table = c.cast(obj, c.POINTER(ptr))[0]
    address = c.cast(table, c.POINTER(ptr))[index]
    return c.WINFUNCTYPE(ret, ptr, *args)(address)

def uid(a, b, d, e):
    return c.create_string_buffer(a.to_bytes(4, 'little') + bytes((b >> 16 & 255, b >> 24 & 255, b & 255, b >> 8 & 255)) +
                                  d.to_bytes(4, 'big') + e.to_bytes(4, 'big'), 16)

factory = lib.GetPluginFactory()
assert factory
assert method(factory, 4, i32)(factory) == 2
cl = ClassInfo()
assert method(factory, 5, i32, i32, ptr)(factory, 1, c.byref(cl)) == 0
controller = ptr()
unknown = uid(0, 0, 0xC0000000, 0x46)
class_id = c.create_string_buffer(bytes(cl.cid), 16)
assert method(factory, 6, i32, ptr, ptr, ptr)(factory, c.cast(class_id, ptr), c.cast(unknown, ptr), c.byref(controller)) == 0
view = ptr()
user32 = c.windll.user32
user32.CreateWindowExW.restype = ptr
user32.CreateWindowExW.argtypes = [u32, c.c_wchar_p, c.c_wchar_p, u32, i32, i32, i32, i32, ptr, ptr, ptr, ptr]
user32.GetWindow.restype = ptr
user32.GetWindow.argtypes = [ptr, u32]
user32.SendMessageW.restype = c.c_ssize_t
user32.SendMessageW.argtypes = [ptr, u32, c.c_size_t, c.c_ssize_t]
user32.DestroyWindow.argtypes = [ptr]
parent = ptr()
try:
    assert method(controller, 3, i32, ptr)(controller, None) == 0
    view = method(controller, 17, ptr, c.c_char_p)(controller, b'editor')
    assert view, 'createView did not return an editor'
    assert method(view, 3, i32, c.c_char_p)(view, b'HWND') == 0
    size = Rect()
    assert method(view, 9, i32, ptr)(view, c.byref(size)) == 0
    assert (size.right - size.left, size.bottom - size.top) == (1024, 736)
    parent = user32.CreateWindowExW(0, 'STATIC', 'DLM GUI host fixture', 0x10CF0000,
                                    0, 0, 1100, 800, None, None, None, None)
    assert parent, 'Could not create Win32 parent HWND'
    assert method(view, 4, i32, ptr, c.c_char_p)(view, parent, b'HWND') == 0, 'IPlugView::attached failed'
    child = user32.GetWindow(parent, 5)  # GW_CHILD
    assert child, 'DLM GUI did not create child HWND'
    assert method(controller, 8, i32)(controller) == 48
    info = ParamInfo()
    assert method(controller, 9, i32, i32, ptr)(controller, 4, c.byref(info)) == 0
    original = method(controller, 14, c.c_double, u32)(controller, info.id)
    assert original <= 0.00001
    # Fifth control, row 1 / column 0, has a continuous crossfeed slider.
    x, y = 168, 151
    coords = (y << 16) | x
    user32.SendMessageW(child, 0x0201, 1, coords)  # WM_LBUTTONDOWN
    user32.SendMessageW(child, 0x0202, 0, coords)  # WM_LBUTTONUP
    updated = method(controller, 14, c.c_double, u32)(controller, info.id)
    assert updated > 0.25, f'Mouse gesture did not update parameter: {updated}'
    assert method(view, 5, i32)(view) == 0
    print('PASS: VST3 editor attached to HWND, DLM vector UI, mouse interaction, release')
finally:
    if view:
        method(view, 2, u32)(view)
    if parent:
        user32.DestroyWindow(parent)
    if controller:
        method(controller, 4, i32)(controller)
        method(controller, 2, u32)(controller)
    method(factory, 2, u32)(factory)

#!/usr/bin/env python3
"""Independent ABI, state, block-splitting and resource regression tests."""
import argparse
import ctypes as C
import math
from pathlib import Path


def configured(path):
    lib = C.CDLL(str(path.resolve()))
    lib.live_latency.restype = C.c_uint32
    lib.live_create_ex.argtypes = [C.c_double, C.c_uint32, C.c_uint32, C.c_uint32, C.c_float]
    lib.live_create_ex.restype = C.c_void_p
    lib.live_process_full.argtypes = [
        C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_size_t,
        C.c_float, C.c_float, C.c_float, C.c_uint32, C.c_uint32,
        C.c_float, C.c_float, C.c_float, C.c_float, C.c_float,
    ]
    lib.live_process_full.restype = C.c_uint32
    lib.live_destroy.argtypes = [C.c_void_p]
    lib.live_reset.argtypes = [C.c_void_p]
    lib.dlm_native_last_error.restype = C.c_char_p
    lib.dlm_native_allocations.restype = C.c_uint64
    lib.dlm_native_live_allocations.restype = C.c_uint64
    lib.dlm_native_thread_init()
    return lib


def render(lib, handle, n, splits):
    L = (C.c_float * n)()
    R = (C.c_float * n)()
    for i in range(n):
        L[i] = C.c_float(0.17 * math.sin(i * 0.074) + 0.31 * math.cos(i * 0.017)).value
        R[i] = C.c_float(0.24 * math.sin(i * 0.055 + 0.73)).value
    count_start = lib.dlm_native_allocations()
    at = 0
    while at < n:
        width = min(splits, n-at)
        result = lib.live_process_full(
            handle,
            C.cast(C.byref(L, at * 4), C.POINTER(C.c_float)),
            C.cast(C.byref(R, at * 4), C.POINTER(C.c_float)),
            width, 0.27, 0.76, -2.0, 3, 2, 0.52, 0.63, 0.49, 0.31, 0.66,
        )
        assert result == 1, lib.dlm_native_last_error()
        at += width
    assert lib.dlm_native_allocations() == count_start, 'Unexpected DSP heap allocation'
    assert all(math.isfinite(x) for x in L) and all(math.isfinite(x) for x in R)
    return list(L),list(R)


def test(lib):
    assert lib.live_latency() == 157
    for depth in range(6):
        handle1 = lib.live_create_ex(48000.0, depth, 2, 0, 0.25)
        handle2 = lib.live_create_ex(48000.0, depth, 2, 0, 0.25)
        assert handle1 and handle2
        one = render(lib,handle1,1024,1024)
        many = render(lib,handle2,1024,47)
        assert one == many, f'Block splitting changed output for depth {depth}'
        lib.live_reset(handle1)
        lib.live_reset(handle2)
        after_reset = render(lib,handle1,1024,19)
        after_reset2 = render(lib,handle2,1024,23)
        assert after_reset == after_reset2, f'Reset or block splitting failed for depth {depth}'
        lib.live_destroy(handle1)
        lib.live_destroy(handle2)
        assert lib.dlm_native_live_allocations() == 0, f'Leaked resources at depth {depth}'
        print(f'PASS depth={depth}, no allocations, identical block splits, reset, clean lifetime')
    assert not lib.live_create_ex(48000.0, 6, 0, 0, 0.25)
    assert not lib.live_create_ex(float('nan'), 1, 0, 0, 0.25)
    print('PASS ABI, invalid configurations and all 6 oversampling modes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--library', type=Path, default=Path(__file__).resolve().parents[1] / 'dist/linux-x86_64/libchordlive.so')
    args = parser.parse_args()
    test(configured(args.library))

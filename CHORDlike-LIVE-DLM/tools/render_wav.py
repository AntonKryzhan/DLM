#!/usr/bin/env python3
"""Render a stereo PCM16 WAV through the DLM-native CHORD-like LIVE core."""
import argparse
import ctypes as ct
import os
from pathlib import Path
import shutil
import struct
import sys
import wave


def load_dlm(path):
    # Python 3.8+ does not search PATH for dependent DLLs by default.
    # Make the MinGW runtime supplied with the active GCC toolchain visible.
    dll_directory = None
    if sys.platform == 'win32' and hasattr(os, 'add_dll_directory'):
        gcc = shutil.which('gcc')
        if gcc:
            dll_directory = os.add_dll_directory(str(Path(gcc).resolve().parent))
    try:
        lib = ct.CDLL(str(path.resolve()))
    except OSError:
        if dll_directory is not None:
            dll_directory.close()
        raise
    lib._dll_directory = dll_directory
    lib.dlm_native_last_error.restype = ct.c_char_p
    lib.dlm_native_thread_init.restype = ct.c_uint64
    lib.live_latency.restype = ct.c_uint32
    lib.live_latency_final.restype = ct.c_uint32
    lib.live_fast_mode.argtypes = [ct.c_void_p, ct.c_bool]
    lib.live_fast_mode.restype = ct.c_uint32
    lib.live_tube_cpu_mode.argtypes = [ct.c_void_p, ct.c_uint32]
    lib.live_tube_cpu_mode.restype = ct.c_uint32
    lib.live_tube_config.argtypes = [ct.c_void_p, ct.c_uint32, ct.c_uint32,
        ct.c_float, ct.c_float, ct.c_float, ct.c_float]
    lib.live_tube_config.restype = ct.c_uint32
    lib.live_tube_routing.argtypes = [ct.c_void_p, ct.c_uint32]
    lib.live_tube_routing.restype = ct.c_uint32
    lib.live_band_frequency.argtypes = [ct.c_void_p, ct.c_float, ct.c_float]
    lib.live_band_frequency.restype = ct.c_uint32
    lib.live_band_config.argtypes = [ct.c_void_p, ct.c_uint32, ct.c_uint32, ct.c_uint32,
        ct.c_float, ct.c_float, ct.c_float, ct.c_float]
    lib.live_band_config.restype = ct.c_uint32
    lib.live_request_mode.argtypes = [ct.c_void_p, ct.c_uint32, ct.c_uint32, ct.c_uint32, ct.c_float]
    lib.live_request_mode.restype = ct.c_uint32
    lib.live_create_ex.argtypes = [ct.c_double, ct.c_uint32, ct.c_uint32, ct.c_uint32, ct.c_float]
    lib.live_create_ex.restype = ct.c_void_p
    lib.live_process_full.argtypes = [
        ct.c_void_p, ct.POINTER(ct.c_float), ct.POINTER(ct.c_float), ct.c_size_t,
        ct.c_float, ct.c_float, ct.c_float, ct.c_uint32, ct.c_uint32,
        ct.c_float, ct.c_float, ct.c_float, ct.c_float, ct.c_float,
    ]
    lib.live_process_full.restype = ct.c_uint32
    lib.live_process_final.argtypes = lib.live_process_full.argtypes + [
        ct.c_bool, ct.c_bool, ct.c_float,
    ]
    lib.live_process_final.restype = ct.c_uint32
    lib.live_destroy.argtypes = [ct.c_void_p]
    lib.dlm_native_thread_init()
    return lib


def saturate_pcm16(value):
    return max(-32768, min(32767, round(value * 32768.0)))


def parse_band(text):
    values = [item.strip() for item in text.split(',')]
    if len(values) != 6:
        raise ValueError('Band format: type,topology,driveDb,bias,sag,wet')
    try:
        tube, topology = int(values[0]), int(values[1])
        drive, bias, sag, wet = map(float, values[2:])
    except ValueError as exc:
        raise ValueError('Invalid numeric band setting') from exc
    if not (0 <= tube <= 6 and 0 <= topology <= 1 and 0 <= drive <= 36
            and -1 <= bias <= 1 and 0 <= sag <= 1 and 0 <= wet <= 1):
        raise ValueError('Band settings outside supported bounds')
    return tube, topology, drive, bias, sag, wet


def require_call(lib, function, *args):
    if function(*args) != 1:
        raise RuntimeError(f'DLM configuration failed: {lib.dlm_native_last_error()}')


def run(args):
    lib = load_dlm(args.library)
    with wave.open(str(args.input), 'rb') as source:
        if source.getnchannels() != 2 or source.getsampwidth() != 2 or source.getcomptype() != 'NONE':
            raise ValueError('Input must be uncompressed stereo PCM16 WAV')
        rate = source.getframerate()
        if not 8000 <= rate <= 384000:
            raise ValueError('Unsupported sample rate')
        if args.input.resolve() == args.output.resolve():
            raise ValueError('Output file must differ from input file')
        engine = lib.live_create_ex(rate, args.oversampling, args.pulse, args.dac, args.mismatch)
        if not engine:
            raise RuntimeError(f'create: {lib.dlm_native_last_error()}')
        try:
            require_call(lib, lib.live_tube_config, engine, args.tube_type,
                args.tube_topology, args.tube_drive_db, args.tube_bias, args.tube_sag, args.tube_wet)
            require_call(lib, lib.live_tube_cpu_mode, engine, args.fast_tubes)
            if args.tube_routing:
                require_call(lib, lib.live_tube_routing, engine, 1)
                require_call(lib, lib.live_band_frequency, engine, args.low_hz, args.high_hz)
                for i, spec in enumerate((args.band_low, args.band_mid, args.band_high)):
                    require_call(lib, lib.live_band_config, engine, i, *parse_band(spec))
            if args.fast:
                require_call(lib, lib.live_fast_mode, engine, True)
            with wave.open(str(args.output), 'wb') as dest:
                dest.setnchannels(2)
                dest.setsampwidth(2)
                dest.setframerate(rate)
                remaining = source.getnframes() + lib.live_latency_final()
                available = source.getnframes()
                while remaining:
                    count = min(args.block, remaining)
                    take = min(count, available)
                    raw = source.readframes(take)
                    available -= take
                    samples = struct.unpack('<' + str(2 * take) + 'h', raw) if take else ()
                    left = (ct.c_float * count)()
                    right = (ct.c_float * count)()
                    for i in range(take):
                        left[i] = samples[2 * i] / 32768.0
                        right[i] = samples[2 * i + 1] / 32768.0
                    code = lib.live_process_final(
                        engine, left, right, count,
                        args.crossfeed, args.mix, args.output_db, args.filter,
                        args.phase_mode, args.focus, args.phase_motion, args.hf_tone,
                        args.character, args.pulse_color,
                        args.auto_match, args.limiter, args.ceiling_db,
                    )
                    if code != 1:
                        raise RuntimeError(f'process: {lib.dlm_native_last_error()}')
                    data = [0] * (2 * count)
                    for i in range(count):
                        data[2 * i] = saturate_pcm16(left[i])
                        data[2 * i + 1] = saturate_pcm16(right[i])
                    dest.writeframesraw(struct.pack('<' + str(2 * count) + 'h', *data))
                    remaining -= count
        finally:
            lib.live_destroy(engine)
            error = lib.dlm_native_last_error()
            if error:
                raise RuntimeError(f'destroy: {error.decode()}')
    print(f'Rendered {args.output} ({rate} Hz, {lib.live_latency_final()}-sample total delay)')


def cli():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--block', type=int, default=1024)
    parser.add_argument('--oversampling', type=int, choices=range(6), default=3, help='0..5 = 2x,4x,8x,16x,32x,64x')
    parser.add_argument('--pulse', type=int, choices=range(3), default=0)
    parser.add_argument('--dac', type=int, choices=range(4), default=0)
    parser.add_argument('--mismatch', type=float, default=0.25)
    parser.add_argument('--crossfeed', type=float, default=0.0)
    parser.add_argument('--mix', type=float, default=1.0)
    parser.add_argument('--output-db', type=float, default=0.0)
    parser.add_argument('--filter', type=int, choices=range(4), default=0)
    parser.add_argument('--phase-mode', type=int, choices=range(3), default=0)
    parser.add_argument('--focus', type=float, default=0.0)
    parser.add_argument('--phase-motion', type=float, default=0.0)
    parser.add_argument('--hf-tone', type=float, default=0.0)
    parser.add_argument('--character', type=float, default=0.0)
    parser.add_argument('--pulse-color', type=float, default=0.0)
    parser.add_argument('--auto-match', action='store_true', help='Enable output loudness matching')
    parser.add_argument('--limiter', action='store_true', help='Enable output true-peak lookahead limiter')
    parser.add_argument('--ceiling-db', type=float, default=-1.0)
    parser.add_argument('--fast', action='store_true', help='Use original folded FIR route when nonlinear effects are off')
    parser.add_argument('--fast-tubes', type=int, choices=[0, 1], default=0)
    parser.add_argument('--tube-type', type=int, choices=range(7), default=0)
    parser.add_argument('--tube-topology', type=int, choices=[0, 1], default=0)
    parser.add_argument('--tube-drive-db', type=float, default=12.0)
    parser.add_argument('--tube-bias', type=float, default=0.0)
    parser.add_argument('--tube-sag', type=float, default=0.0)
    parser.add_argument('--tube-wet', type=float, default=1.0)
    parser.add_argument('--tube-routing', type=int, choices=[0, 1], default=0)
    parser.add_argument('--low-hz', type=float, default=250.0)
    parser.add_argument('--high-hz', type=float, default=3000.0)
    parser.add_argument('--band-low', default='0,0,12,0,0,1', help='type,topology,driveDb,bias,sag,wet')
    parser.add_argument('--band-mid', default='0,0,12,0,0,1')
    parser.add_argument('--band-high', default='0,0,12,0,0,1')
    a = parser.parse_args()
    if a.block < 1 or a.block > 65536:
        parser.error('--block must be in 1..65536')
    if not -12.0 <= a.ceiling_db <= -0.1:
        parser.error('--ceiling-db must be in -12..-0.1')
    if not (0 <= a.tube_drive_db <= 36 and -1 <= a.tube_bias <= 1 and
            0 <= a.tube_sag <= 1 and 0 <= a.tube_wet <= 1):
        parser.error('Tube settings outside supported bounds')
    if not (40 <= a.low_hz < a.high_hz <= 12000):
        parser.error('Band frequencies require 40 <= low < high <= 12000')
    if a.fast and (a.pulse or a.dac or a.character or a.tube_type or a.tube_routing):
        parser.error('--fast requires neutral DAC, Pulse, Character and Tube settings')
    if not 0.0 <= a.mismatch <= 1.0:
        parser.error('--mismatch must be in 0..1')
    for field in ('crossfeed', 'mix', 'focus', 'phase_motion', 'hf_tone', 'character', 'pulse_color'):
        if not 0.0 <= getattr(a, field) <= 1.0:
            parser.error(f'--{field.replace("_", "-")} must be in 0..1')
    try:
        run(a)
    except (ValueError, OSError, RuntimeError, wave.Error) as exc:
        parser.exit(1, f'error: {exc}\n')


if __name__ == '__main__':
    cli()

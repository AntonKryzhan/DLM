## VST3 сборка

См. [README-VST3-RU.md](README-VST3-RU.md). Для Windows запусти `BUILD-VST3-WINDOWS.cmd`.

# CHORD-like LIVE v0.17.0 — expanded native DLM port

This project builds a **DLM v0.133.0 native processing library** for the LIVE engine. All production DSP code is in `src/live_core.dlm`. Its generated Windows DLL is **not yet a Steinberg VST3 plug-in**, and renaming a DLL to `.vst3` will not make it one. The C++ files in `tests/` are independently compiled *comparison harnesses* using the user's separate original CHORDlike v0.17.0 source tree; they are not linked into the production DLM library.

## Implemented and differentially tested against original C++

- Symmetric polyphase FIR reconstruction, all six oversampling settings (2× to 64×), stereo histories, original coefficients and 157-sample core PDC.
- Precomputed, equivalent 384-tap base-rate folded FIR with 128-sample transitions to/from the optimized mode; neutral configuration only.
- Original crossfeed, gain, dry/wet smoothing, warm/HF voicing, phase and creative shaping.
- Pulse Array modes 0–2, DAC modes 0–3, dither, noise shaping, seeded pseudo-random generators and mismatch.
- All six Koren-derived tube LUT models, single-ended and push/pull configurations, sag, bias, wet/drive smoothing, transitions.
- ThreeBandTube LR4 crossover, independent low/mid/high valve settings, crossover-frequency smoothing and single/three-band route blending.
- Tube 16× processing mode for requested 32×/64× oversampling. FIR hot loops in that mode remain scalar (the original C++ has a distinct SSE2 summation order).
- Non-blocking changes to oversampling, Pulse and DAC modes through the original two-phase, 128+128 sample transition.
- OutputStage: automatic level matching, 4-phase interpolated peak **estimate**, lookahead limiter, 16 additional samples of fixed latency, now **173 total** for the final API.
- Per-instance resources, reset and destruction, no allocation inside the tested DSP processing callbacks.

The original 157-sample `live_process_full` and `live_latency` entry points remain available for ABI compatibility. `live_process_final` includes OutputStage and uses `live_latency_final` (173). New configuration entry points expose single-valve, three-band, DSP fast-path and mode-transition settings. Consult `include/CHORDlikeLiveDLM.h` for exact generated C signatures. Call `dlm_native_thread_init()` on each host processing thread before calling exported DLM functions.

## Still missing — required before calling this a complete VST3

- A Steinberg-compatible `GetPluginFactory`, VST3 `IComponent`, `IAudioProcessor`, `IEditController` and `IPlugView` implementation and accurate ABI IDs.
- Host parameter automation and matching original 48 LIVE parameter IDs, ranges, defaults, VST3 state and `.chordpreset` compatibility.
- Embedding DLM UI into a VST3 host window, controller/process communication, original editor appearance and handling.
- `.vst3` bundle layout, DAW/Steinberg validator testing on Windows, full host-level latency and lifecycle testing.
- Independent real-time deadline validation on the target PC, including nonlinear 32×/64× modes. The DLM fast-tube route does not yet reproduce the original SSE2 reduction order.

These are substantial open integration requirements. The project does not substitute C++/JUCE components or misleading placeholder interfaces to claim completeness.

## Windows build and use

Requires `dlm.exe` v0.133.0 and GCC in PATH. For the existing SDK layout:

```powershell
$env:Path = 'M:\users\Ark\Desktop\DLM\DLM-OUT\bin;M:\users\Ark\Desktop\DLM\DLM-OUT\toolchain\mingw64\bin;' + $env:Path
cd 'M:\users\Ark\Desktop\DLM\CHORDlike-LIVE-DLM'
.\build-windows.cmd
```

The resulting DLL is `dist\windows-x86_64\CHORDlike-LIVE-DLM.dll` (a native processing library). No Python or C++ is used by the library itself; Python is used for offline WAV rendering and tests.

To check stereo 16-bit PCM WAV output, including the limiter and full 173-sample fixed latency:

```powershell
py -3 tools\render_wav.py tests\input_example.wav output-windows.wav --library dist\windows-x86_64\CHORDlike-LIVE-DLM.dll --oversampling 3 --dac 2 --character 0.4 --pulse-color 0.5 --limiter
```

To use the three-band tube configuration:

```powershell
py -3 tools\render_wav.py tests\input_example.wav output-tubes.wav --library dist\windows-x86_64\CHORDlike-LIVE-DLM.dll --oversampling 3 --tube-routing 1 --band-low '2,0,14.1,0.408,0,0.696' --band-mid '1,0,14.8,0.368,0,0.616' --band-high '3,0,27.6,0.44,0,1' --low-hz 440 --high-hz 3232 --limiter
```

A band setting is `type,topology,driveDb,bias,sag,wet`; input is restricted to uncompressed stereo PCM16 WAV. The Python loader also adds the active GCC directory to Windows DLL search so that MinGW dependencies are found (Python 3.8+). Run the WAV command in the same shell that can find `gcc`.

For a neutral fast path use `--fast` without Pulse/DAC/Character/Tube modes. `--fast-tubes 1` selects 16× valve processing at 32×/64× but is a separate feature.

## Linux build and regression

```sh
./build-linux.sh
python3 tests/test_port.py
python3 tests/compare_live.py --reference-dir /path/to/unpacked/original
python3 tests/test_output_stage.py --reference-dir /path/to/unpacked/original
python3 tests/test_tube.py --reference-dir /path/to/unpacked/original
python3 tests/test_threeband.py --reference-dir /path/to/unpacked/original
python3 tests/test_mode_transition.py --reference-dir /path/to/unpacked/original
python3 tests/test_folded.py --reference-dir /path/to/unpacked/original
python3 tests/test_folded_toggle.py --reference-dir /path/to/unpacked/original
```

`--reference-dir` points to a separate checkout containing `Source/DSP/LiveAudioEngine.cpp` and the other DSP sources from the original archive. Comparisons do not require JUCE; they use a compiled C++ reference in the *test environment only*. Their accuracy findings apply to those exact configurations and machines, not to every possible input or to VST3 host behavior.

## Safety and performance constraints

Input channel pointers must be valid, distinct, writable and remain alive during processing; one engine instance must not be processed concurrently. The host must handle output buffers, callbacks and any external thread synchronization correctly. Sample-level processing does not allocate heap memory in the tested paths, but DLM is not yet a certified bounded-time real-time environment. High oversampling with active nonlinearity may exceed the host's processing deadline.

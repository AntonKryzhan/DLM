# Verification report — expanded DLM LIVE port

Source: user-supplied CHORDlike LIVE v0.17.0 and DLM v0.133.0. Build environment: Linux x86-64, GCC, DLM native backend. The Windows DLL from the earlier port was built by the user; the new extended DLL has not been built or loaded on Windows in this environment.

| Test | Observed result |
|---|---|
| DLM native check and shared-library compilation | PASS |
| Existing neutral/creative/Pulse/DAC comparison | 15 scenarios, maximum F32 difference 0 |
| `tests/test_port.py` | PASS, 6 ratios, block split and reset, zero process heap allocation |
| Original `OutputStage` with 173-sample PDC | 6 scenarios, maximum F32 difference 0 |
| Single Koren `TubeStage` | 6 valve scenarios, maximum F32 difference 0 |
| ThreeBandTube | 4 scenarios, maximum F32 difference 0 |
| Dynamic oversampling/Pulse/DAC switching | 4 scenarios, maximum F32 difference 0 |
| Folded FIR and fast mode | 3 scenarios including 64×, maximum F32 difference 0 |
| Fast-to-legacy return transition | 2 scenarios, maximum F32 difference 0 |
| PCM16 WAV with 3-band valves, limiter | PASS, created WAV with 173-sample tail |
| Native memory accounting | No heap allocations in measured process calls; zero live allocations after engine destruction |
| Windows x64 rebuilt library, VST3 host, editor, automation | NOT TESTED |
| Native Steinberg VST3 exports/bundle | NOT IMPLEMENTED |

These are **sample-precision differential tests**, not formal proofs of bit-identical behavior on every platform, real-time deadline guarantees or full VST3 compatibility. The original SIMD-specific fast-tube summation path is not yet verified in DLM against the C++ SIMD order.

The new `live_process_final` API includes `OutputStage` and a fixed **173-sample total latency**; the original `live_process_full` remains **157 samples**. Neither interface is itself a VST3 plug-in.

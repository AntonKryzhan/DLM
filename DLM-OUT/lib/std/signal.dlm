// Signal processing (v0.123, docs/KERNELS.md): windows, FIR design, biquad (RBJ) designs, IIR filtering,
// spectrograms, wavelet denoising, 2-D spectra, SHA-256 digests.
module std.signal
import std.maps

theory StdSignal extends StdMaps {
    record Biquad(b: FloatVec, a: FloatVec)

    private fn tau() -> Float = 6.283185307179586
    private fn idx(n: Nat) -> List<Nat> = nv_to_list(unwrap_or(nv_range(0, n), nv_empty()))
    private fn tab(n: Nat, f: Fn<Nat, Float>) -> FloatVec = unwrap_or(fv_from_list(map(idx(n), f)), fv_empty())
    private fn phase(i: Nat, n: Nat) -> Float = fl_div(fl_mul(tau(), fl_of_nat(i)), fl_of_nat(monus(n, 1)))
    fn hann(n: Nat) -> FloatVec = if_then_else(2 > n, tab(n, fun(i: Nat, 1.0)), tab(n, fun(i: Nat, fl_mul(0.5, fl_sub(1.0, fl_cos(phase(i, n)))))))
    fn hamming(n: Nat) -> FloatVec = if_then_else(2 > n, tab(n, fun(i: Nat, 1.0)), tab(n, fun(i: Nat, fl_sub(0.54, fl_mul(0.46, fl_cos(phase(i, n)))))))
    fn blackman(n: Nat) -> FloatVec =
        if_then_else(2 > n, tab(n, fun(i: Nat, 1.0)), tab(n, fun(i: Nat, fl_add(fl_sub(0.42, fl_mul(0.5, fl_cos(phase(i, n)))), fl_mul(0.08, fl_cos(fl_mul(2.0, phase(i, n))))))))
    // windowed-sinc low-pass FIR with `taps` coefficients, cutoff as a fraction of the sample rate (0 < fc < 0.5)
    fn fir_lowpass(taps: Nat, fc: Float) -> FloatVec =
        let w = hamming(taps) in let m = fl_mul(0.5, fl_of_nat(monus(taps, 1))) in
        let h = tab(taps, fun(i: Nat, let t = fl_sub(fl_of_nat(i), m) in
            fl_mul(unwrap_or(fv_get(w, i), 1.0), if_then_else(fl_eq(t, 0.0), fl_mul(2.0, fc), fl_div(fl_sin(fl_mul(fl_mul(tau(), fc), t)), fl_mul(3.141592653589793, t)))))) in
        let s = fv_sum(h) in if_then_else(fl_eq(s, 0.0), h, fv_scale(h, fl_div(1.0, s)))
    fn fir(h: FloatVec, x: FloatVec) -> FloatVec = unwrap_or(sig_iir(h, unwrap_or(fv_new(1, 1.0), fv_empty()), x), fv_empty())
    fn filter(f: Biquad, x: FloatVec) -> FloatVec = unwrap_or(sig_iir(field(f, b), field(f, a), x), fv_empty())
    fn filter_cascade(fs: List<Biquad>, x: FloatVec) -> FloatVec = fold(fs, x, fun(acc: FloatVec, f: Biquad, filter(f, acc)))
    private fn vec3(a: Float, b: Float, c: Float) -> FloatVec = unwrap_or(fv_from_list(list(a, b, c)), fv_empty())
    // RBJ cookbook biquads; f0 and fs in Hz, q quality factor
    fn lowpass(f0: Float, fs: Float, q: Float) -> Biquad =
        let w = fl_div(fl_mul(tau(), f0), fs) in let al = fl_div(fl_sin(w), fl_mul(2.0, q)) in let c = fl_cos(w) in
        Biquad(vec3(fl_mul(0.5, fl_sub(1.0, c)), fl_sub(1.0, c), fl_mul(0.5, fl_sub(1.0, c))), vec3(fl_add(1.0, al), fl_mul(-2.0, c), fl_sub(1.0, al)))
    fn highpass(f0: Float, fs: Float, q: Float) -> Biquad =
        let w = fl_div(fl_mul(tau(), f0), fs) in let al = fl_div(fl_sin(w), fl_mul(2.0, q)) in let c = fl_cos(w) in
        Biquad(vec3(fl_mul(0.5, fl_add(1.0, c)), fl_neg(fl_add(1.0, c)), fl_mul(0.5, fl_add(1.0, c))), vec3(fl_add(1.0, al), fl_mul(-2.0, c), fl_sub(1.0, al)))
    fn bandpass(f0: Float, fs: Float, q: Float) -> Biquad =
        let w = fl_div(fl_mul(tau(), f0), fs) in let al = fl_div(fl_sin(w), fl_mul(2.0, q)) in let c = fl_cos(w) in
        Biquad(vec3(al, 0.0, fl_neg(al)), vec3(fl_add(1.0, al), fl_mul(-2.0, c), fl_sub(1.0, al)))
    fn notch(f0: Float, fs: Float, q: Float) -> Biquad =
        let w = fl_div(fl_mul(tau(), f0), fs) in let al = fl_div(fl_sin(w), fl_mul(2.0, q)) in let c = fl_cos(w) in
        Biquad(vec3(1.0, fl_mul(-2.0, c), 1.0), vec3(fl_add(1.0, al), fl_mul(-2.0, c), fl_sub(1.0, al)))
    // |H(e^{iω})| of a biquad at frequency f
    fn gain(f: Biquad, freq: Float, fs: Float) -> Float =
        let w = fl_div(fl_mul(tau(), freq), fs) in
        let part = fun(v: FloatVec, k: Nat, fl_mul(unwrap_or(fv_get(v, k), 0.0), fl_cos(fl_mul(fl_of_nat(k), w)))) in
        let parti = fun(v: FloatVec, k: Nat, fl_mul(unwrap_or(fv_get(v, k), 0.0), fl_neg(fl_sin(fl_mul(fl_of_nat(k), w))))) in
        let nr = fl_add(fl_add(part(field(f, b), 0), part(field(f, b), 1)), part(field(f, b), 2)) in let ni = fl_add(fl_add(parti(field(f, b), 0), parti(field(f, b), 1)), parti(field(f, b), 2)) in
        let dr = fl_add(fl_add(part(field(f, a), 0), part(field(f, a), 1)), part(field(f, a), 2)) in let di = fl_add(fl_add(parti(field(f, a), 0), parti(field(f, a), 1)), parti(field(f, a), 2)) in
        fl_sqrt(fl_div(fl_add(fl_mul(nr, nr), fl_mul(ni, ni)), fl_add(fl_mul(dr, dr), fl_mul(di, di))))
    // spectrogram: frames × (w/2+1) powers with a Hann window
    fn spectrogram(x: FloatVec, w: Nat, hop: Nat) -> Option<FloatVec> = sig_stft(x, hann(w), hop)
    fn rms(x: FloatVec) -> Float = if_then_else(nat_eq(fv_len(x), 0), 0.0, fl_sqrt(fl_div(unwrap_or(fv_dot(x, x), 0.0), fl_of_nat(fv_len(x)))))
    fn snr_db(signal: FloatVec, noisy: FloatVec) -> Option<Float> =
        match fv_sub(noisy, signal) { none => none(Float), some(e) => some(fl_mul(20.0, fl_div(fl_log(fl_div(rms(signal), rms(e))), fl_log(10.0)))) }
    private fn soft(x: Float, t: Float) -> Float = if_then_else(fl_le(fl_abs(x), t), 0.0, if_then_else(fl_lt(0.0, x), fl_sub(x, t), fl_add(x, t)))
    // wavelet denoising: soft-threshold the detail coefficients of a `levels`-level transform
    fn wavelet_denoise(x: FloatVec, levels: Nat, kind: Nat, threshold: Float) -> Option<FloatVec> =
        match sig_dwt(x, levels, kind) { none => none(FloatVec), some(c) =>
            let keep = div(fv_len(c), 2 ^ levels) in
            sig_idwt(tab(fv_len(c), fun(i: Nat, let v = unwrap_or(fv_get(c, i), 0.0) in if_then_else(keep > i, v, soft(v, threshold)))), levels, kind) }
    // power spectrum of a rows × cols real image
    fn spectrum_2d(img: FloatVec, rows: Nat, cols: Nat) -> Option<FloatVec> =
        match fft2_forward(to_complex(img), rows, cols) { none => none(FloatVec), some(z) =>
            some(tab(mul(rows, cols), fun(i: Nat, let re = unwrap_or(fv_get(z, mul(2, i)), 0.0) in let im = unwrap_or(fv_get(z, mul(2, i) + 1), 0.0) in fl_add(fl_mul(re, re), fl_mul(im, im))))) }
    fn moving_average(x: FloatVec, n: Nat) -> FloatVec = if_then_else(nat_eq(n, 0), x, fir(unwrap_or(fv_new(n, fl_div(1.0, fl_of_nat(n))), fv_empty()), x))
    fn digest(t: Text) -> Text = sig_sha256(t)

    theorem sha256_of_abc: text_eq(sig_sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
}

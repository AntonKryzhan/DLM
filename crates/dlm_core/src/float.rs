//! v0.114: machine floats (`Float`, IEEE-754 binary64) and dense float vectors
//! (`FloatVec`). The interpreter uses these functions; the native runtime
//! (`dvf_*` in `backend_c_runtime.c`) implements the same definitions:
//! sequential summation from `+0.0`, no fused multiply-add (`-ffp-contract=off`),
//! explicit signed-zero rules for `min`/`max`, a strict literal grammar and the
//! canonical text below. Transcendental functions use the platform libm.
pub const VEC_MAX: usize = 1 << 24;

/// `[-]D+[.D+][(e|E)[+|-]D+]`.
pub fn valid_literal(t: &str) -> bool {
    let b = t.as_bytes();
    let mut i = usize::from(b.first() == Some(&b'-'));
    let digits = |i: &mut usize| {
        let s = *i;
        while *i < b.len() && b[*i].is_ascii_digit() {
            *i += 1;
        }
        *i > s
    };
    if !digits(&mut i) {
        return false;
    }
    if i < b.len() && b[i] == b'.' {
        i += 1;
        if !digits(&mut i) {
            return false;
        }
    }
    if i < b.len() && (b[i] == b'e' || b[i] == b'E') {
        i += 1;
        if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
            i += 1;
        }
        if !digits(&mut i) {
            return false;
        }
    }
    i == b.len() && t.len() <= 4096
}

pub fn parse(t: &str) -> Option<f64> {
    if valid_literal(t) {
        t.parse().ok()
    } else {
        None
    }
}

/// Literal conversion also accepts canonical special values; parsing remains strict.
pub fn literal(t: &str) -> f64 {
    match t {
        "inf" => f64::INFINITY,
        "-inf" => f64::NEG_INFINITY,
        _ => parse(t).unwrap_or(f64::from_bits(0x7ff8_0000_0000_0000)),
    }
}

/// Canonical text from the decimal digits `d1 d2 ...` of `d1.d2... × 10^e`.
pub fn canon(negative: bool, digits: &str, e: i32) -> String {
    let digits = digits.trim_end_matches('0');
    let digits = if digits.is_empty() { "0" } else { digits };
    let n = digits.len() as i32;
    let sign = if negative { "-" } else { "" };
    if (-5..17).contains(&e) {
        if e >= 0 {
            let int_len = e + 1;
            let int: String = if n >= int_len {
                digits[..int_len as usize].to_string()
            } else {
                format!("{digits}{}", "0".repeat((int_len - n) as usize))
            };
            let frac = if n > int_len {
                &digits[int_len as usize..]
            } else {
                "0"
            };
            format!("{sign}{int}.{frac}")
        } else {
            format!("{sign}0.{}{digits}", "0".repeat((-e - 1) as usize))
        }
    } else {
        let rest = if n > 1 { &digits[1..] } else { "0" };
        format!("{sign}{}.{rest}e{e}", &digits[..1])
    }
}

/// Shortest round-tripping decimal text: `1.5`, `2.0`, `-0.0`, `1.0e20`, `nan`, `inf`.
pub fn text(x: f64) -> String {
    if x.is_nan() {
        return "nan".into();
    }
    if x.is_infinite() {
        return if x < 0.0 { "-inf".into() } else { "inf".into() };
    }
    if x == 0.0 {
        return if x.is_sign_negative() {
            "-0.0".into()
        } else {
            "0.0".into()
        };
    }
    let mut s = String::new();
    for p in 0..17 {
        s = format!("{:.*e}", p, x);
        if s.parse::<f64>().ok() == Some(x) {
            break;
        }
    }
    let negative = s.starts_with('-');
    let body = s.trim_start_matches('-');
    let (mantissa, exp) = body.split_once('e').expect("exponent form");
    let digits: String = mantissa.chars().filter(|c| *c != '.').collect();
    canon(negative, &digits, exp.parse().expect("exponent"))
}

pub fn min(a: f64, b: f64) -> f64 {
    if a.is_nan() {
        b
    } else if b.is_nan() || a < b {
        a
    } else if b < a {
        b
    } else if a.is_sign_negative() {
        a
    } else {
        b
    }
}

pub fn max(a: f64, b: f64) -> f64 {
    if a.is_nan() {
        b
    } else if b.is_nan() || a > b {
        a
    } else if b > a {
        b
    } else if a.is_sign_negative() {
        b
    } else {
        a
    }
}

pub fn to_int(x: f64) -> Option<i128> {
    let t = x.trunc();
    (t.is_finite() && t >= -2f64.powi(127) && t < 2f64.powi(127)).then_some(t as i128)
}

pub fn sum(xs: &[f64]) -> f64 {
    xs.iter().fold(0.0, |s, x| s + x)
}

pub fn dot(a: &[f64], b: &[f64]) -> Option<f64> {
    (a.len() == b.len()).then(|| a.iter().zip(b).fold(0.0, |s, (x, y)| s + x * y))
}

pub fn norm(a: &[f64]) -> f64 {
    a.iter().fold(0.0, |s, x| s + x * x).sqrt()
}

pub fn zip(a: &[f64], b: &[f64], f: impl Fn(f64, f64) -> f64) -> Option<Vec<f64>> {
    (a.len() == b.len()).then(|| a.iter().zip(b).map(|(x, y)| f(*x, *y)).collect())
}

pub fn matmul(a: &[f64], b: &[f64], n: u128, m: u128, k: u128) -> Option<Vec<f64>> {
    if n.checked_mul(m)? != a.len() as u128
        || m.checked_mul(k)? != b.len() as u128
        || n.checked_mul(k)? > VEC_MAX as u128
    {
        return None;
    }
    if n == 0 || k == 0 {
        return Some(Vec::new());
    }
    let (n, m, k) = (n as usize, m as usize, k as usize);
    let mut c = vec![0.0; n * k];
    for i in 0..n {
        for j in 0..k {
            let mut s = 0.0;
            for l in 0..m {
                s += a[i * m + l] * b[l * k + j];
            }
            c[i * k + j] = s;
        }
    }
    Some(c)
}

pub fn transpose(a: &[f64], rows: u128, cols: u128) -> Option<Vec<f64>> {
    if rows.checked_mul(cols)? != a.len() as u128 {
        return None;
    }
    if a.is_empty() {
        return Some(Vec::new());
    }
    let (r, c) = (rows as usize, cols as usize);
    let mut out = vec![0.0; a.len()];
    for i in 0..r {
        for j in 0..c {
            out[j * r + i] = a[i * c + j];
        }
    }
    Some(out)
}

/// Solution of `A x = b` (`A` n×n row-major) by Gaussian elimination with partial
/// pivoting (first largest |pivot|); `None` for a shape mismatch or a zero pivot.
pub fn solve(a: &[f64], b: &[f64], n: u128) -> Option<Vec<f64>> {
    if n.checked_mul(n)? != a.len() as u128
        || n != b.len() as u128
        || !a.iter().chain(b).all(|x| x.is_finite())
    {
        return None;
    }
    let n = n as usize;
    let mut m = a.to_vec();
    let mut x = b.to_vec();
    for col in 0..n {
        let mut p = col;
        for r in col + 1..n {
            if m[r * n + col].abs() > m[p * n + col].abs() {
                p = r;
            }
        }
        if m[p * n + col] == 0.0 || m[p * n + col].is_nan() {
            return None;
        }
        if p != col {
            for j in 0..n {
                m.swap(col * n + j, p * n + j);
            }
            x.swap(col, p);
        }
        for r in col + 1..n {
            let f = m[r * n + col] / m[col * n + col];
            for j in col..n {
                let t = f * m[col * n + j];
                m[r * n + j] -= t;
            }
            let t = f * x[col];
            x[r] -= t;
        }
    }
    for col in (0..n).rev() {
        let mut s = x[col];
        for j in col + 1..n {
            let t = m[col * n + j] * x[j];
            s -= t;
        }
        x[col] = s / m[col * n + col];
    }
    x.iter().all(|v| v.is_finite()).then_some(x)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn canonical_text_round_trips() {
        for (x, t) in [
            (1.5, "1.5"),
            (2.0, "2.0"),
            (0.1, "0.1"),
            (-0.0, "-0.0"),
            (1e20, "1.0e20"),
            (1e-7, "1.0e-7"),
            (123456.789, "123456.789"),
            (1e16, "10000000000000000.0"),
            (0.00001, "0.00001"),
            (f64::MAX, "1.7976931348623157e308"),
        ] {
            assert_eq!(text(x), t);
            if x.is_finite() {
                assert_eq!(text(x).replace("e", "e").parse::<f64>().unwrap(), x);
            }
        }
        assert_eq!(text(f64::NAN), "nan");
        assert!(
            valid_literal("-1.5e-3")
                && !valid_literal("1.")
                && !valid_literal(".5")
                && !valid_literal("inf")
                && !valid_literal("0x1p3")
        );
        assert_eq!(min(-0.0, 0.0).to_bits(), (-0.0f64).to_bits());
        assert_eq!(max(-0.0, 0.0).to_bits(), 0.0f64.to_bits());
        assert_eq!(
            matmul(&[1.0, 2.0, 3.0, 4.0], &[5.0, 6.0, 7.0, 8.0], 2, 2, 2).unwrap(),
            vec![19.0, 22.0, 43.0, 50.0]
        );
    }
}

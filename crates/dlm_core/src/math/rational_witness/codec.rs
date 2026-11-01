//! Strict bounded standalone transport. No producer or verifier is invoked here.
use super::{exhausted, invalid, matrix, valid, RankCertificate, WitnessResult};
use crate::math::{
    exact::{ExactBudget, Rational},
    rational_matrix::{Matrix, MAX_AXIS},
};
use std::fmt::Write;
pub const MAX_CERT_TEXT_BYTES: usize = 1 << 20;
struct LimitedText {
    value: String,
}
impl Write for LimitedText {
    fn write_str(&mut self, s: &str) -> std::fmt::Result {
        if s.len() > MAX_CERT_TEXT_BYTES - self.value.len() {
            return Err(std::fmt::Error);
        }
        self.value.push_str(s);
        Ok(())
    }
}
pub fn encode_rank_certificate(c: &RankCertificate, b: &mut ExactBudget) -> WitnessResult<String> {
    valid(c.rank <= MAX_AXIS, "Q rank exceeds transport bound")?;
    let mut out = LimitedText {
        value: String::new(),
    };
    writeln!(&mut out, "DLM-Q-RANK v1\nrank {}", c.rank)
        .map_err(|_| exhausted("Q witness text budget exhausted"))?;
    for (label, a) in [
        ("input", &c.input),
        ("columns", &c.columns),
        ("coordinates", &c.coordinates),
        ("left", &c.left),
        ("right", &c.right),
        ("kernel", &c.kernel),
        ("kernel_left", &c.kernel_left),
    ] {
        matrix(a, b)?;
        write!(&mut out, "matrix {label} {} {}", a.rows(), a.cols())
            .map_err(|_| exhausted("Q witness text budget exhausted"))?;
        for q in a.entries() {
            write!(&mut out, " {q}").map_err(|_| exhausted("Q witness text budget exhausted"))?;
        }
        writeln!(&mut out).map_err(|_| exhausted("Q witness text budget exhausted"))?;
    }
    writeln!(&mut out, "end").map_err(|_| exhausted("Q witness text budget exhausted"))?;
    Ok(out.value)
}
struct Tokens<'a> {
    tokens: std::str::SplitWhitespace<'a>,
}
impl<'a> Tokens<'a> {
    fn next(&mut self) -> WitnessResult<&'a str> {
        self.tokens
            .next()
            .ok_or_else(|| invalid("truncated Q witness"))
    }
    fn expect(&mut self, value: &str) -> WitnessResult<()> {
        valid(
            self.next()? == value,
            "unexpected Q witness token or version",
        )
    }
    fn number(&mut self) -> WitnessResult<usize> {
        let n = self.next()?;
        valid(
            n.bytes().all(|v| v.is_ascii_digit()),
            "invalid Q witness integer",
        )?;
        n.parse().map_err(|_| invalid("Q witness integer overflow"))
    }
    fn matrix(&mut self, label: &str, b: &mut ExactBudget) -> WitnessResult<Matrix> {
        self.expect("matrix")?;
        self.expect(label)?;
        let rows = self.number()?;
        let cols = self.number()?;
        valid(
            rows <= MAX_AXIS && cols <= MAX_AXIS,
            "Q witness matrix axis bound exceeded",
        )?;
        let mut data = Vec::with_capacity(rows * cols);
        for _ in 0..rows * cols {
            data.push(Rational::parse(self.next()?, b)?);
        }
        Ok(Matrix::new(rows, cols, data, b)?)
    }
}
pub fn decode_rank_certificate(text: &str, b: &mut ExactBudget) -> WitnessResult<RankCertificate> {
    if text.len() > MAX_CERT_TEXT_BYTES {
        return Err(exhausted("Q witness text budget exhausted"));
    }
    b.charge(text.len() + 1)?;
    let mut t = Tokens {
        tokens: text.split_whitespace(),
    };
    t.expect("DLM-Q-RANK")?;
    t.expect("v1")?;
    t.expect("rank")?;
    let rank = t.number()?;
    valid(rank <= MAX_AXIS, "Q rank exceeds transport bound")?;
    let input = t.matrix("input", b)?;
    let columns = t.matrix("columns", b)?;
    let coordinates = t.matrix("coordinates", b)?;
    let left = t.matrix("left", b)?;
    let right = t.matrix("right", b)?;
    let kernel = t.matrix("kernel", b)?;
    let kernel_left = t.matrix("kernel_left", b)?;
    t.expect("end")?;
    valid(t.tokens.next().is_none(), "trailing Q witness tokens")?;
    Ok(RankCertificate {
        input,
        rank,
        columns,
        coordinates,
        left,
        right,
        kernel,
        kernel_left,
    })
}

//! Strict bounded text transport. This format is distinct from Nat `.kproof`.
use super::*;
use std::fmt::Write;
const HEADER: &str = "DLM-F2-HOMOLOGY v1\n";
fn write_matrix(out: &mut String, m: &Matrix) {
    write!(out, "matrix {} {} ", m.rows(), m.cols()).unwrap();
    if m.entries().is_empty() {
        out.push('-');
    } else {
        for &b in m.entries() {
            out.push(char::from(b'0' + b));
        }
    }
    out.push('\n');
}
fn write_rank(out: &mut String, w: &RankWitness) {
    write!(out, "rank {}", w.pivots.len()).unwrap();
    for p in &w.pivots {
        write!(out, " {p}").unwrap();
    }
    out.push('\n');
    for m in [&w.reduced, &w.left, &w.left_inverse] {
        write_matrix(out, m);
    }
}
pub fn encode_homology_certificate(
    c: &HomologyCertificate,
    budget: WitnessBudget,
) -> WitnessResult<String> {
    check_homology(c, budget)?;
    let mut out = String::from(HEADER);
    writeln!(out, "boundaries {}", c.boundaries.len()).unwrap();
    for m in &c.boundaries {
        write_matrix(&mut out, m);
    }
    writeln!(out, "groups {}", c.groups.len()).unwrap();
    for g in &c.groups {
        writeln!(out, "group {}", g.degree).unwrap();
        write_matrix(&mut out, &g.representatives);
        for w in [&g.outgoing, &g.incoming, &g.quotient] {
            write_rank(&mut out, w);
        }
    }
    out.push_str("end\n");
    if out.len() > budget.max_text_bytes {
        return Err(exhausted("witness text budget exhausted"));
    }
    Ok(out)
}
struct Reader<'a> {
    tokens: std::str::SplitWhitespace<'a>,
    budget: WitnessBudget,
    matrices: usize,
}
impl<'a> Reader<'a> {
    fn word(&mut self) -> WitnessResult<&'a str> {
        self.tokens
            .next()
            .ok_or_else(|| invalid("truncated homology certificate"))
    }
    fn expect(&mut self, label: &str) -> WitnessResult<()> {
        valid(
            self.word()? == label,
            "unexpected homology certificate token",
        )
    }
    fn number(&mut self) -> WitnessResult<usize> {
        let text = self.word()?;
        valid(
            !text.is_empty() && text.bytes().all(|b| b.is_ascii_digit()),
            "expected unsigned decimal integer",
        )?;
        text.parse()
            .map_err(|_| invalid("certificate integer out of range"))
    }
    fn matrix(&mut self) -> WitnessResult<Matrix> {
        self.expect("matrix")?;
        if self.matrices >= self.budget.max_matrices {
            return Err(exhausted("witness matrix budget exhausted"));
        }
        self.matrices += 1;
        let rows = self.number()?;
        let cols = self.number()?;
        valid(
            rows <= super::super::f2::MAX_AXIS && cols <= super::super::f2::MAX_AXIS,
            "certificate matrix axis budget exceeded",
        )?;
        let data = self.word()?;
        let bits = if rows * cols == 0 {
            valid(data == "-", "empty matrix needs '-' payload")?;
            Vec::new()
        } else {
            valid(
                data.len() == rows * cols && data.bytes().all(|b| b == b'0' || b == b'1'),
                "invalid F2 matrix payload",
            )?;
            data.bytes().map(|b| b - b'0').collect()
        };
        Ok(Matrix::new(rows, cols, bits)?)
    }
    fn rank(&mut self) -> WitnessResult<RankWitness> {
        self.expect("rank")?;
        let count = self.number()?;
        valid(
            count <= super::super::f2::MAX_AXIS,
            "rank pivot count exceeds axis budget",
        )?;
        let mut pivots = Vec::new();
        for _ in 0..count {
            pivots.push(self.number()?);
        }
        Ok(RankWitness {
            reduced: self.matrix()?,
            left: self.matrix()?,
            left_inverse: self.matrix()?,
            pivots,
        })
    }
}
/// Parse only. Call `check_homology` (or the external-claim variant) before use.
pub fn decode_homology_certificate(
    text: &str,
    budget: WitnessBudget,
) -> WitnessResult<HomologyCertificate> {
    if text.len() > budget.max_text_bytes {
        return Err(exhausted("witness text budget exhausted"));
    }
    let body = text
        .strip_prefix(HEADER)
        .ok_or_else(|| invalid("unsupported homology certificate header"))?;
    let mut r = Reader {
        tokens: body.split_whitespace(),
        budget,
        matrices: 0,
    };
    r.expect("boundaries")?;
    let count = r.number()?;
    valid(
        count <= MAX_DEGREES,
        "certificate chain degree budget exceeded",
    )?;
    let mut boundaries = Vec::new();
    for _ in 0..count {
        boundaries.push(r.matrix()?);
    }
    r.expect("groups")?;
    let count = r.number()?;
    valid(
        count <= MAX_DEGREES,
        "certificate homology degree budget exceeded",
    )?;
    let mut groups = Vec::new();
    for _ in 0..count {
        r.expect("group")?;
        let degree = r.number()?;
        groups.push(HomologyWitness {
            degree,
            representatives: r.matrix()?,
            outgoing: r.rank()?,
            incoming: r.rank()?,
            quotient: r.rank()?,
        });
    }
    r.expect("end")?;
    valid(
        r.tokens.next().is_none(),
        "trailing homology certificate tokens",
    )?;
    Ok(HomologyCertificate { boundaries, groups })
}

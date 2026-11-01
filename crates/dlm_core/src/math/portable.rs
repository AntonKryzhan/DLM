//! Canonical portable text of all numeric families (numeric criterion N12).
//!
//! `dlm1:<family>:<payload>` with the family's canonical text as payload:
//! `int` (decimal integer), `rat` (`n/d`), `ivq` (`[lo,hi]`), `cq` (`re;im`),
//! `creal` (program), `ccomplex` (`re | im`). Parsing is strict: the text
//! must be exactly the canonical formatting of the value it denotes, so
//! `format(parse(t)) = t` for every accepted `t`; other versions, unknown
//! families and non-canonical spellings are refused with distinct messages.
use super::ccomplex::CComplex;
use super::complex_q::ComplexQ;
use super::creal::CReal;
use super::exact::{ExactBudget, Integer, Rational};
use super::interval_q::IntervalQ;
use super::{MathError, MathResult};

pub const VERSION: &str = "dlm1";

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum PortableNumber {
    Int(Integer),
    Rat(Rational),
    Interval(IntervalQ),
    ComplexQ(ComplexQ),
    CReal(CReal),
    CComplex(CComplex),
}

impl PortableNumber {
    pub fn family(&self) -> &'static str {
        match self {
            PortableNumber::Int(_) => "int",
            PortableNumber::Rat(_) => "rat",
            PortableNumber::Interval(_) => "ivq",
            PortableNumber::ComplexQ(_) => "cq",
            PortableNumber::CReal(_) => "creal",
            PortableNumber::CComplex(_) => "ccomplex",
        }
    }
    fn payload(&self) -> String {
        match self {
            PortableNumber::Int(x) => x.to_string(),
            PortableNumber::Rat(x) => x.to_string(),
            PortableNumber::Interval(x) => x.to_string(),
            PortableNumber::ComplexQ(x) => x.to_string(),
            PortableNumber::CReal(x) => x.to_string(),
            PortableNumber::CComplex(x) => x.to_string(),
        }
    }
}

pub fn format(x: &PortableNumber) -> String {
    format!("{VERSION}:{}:{}", x.family(), x.payload())
}

pub fn parse(text: &str, b: &mut ExactBudget) -> MathResult<PortableNumber> {
    let Some((version, rest)) = text.split_once(':') else {
        return Err(MathError("invalid portable number".into()));
    };
    if version != VERSION {
        let numbered = version
            .strip_prefix("dlm")
            .is_some_and(|v| !v.is_empty() && v.bytes().all(|c| c.is_ascii_digit()));
        return Err(MathError(
            if numbered {
                "unsupported portable number version"
            } else {
                "invalid portable number"
            }
            .into(),
        ));
    }
    let Some((family, payload)) = rest.split_once(':') else {
        return Err(MathError("invalid portable number".into()));
    };
    let value = match family {
        "int" => {
            let n = super::exact::Natural::parse(payload.strip_prefix('-').unwrap_or(payload), b)?;
            PortableNumber::Int(Integer::from_parts(payload.starts_with('-'), n))
        }
        "rat" => PortableNumber::Rat(Rational::parse(payload, b)?),
        "ivq" => PortableNumber::Interval(IntervalQ::parse(payload, b)?),
        "cq" => {
            let (re, im) = payload
                .split_once(';')
                .ok_or_else(|| MathError("invalid portable number".into()))?;
            PortableNumber::ComplexQ(ComplexQ::from_parts(
                Rational::parse(re, b)?,
                Rational::parse(im, b)?,
                b,
            )?)
        }
        "creal" => PortableNumber::CReal(CReal::parse(payload, b)?),
        "ccomplex" => PortableNumber::CComplex(CComplex::parse(payload, b)?),
        _ => {
            return Err(MathError(format!(
                "unknown portable number family `{family}`"
            )))
        }
    };
    if format(&value) != text {
        return Err(MathError("non-canonical portable number".into()));
    }
    Ok(value)
}

/// `parse`, requiring the given family.
pub fn parse_as(text: &str, family: &str, b: &mut ExactBudget) -> MathResult<PortableNumber> {
    let value = parse(text, b)?;
    if value.family() != family {
        return Err(MathError(format!(
            "portable number is `{}`, expected `{family}`",
            value.family()
        )));
    }
    Ok(value)
}

// Independently checkable scalar witnesses (N15), the host's math::scalar_witness.
// Line: dlmw1|<kind>|<fields>|<digest>, digest = polynomial hash mod 2^61 - 1, base 257, over the bytes before it.
// Kinds: quotient, round, decimal, sqrt (trust "checked": inverse identities and inequalities) and
// enclosure (trust "replayed": recomputed). See docs/SCALAR_WITNESSES.md.
module std.scalar_witness
import std.numeric

theory StdScalarWitness extends StdNumeric {
    record SwFlag(value: Bool, budget: ExactBudget)
    private fn sw_find(n: Nat, s: Text, c: Text, i: Nat) -> Option<Nat> decreasing n fuel 128 =
        if_then_else(nat_eq(n, 0), none(Nat), if_then_else(text_eq(substring(s, i, 1), c), some(i), sw_find(pred(n), s, c, i + 1)))
    // byte of an allowed witness character: printable ASCII without '"' and backslash
    private fn sw_code(c: Text) -> Option<Nat> =
        match sw_find(2, " !", c, 0) { some(i) => some(32 + i), none =>
        match sw_find(57, "#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[", c, 0) { some(i) => some(35 + i), none =>
        match sw_find(34, "]^_`abcdefghijklmnopqrstuvwxyz{|}~", c, 0) { some(i) => some(93 + i), none => none(Nat) } } }
    private fn sw_hash(n: Nat, s: Text, i: Nat, h: Nat) -> Option<Nat> decreasing n fuel 4200 =
        if_then_else(nat_eq(n, 0), some(h), match sw_code(substring(s, i, 1)) { none => none(Nat), some(c) => sw_hash(pred(n), s, i + 1, mod(mul(h, 257) + c, 2305843009213693951)) })
    fn sw_digest(s: Text) -> Option<Nat> = if_then_else(text_length(s) > 4096, none(Nat), sw_hash(text_length(s), s, 0, 0))
    private fn sw_split(n: Nat, s: Text, start: Nat, i: Nat, acc: List<Text>) -> List<Text> decreasing n fuel 4200 =
        if_then_else(nat_eq(n, 0), cons(substring(s, start, monus(i, start)), acc),
            if_then_else(text_eq(substring(s, i, 1), "|"), sw_split(pred(n), s, i + 1, i + 1, cons(substring(s, start, monus(i, start)), acc)), sw_split(pred(n), s, start, i + 1, acc)))
    fn sw_fields(s: Text) -> List<Text> = fold(sw_split(text_length(s), s, 0, 0, empty_list(Text)), empty_list(Text), fun(r: List<Text>, w: Text, cons(w, r)))
    private fn sw_digits(n: Nat, s: Text, i: Nat) -> Bool decreasing n fuel 4200 =
        if_then_else(nat_eq(n, 0), true, and(is_some(text_to_nat(substring(s, i, 1))), sw_digits(pred(n), s, i + 1)))
    // canonical decimal Nat of at most w digits
    private fn sw_canonical(t: Text, w: Nat) -> Bool =
        and(and(text_length(t) > 0, not(text_length(t) > w)), and(sw_digits(text_length(t), t, 0), or(text_eq(t, "0"), not(text_eq(substring(t, 0, 1), "0")))))
    private fn sw_small(t: Text, max: Nat) -> Result<Nat, Text> =
        if_then_else(and(sw_canonical(t, 5), not(unwrap_or(text_to_nat(t), 0) > max)), ok(unwrap_or(text_to_nat(t), 0), Text), err("invalid scalar witness", Nat))
    private fn sw_invalid(e: Text) -> Text = if_then_else(nat_eq(nm_code(e), 4), e, "invalid scalar witness")
    private fn sw_payload(b: ExactBudget, t: Text, family: Text) -> Result<BigTextCalc, Text> =
        match pn_check(b, t) { err(e) => err(sw_invalid(e), BigTextCalc), ok(c) =>
            let prefix = concat("dlm1:", concat(family, ":")) in
            if_then_else(nm_starts(t, prefix), ok(BigTextCalc(substring(t, text_length(prefix), text_length(t)), field(c, budget)), Text), err("invalid scalar witness", BigTextCalc)) }
    private fn sw_rat(b: ExactBudget, t: Text) -> Result<BigRationalCalc, Text> =
        match sw_payload(b, t, "rat") { err(e) => err(e, BigRationalCalc), ok(p) => match bigrat_parse(field(p, budget), field(p, value)) { err(e) => err(sw_invalid(e), BigRationalCalc), ok(v) => ok(v, Text) } }
    private fn sw_int(b: ExactBudget, t: Text) -> Result<BigIntCalc, Text> =
        match sw_payload(b, t, "int") { err(e) => err(e, BigIntCalc), ok(p) => match big_parse(field(p, budget), field(p, value)) { err(e) => err(sw_invalid(e), BigIntCalc), ok(v) => ok(v, Text) } }
    private fn sw_ivq(b: ExactBudget, t: Text) -> Result<IntervalQCalc, Text> =
        match sw_payload(b, t, "ivq") { err(e) => err(e, IntervalQCalc), ok(p) => match ivq_parse(field(p, budget), field(p, value)) { err(e) => err(sw_invalid(e), IntervalQCalc), ok(v) => ok(v, Text) } }
    private fn sw_creal(b: ExactBudget, t: Text) -> Result<CRealCalc, Text> =
        match sw_payload(b, t, "creal") { err(e) => err(e, CRealCalc), ok(p) => match creal_parse(field(p, budget), field(p, value)) { err(e) => err(sw_invalid(e), CRealCalc), ok(v) => ok(v, Text) } }
    private fn sw_mode(t: Text) -> Result<Nat, Text> =
        if_then_else(text_eq(t, "floor"), ok(0, Text), if_then_else(text_eq(t, "ceil"), ok(1, Text), if_then_else(text_eq(t, "trunc"), ok(2, Text),
            if_then_else(text_eq(t, "half_even"), ok(3, Text), err("invalid scalar witness", Nat)))))
    fn sw_mode_name(m: Nat) -> Text = unwrap_or(index(list("floor", "ceil", "trunc", "half_even"), m), "")

    // ---- claims ----
    private fn sw_ord(r: Result<BigCompare, Text>) -> Result<BigCompare, Text> = r
    private fn sw_zero() -> BigRational = BigRational(false, empty_list(Nat), list(1))
    // n = round(q) by mode, by the inequalities on d = q - n
    fn sw_round_holds(b: ExactBudget, q: BigRational, m: Nat, n: BigInt) -> Result<SwFlag, Text> =
        match bigrat_from_ints(b, n, BigInt(false, list(1))) { err(e) => err(e, SwFlag), ok(nq) =>
        match bigrat_sub(field(nq, budget), q, field(nq, value)) { err(e) => err(e, SwFlag), ok(d) =>
        match bigrat_cmp(field(d, budget), field(d, value), sw_zero()) { err(e) => err(e, SwFlag), ok(c0) =>
        match bigrat_cmp(field(c0, budget), field(d, value), BigRational(false, list(1), list(1))) { err(e) => err(e, SwFlag), ok(c1) =>
        match bigrat_cmp(field(c1, budget), field(d, value), BigRational(true, list(1), list(1))) { err(e) => err(e, SwFlag), ok(cm) =>
        match bigrat_cmp(field(cm, budget), field(d, value), BigRational(false, list(1), list(2))) { err(e) => err(e, SwFlag), ok(ch) =>
        match bigrat_cmp(field(ch, budget), field(d, value), BigRational(true, list(1), list(2))) { err(e) => err(e, SwFlag), ok(cl) =>
            let floor = and(not(int_eq(field(c0, order), int(0) - int(1))), int_eq(field(c1, order), int(0) - int(1))) in
            let ceil = and(not(int_eq(field(c0, order), int(1))), int_eq(field(cm, order), int(1))) in
            let even = nat_eq(mod(unwrap_or(head(field(n, words)), 0), 2), 0) in
            let half = and(and(not(int_eq(field(ch, order), int(1))), not(int_eq(field(cl, order), int(0) - int(1)))),
                or(and(int_eq(field(ch, order), int(0) - int(1)), int_eq(field(cl, order), int(1))), even)) in
            ok(SwFlag(if_then_else(nat_eq(m, 0), floor, if_then_else(nat_eq(m, 1), ceil, if_then_else(nat_eq(m, 2), if_then_else(field(q, negative), ceil, floor), half))), field(cl, budget)), Text) } } } } } } }
    private fn sw_le(b: ExactBudget, a: BigRational, c: BigRational) -> Result<SwFlag, Text> =
        match bigrat_cmp(b, a, c) { err(e) => err(e, SwFlag), ok(o) => ok(SwFlag(not(int_eq(field(o, order), int(1))), field(o, budget)), Text) }
    private fn sw_scale(b: ExactBudget, base: Nat, k: Nat) -> Result<BigRationalCalc, Text> =
        match big_pow(b, BigInt(false, list(base)), k) { err(e) => err(e, BigRationalCalc), ok(p) => bigrat_from_ints(field(p, budget), field(p, value), BigInt(false, list(1))) }
    private fn sw_done(kind: Text, trust: Text, r: Result<SwFlag, Text>) -> Result<BigTextCalc, Text> =
        match r { err(e) => err(e, BigTextCalc), ok(f) => if_then_else(field(f, value), ok(BigTextCalc(concat(trust, concat(" ", kind)), field(f, budget)), Text), err(concat("scalar witness claim rejected: ", kind), BigTextCalc)) }
    private fn sw_at(fs: List<Text>, i: Nat) -> Text = unwrap_or(index(fs, i), "")
    private fn sw_quotient_claim(b: ExactBudget, fs: List<Text>) -> Result<BigTextCalc, Text> =
        match sw_rat(b, sw_at(fs, 2)) { err(e) => err(e, BigTextCalc), ok(a) => match sw_rat(field(a, budget), sw_at(fs, 3)) { err(e) => err(e, BigTextCalc), ok(c) =>
        match sw_rat(field(c, budget), sw_at(fs, 4)) { err(e) => err(e, BigTextCalc), ok(q) =>
            sw_done("quotient", "checked", if_then_else(is_empty(field(field(c, value), numerator)), ok(SwFlag(false, field(q, budget)), Text),
                match bigrat_mul(field(q, budget), field(q, value), field(c, value)) { err(e) => err(e, SwFlag), ok(p) =>
                match bigrat_cmp(field(p, budget), field(p, value), field(a, value)) { err(e) => err(e, SwFlag), ok(o) => ok(SwFlag(int_eq(field(o, order), int(0)), field(o, budget)), Text) } })) } } }
    private fn sw_round_claim(b: ExactBudget, fs: List<Text>) -> Result<BigTextCalc, Text> =
        match sw_rat(b, sw_at(fs, 2)) { err(e) => err(e, BigTextCalc), ok(q) => match sw_mode(sw_at(fs, 3)) { err(e) => err(e, BigTextCalc), ok(m) =>
        match sw_int(field(q, budget), sw_at(fs, 4)) { err(e) => err(e, BigTextCalc), ok(n) => sw_done("round", "checked", sw_round_holds(field(n, budget), field(q, value), m, field(n, value))) } } }
    private fn sw_decimal_claim(b: ExactBudget, fs: List<Text>) -> Result<BigTextCalc, Text> =
        match sw_rat(b, sw_at(fs, 2)) { err(e) => err(e, BigTextCalc), ok(q) => match sw_small(sw_at(fs, 3), 4096) { err(e) => err(e, BigTextCalc), ok(digits) =>
        match sw_mode(sw_at(fs, 4)) { err(e) => err(e, BigTextCalc), ok(m) =>
        match cv_parse_decimal(field(q, budget), sw_at(fs, 5)) { err(e) => err(sw_invalid(e), BigTextCalc), ok(d) =>
        match cv_decimal(field(d, budget), field(d, value), digits, 2) { err(e) => err(e, BigTextCalc), ok(t) =>
            if_then_else(not(text_eq(field(t, value), sw_at(fs, 5))), err("invalid scalar witness", BigTextCalc),
            match sw_scale(field(t, budget), 10, digits) { err(e) => err(e, BigTextCalc), ok(s) =>
            match bigrat_mul(field(s, budget), field(d, value), field(s, value)) { err(e) => err(e, BigTextCalc), ok(ds) =>
            match cv_integer_exact(field(ds, budget), field(ds, value)) { err(e) => err(e, BigTextCalc), ok(n) =>
            match bigrat_mul(field(n, budget), field(q, value), field(s, value)) { err(e) => err(e, BigTextCalc), ok(qs) =>
                sw_done("decimal", "checked", sw_round_holds(field(qs, budget), field(qs, value), m, field(n, value))) } } } }) } } } } }
    private fn sw_sqrt_claim(b: ExactBudget, fs: List<Text>) -> Result<BigTextCalc, Text> =
        match sw_rat(b, sw_at(fs, 2)) { err(e) => err(e, BigTextCalc), ok(x) => match sw_small(sw_at(fs, 3), 16384) { err(e) => err(e, BigTextCalc), ok(k) =>
        match sw_ivq(field(x, budget), sw_at(fs, 4)) { err(e) => err(e, BigTextCalc), ok(r) =>
            let lo = field(field(r, value), lo) in let hi = field(field(r, value), hi) in
            sw_done("sqrt", "checked",
            match bigrat_mul(field(r, budget), lo, lo) { err(e) => err(e, SwFlag), ok(l2) =>
            match bigrat_mul(field(l2, budget), hi, hi) { err(e) => err(e, SwFlag), ok(h2) =>
            match sw_le(field(h2, budget), lo, sw_zero()) { err(e) => err(e, SwFlag), ok(lz) =>
            match sw_le(field(lz, budget), field(l2, value), field(x, value)) { err(e) => err(e, SwFlag), ok(lx) =>
            match sw_le(field(lx, budget), sw_zero(), hi) { err(e) => err(e, SwFlag), ok(hz) =>
            match sw_le(field(hz, budget), field(x, value), field(h2, value)) { err(e) => err(e, SwFlag), ok(hx) =>
            match bigrat_sub(field(hx, budget), hi, lo) { err(e) => err(e, SwFlag), ok(w) =>
            match sw_scale(field(w, budget), 2, k) { err(e) => err(e, SwFlag), ok(p) =>
            match bigrat_mul(field(p, budget), field(w, value), field(p, value)) { err(e) => err(e, SwFlag), ok(wp) =>
            match sw_le(field(wp, budget), field(wp, value), BigRational(false, list(2), list(1))) { err(e) => err(e, SwFlag), ok(nw) =>
                ok(SwFlag(and(and(not(field(field(x, value), negative)), or(field(lz, value), field(lx, value))), and(and(field(hz, value), field(hx, value)), field(nw, value))), field(nw, budget)), Text) } } } } } } } } } }) } } }
    private fn sw_enclosure_claim(b: ExactBudget, fs: List<Text>) -> Result<BigTextCalc, Text> =
        match sw_creal(b, sw_at(fs, 2)) { err(e) => err(e, BigTextCalc), ok(x) => match sw_small(sw_at(fs, 3), 4096) { err(e) => err(e, BigTextCalc), ok(n) =>
        match sw_ivq(field(x, budget), sw_at(fs, 4)) { err(e) => err(e, BigTextCalc), ok(r) =>
            sw_done("enclosure", "replayed",
            match creal_approx(field(r, budget), field(x, value), n) { err(e) => err(e, SwFlag), ok(v) =>
            match ivq_text(field(v, budget), field(v, value)) { err(e) => err(e, SwFlag), ok(t1) =>
            match ivq_text(field(t1, budget), field(r, value)) { err(e) => err(e, SwFlag), ok(t2) => ok(SwFlag(text_eq(field(t1, value), field(t2, value)), field(t2, budget)), Text) } } }) } } }

    // Checks a witness line: ok("<trust> <kind>") or the refusal (same messages as the host).
    fn sw_check(b: ExactBudget, text: Text) -> Result<BigTextCalc, Text> =
        if_then_else(text_length(text) > 4096, err("invalid scalar witness", BigTextCalc),
        let fs = sw_fields(text) in
        let version = sw_at(fs, 0) in
        if_then_else(not(text_eq(version, "dlmw1")),
            if_then_else(and(and(text_length(version) > 4, nm_starts(version, "dlmw")), sw_digits(monus(text_length(version), 4), version, 4)),
                err("unsupported scalar witness version", BigTextCalc), err("invalid scalar witness", BigTextCalc)),
        let sealed = sw_at(fs, monus(length(fs), 1)) in
        let body = substring(text, 0, monus(text_length(text), text_length(sealed))) in
        match sw_digest(body) {
            none => err("invalid scalar witness", BigTextCalc),
            some(h) => if_then_else(or(3 > length(fs), not(sw_canonical(sealed, 19))), err("invalid scalar witness", BigTextCalc),
                if_then_else(not(text_eq(nat_to_text(h), sealed)), err("scalar witness digest mismatch", BigTextCalc),
                let kind = sw_at(fs, 1) in
                if_then_else(text_eq(kind, "decimal"), if_then_else(nat_eq(length(fs), 7), sw_decimal_claim(b, fs), err("invalid scalar witness", BigTextCalc)),
                if_then_else(or(or(text_eq(kind, "quotient"), text_eq(kind, "round")), or(text_eq(kind, "sqrt"), text_eq(kind, "enclosure"))),
                    if_then_else(not(nat_eq(length(fs), 6)), err("invalid scalar witness", BigTextCalc),
                    if_then_else(text_eq(kind, "quotient"), sw_quotient_claim(b, fs), if_then_else(text_eq(kind, "round"), sw_round_claim(b, fs),
                    if_then_else(text_eq(kind, "sqrt"), sw_sqrt_claim(b, fs), sw_enclosure_claim(b, fs))))),
                    err(concat("unknown scalar witness kind `", concat(kind, "`")), BigTextCalc)))))
        }))

    // ---- producers ----
    fn sw_seal(fields: List<Text>) -> Text = let body = concat(join(fields, "|"), "|") in concat(body, nat_to_text(unwrap_or(sw_digest(body), 0)))
    private fn sw_line(r: Result<BigTextCalc, Text>, fields: List<Text>) -> Result<BigTextCalc, Text> = match r { err(e) => err(e, BigTextCalc), ok(t) =>
        let line = sw_seal(fields) in
        if_then_else(text_length(line) > 4096, err("invalid scalar witness", BigTextCalc), ok(BigTextCalc(line, field(t, budget)), Text)) }
    fn sw_quotient(b: ExactBudget, a: BigRational, c: BigRational) -> Result<BigTextCalc, Text> =
        match bigrat_div(b, a, c) { err(e) => err(e, BigTextCalc), ok(q) => match pn_rat(field(q, budget), a) { err(e) => err(e, BigTextCalc), ok(ta) =>
        match pn_rat(field(ta, budget), c) { err(e) => err(e, BigTextCalc), ok(tc) => match pn_rat(field(tc, budget), field(q, value)) { err(e) => err(e, BigTextCalc), ok(tq) =>
            sw_line(ok(tq, Text), list("dlmw1", "quotient", field(ta, value), field(tc, value), field(tq, value))) } } } }
    fn sw_round(b: ExactBudget, q: BigRational, m: Nat) -> Result<BigTextCalc, Text> =
        match cv_round(b, q, m) { err(e) => err(e, BigTextCalc), ok(n) => match pn_rat(field(n, budget), q) { err(e) => err(e, BigTextCalc), ok(tq) =>
        match pn_int(field(tq, budget), field(n, value)) { err(e) => err(e, BigTextCalc), ok(tn) => sw_line(ok(tn, Text), list("dlmw1", "round", field(tq, value), sw_mode_name(m), field(tn, value))) } } }
    fn sw_decimal(b: ExactBudget, q: BigRational, digits: Nat, m: Nat) -> Result<BigTextCalc, Text> =
        match cv_decimal(b, q, digits, m) { err(e) => err(e, BigTextCalc), ok(t) => match pn_rat(field(t, budget), q) { err(e) => err(e, BigTextCalc), ok(tq) =>
            sw_line(ok(tq, Text), list("dlmw1", "decimal", field(tq, value), nat_to_text(digits), sw_mode_name(m), field(t, value))) } }
    fn sw_sqrt(b: ExactBudget, x: BigRational, k: Nat) -> Result<BigTextCalc, Text> =
        match ivq_point(b, x) { err(e) => err(e, BigTextCalc), ok(p) => match ivq_sqrt(field(p, budget), field(p, value), k) { err(e) => err(e, BigTextCalc), ok(r) =>
        match pn_rat(field(r, budget), x) { err(e) => err(e, BigTextCalc), ok(tx) => match pn_ivq(field(tx, budget), field(r, value)) { err(e) => err(e, BigTextCalc), ok(tr) =>
            sw_line(ok(tr, Text), list("dlmw1", "sqrt", field(tx, value), nat_to_text(k), field(tr, value))) } } } }
    fn sw_enclosure(b: ExactBudget, x: CReal, n: Nat) -> Result<BigTextCalc, Text> =
        match creal_approx(b, x, n) { err(e) => err(e, BigTextCalc), ok(v) => match pn_creal(field(v, budget), x) { err(e) => err(e, BigTextCalc), ok(tx) =>
        match pn_ivq(field(tx, budget), field(v, value)) { err(e) => err(e, BigTextCalc), ok(tr) =>
            sw_line(ok(tr, Text), list("dlmw1", "enclosure", field(tx, value), nat_to_text(n), field(tr, value))) } } }

    // Closed finite computations, not general soundness proofs.
    theorem tampered_quotient_is_refused: is_err(sw_check(exact_default_budget(), "dlmw1|quotient|dlm1:rat:1/1|dlm1:rat:2/1|dlm1:rat:1/3|0"))
    theorem digest_of_empty_text_is_zero: nat_eq(unwrap_or(sw_digest(""), 1), 0)
}

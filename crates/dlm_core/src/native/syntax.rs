use super::*;
#[derive(Clone, Debug)]
struct Token {
    s: String,
    line: usize,
    string: bool,
}
fn lex(s: &str) -> Result<Vec<Token>, Diagnostic> {
    let b = s.as_bytes();
    let (mut i, mut line) = (0, 1);
    let mut out = Vec::new();
    while i < b.len() {
        if b[i].is_ascii_whitespace() {
            if b[i] == b'\n' {
                line += 1;
            }
            i += 1;
            continue;
        }
        if b[i] == b'#' || (b[i] == b'/' && b.get(i + 1) == Some(&b'/')) {
            while i < b.len() && b[i] != b'\n' {
                i += 1;
            }
            continue;
        }
        if b[i] == b'/' && b.get(i + 1) == Some(&b'*') {
            let l = line;
            i += 2;
            let mut depth = 1;
            while i < b.len() && depth > 0 {
                if b[i] == b'\n' {
                    line += 1;
                }
                if b.get(i..i + 2) == Some(b"/*") {
                    depth += 1;
                    i += 2;
                } else if b.get(i..i + 2) == Some(b"*/") {
                    depth -= 1;
                    i += 2;
                } else {
                    i += 1;
                }
            }
            if depth != 0 {
                return Err(error(l, "unterminated comment"));
            }
            continue;
        }
        let start = i;
        let l = line;
        if b[i] == b'"' {
            i += 1;
            let mut v = String::new();
            let mut closed = false;
            while i < b.len() {
                let c = s[i..].chars().next().unwrap();
                i += c.len_utf8();
                if c == '"' {
                    closed = true;
                    break;
                }
                if c == '\n' {
                    return Err(error(l, "newline in native string"));
                }
                if c == '\\' {
                    let c = *b.get(i).ok_or_else(|| error(l, "unterminated escape"))?;
                    i += 1;
                    v.push(match c {
                        b'n' => '\n',
                        b'r' => '\r',
                        b't' => '\t',
                        b'0' => '\0',
                        b'"' => '"',
                        b'\\' => '\\',
                        _ => return Err(error(l, "unsupported native string escape")),
                    });
                } else {
                    v.push(c);
                }
            }
            if !closed {
                return Err(error(l, "unterminated string"));
            }
            out.push(Token {
                s: v,
                line: l,
                string: true,
            });
            continue;
        }
        if b[i].is_ascii_alphabetic() || b[i] == b'_' {
            i += 1;
            while i < b.len() && (b[i].is_ascii_alphanumeric() || b[i] == b'_') {
                i += 1;
            }
        } else if b[i].is_ascii_digit() {
            let hex = b[i] == b'0' && matches!(b.get(i + 1), Some(b'x' | b'X'));
            i += 1;
            while i < b.len() {
                let c = b[i];
                if c.is_ascii_alphanumeric() || c == b'_' {
                    i += 1;
                } else if c == b'.' && b.get(i + 1) != Some(&b'.') {
                    i += 1;
                } else if !hex
                    && (c == b'+' || c == b'-')
                    && matches!(b.get(i - 1), Some(b'e' | b'E'))
                {
                    i += 1;
                } else {
                    break;
                }
            }
        } else {
            i += 1;
            if i < b.len()
                && matches!(
                    &s[start..i + 1],
                    "->" | "=="
                        | "!="
                        | "<="
                        | ">="
                        | "&&"
                        | "||"
                        | "<<"
                        | ">>"
                        | ".."
                        | "+="
                        | "-="
                        | "*="
                        | "/="
                )
            {
                i += 1;
            }
        }
        if !s.is_char_boundary(i) {
            return Err(error(
                l,
                "native identifiers use ASCII; strings accept UTF-8",
            ));
        }
        out.push(Token {
            s: s[start..i].into(),
            line: l,
            string: false,
        });
    }
    out.push(Token {
        s: String::new(),
        line,
        string: false,
    });
    Ok(out)
}
/// Extract only module-level native blocks. Blanking preserves physical source
/// coordinates for the existing parser, including all UTF-8 byte positions.
pub fn extract(source: &str) -> Result<(String, Unit), Vec<Diagnostic>> {
    if !source.contains("native") {
        return Ok((source.into(), Unit::default()));
    }
    let mut bytes = source.as_bytes().to_vec();
    let original = source.as_bytes();
    let mut unit = Unit::default();
    let (mut i, mut depth, mut line) = (0, 0usize, 1usize);
    while i < original.len() {
        if original[i] == b'\n' {
            line += 1;
            i += 1;
            continue;
        }
        if original[i] == b'#' || original.get(i..i + 2) == Some(b"//") {
            while i < original.len() && original[i] != b'\n' {
                i += 1;
            }
            continue;
        }
        if original.get(i..i + 2) == Some(b"/*") {
            i += 2;
            let mut n = 1;
            while i < original.len() && n > 0 {
                if original[i] == b'\n' {
                    line += 1;
                }
                if original.get(i..i + 2) == Some(b"/*") {
                    n += 1;
                    i += 2;
                } else if original.get(i..i + 2) == Some(b"*/") {
                    n -= 1;
                    i += 2;
                } else {
                    i += 1;
                }
            }
            continue;
        }
        if original[i] == b'"' {
            i += 1;
            while i < original.len() {
                if original[i] == b'\n' {
                    line += 1;
                }
                if original[i] == b'\\' {
                    i = (i + 2).min(original.len());
                    continue;
                }
                if original[i] == b'"' {
                    i += 1;
                    break;
                }
                i += 1;
            }
            continue;
        }
        if original[i].is_ascii_alphabetic() || original[i] == b'_' {
            let start = i;
            i += 1;
            while i < original.len() && (original[i].is_ascii_alphanumeric() || original[i] == b'_')
            {
                i += 1;
            }
            if depth == 0 && &source[start..i] == "native" {
                let mut open = i;
                while open < original.len() && original[open].is_ascii_whitespace() {
                    open += 1;
                }
                if original.get(open) == Some(&b'{') {
                    let mut end = open + 1;
                    let (mut braces, mut quote, mut comment) = (1usize, false, false);
                    while end < original.len() && braces > 0 {
                        let c = original[end];
                        if comment {
                            if c == b'\n' {
                                comment = false;
                            }
                            end += 1;
                            continue;
                        }
                        if quote {
                            if c == b'\\' {
                                end = (end + 2).min(original.len());
                                continue;
                            }
                            if c == b'"' {
                                quote = false;
                            }
                            end += 1;
                            continue;
                        }
                        if original.get(end..end + 2) == Some(b"/*") {
                            end += 2;
                            let mut n = 1;
                            while end < original.len() && n > 0 {
                                if original.get(end..end + 2) == Some(b"/*") {
                                    n += 1;
                                    end += 2;
                                } else if original.get(end..end + 2) == Some(b"*/") {
                                    n -= 1;
                                    end += 2;
                                } else {
                                    end += 1;
                                }
                            }
                            continue;
                        }
                        if c == b'#' || original.get(end..end + 2) == Some(b"//") {
                            comment = true;
                            end += 1;
                            continue;
                        }
                        if c == b'"' {
                            quote = true;
                        } else if c == b'{' {
                            braces += 1;
                        } else if c == b'}' {
                            braces -= 1;
                        }
                        end += 1;
                    }
                    if braces != 0 {
                        return Err(vec![error(line, "unclosed native block")]);
                    }
                    let prefix = source[..open + 1].bytes().filter(|b| *b == b'\n').count();
                    let native_source =
                        format!("{}{}", "\n".repeat(prefix), &source[open + 1..end]);
                    let mut p = Parser {
                        ts: lex(&native_source).map_err(|d| vec![d])?,
                        i: 0,
                        depth: 0,
                    };
                    let u = p.unit().map_err(|d| vec![d])?;
                    unit.records.extend(u.records);
                    unit.functions.extend(u.functions);
                    unit.trusted.extend(u.trusted);
                    for byte in &mut bytes[start..end] {
                        if *byte != b'\n' && *byte != b'\r' {
                            *byte = b' ';
                        }
                    }
                    line += source[i..end].bytes().filter(|b| *b == b'\n').count();
                    i = end;
                    continue;
                }
            }
            continue;
        }
        if original[i] == b'{' {
            depth += 1;
        }
        if original[i] == b'}' {
            depth = depth.saturating_sub(1);
        }
        i += 1;
    }
    Ok((String::from_utf8(bytes).unwrap(), unit))
}
struct Parser {
    ts: Vec<Token>,
    i: usize,
    depth: usize,
}
impl Parser {
    fn peek(&self) -> &str {
        &self.ts[self.i].s
    }
    fn line(&self) -> usize {
        self.ts[self.i].line
    }
    fn take(&mut self) -> Token {
        let t = self.ts[self.i].clone();
        if self.i + 1 < self.ts.len() {
            self.i += 1;
        }
        t
    }
    fn eat(&mut self, s: &str) -> bool {
        if self.peek() == s && !self.ts[self.i].string {
            self.take();
            true
        } else {
            false
        }
    }
    fn need(&mut self, s: &str) -> Result<(), Diagnostic> {
        if self.eat(s) {
            Ok(())
        } else {
            Err(error(
                self.line(),
                format!("expected '{s}', found '{}'", self.peek()),
            ))
        }
    }
    fn ident(&mut self) -> Result<String, Diagnostic> {
        let t = self.take();
        if !t.string
            && t.s
                .as_bytes()
                .first()
                .is_some_and(|b| b.is_ascii_alphabetic() || *b == b'_')
            && t.s.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')
        {
            Ok(t.s)
        } else {
            Err(error(t.line, "expected identifier"))
        }
    }
    fn string(&mut self) -> Result<String, Diagnostic> {
        let t = self.take();
        if t.string {
            Ok(t.s)
        } else {
            Err(error(t.line, "expected string literal"))
        }
    }
    fn uint(&mut self) -> Result<usize, Diagnostic> {
        let t = self.take();
        t.s.parse()
            .map_err(|_| error(t.line, "expected integer constant"))
    }
    fn unit(&mut self) -> Result<Unit, Diagnostic> {
        let mut u = Unit::default();
        while !self.eat("}") {
            if self.peek().is_empty() {
                return Err(error(self.line(), "unclosed native block"));
            }
            if self.eat("trust") {
                u.trusted.insert(self.string()?);
                self.need(";")?;
                continue;
            }
            if self.peek() == "record" || self.peek() == "union" {
                u.records.push(self.record()?);
                continue;
            }
            u.functions.push(self.function()?);
        }
        Ok(u)
    }
    fn record(&mut self) -> Result<Record, Diagnostic> {
        let line = self.line();
        let union = self.take().s == "union";
        let name = self.ident()?;
        let (mut packed, mut align, mut version) = (false, None, 1);
        self.need("repr")?;
        self.need("(")?;
        self.need("C")?;
        while self.eat(",") {
            if self.eat("packed") {
                packed = true;
            } else if self.eat("align") {
                self.need("=")?;
                align = Some(self.uint()?);
            } else if self.eat("version") {
                self.need("=")?;
                version = u32::try_from(self.uint()?)
                    .map_err(|_| error(line, "ABI version out of range"))?;
            } else {
                return Err(error(self.line(), "unknown representation option"));
            }
        }
        self.need(")")?;
        self.need("{")?;
        let mut fields = Vec::new();
        while !self.eat("}") {
            let n = self.ident()?;
            self.need(":")?;
            let t = self.ty()?;
            self.need(";")?;
            fields.push((n, t));
        }
        self.eat(";");
        Ok(Record {
            name,
            fields,
            union,
            packed,
            align,
            version,
            line,
            source: crate::SourceFile::default(),
        })
    }
    fn function(&mut self) -> Result<Function, Diagnostic> {
        let line = self.line();
        let mut export = None;
        let mut library = None;
        let mut ordinary = None;
        let mut unsafe_ = false;
        if self.eat("export") {
            export = Some(if self.ts[self.i].string {
                self.string()?
            } else {
                String::new()
            });
        }
        if self.eat("extern") {
            if self.string()? != "C" {
                return Err(error(line, "only the C calling convention is supported"));
            }
            library = Some(String::new());
            unsafe_ = true;
        }
        if self.eat("unsafe") {
            unsafe_ = true;
        }
        if self.eat("ordinary") {
            ordinary = Some(self.string()?);
        }
        self.need("fn")?;
        let name = self.ident()?;
        if export.as_ref().is_some_and(String::is_empty) {
            export = Some(name.clone());
        }
        self.need("(")?;
        let mut params = Vec::new();
        if !self.eat(")") {
            loop {
                let n = self.ident()?;
                self.need(":")?;
                params.push((n, self.ty()?));
                if self.eat(")") {
                    break;
                }
                self.need(",")?;
            }
        }
        self.need("->")?;
        let ret = self.ty()?;
        let borrow_result = if self.eat("borrows") {
            self.need("(")?;
            let n = self.ident()?;
            self.need(")")?;
            Some(n)
        } else {
            None
        };
        if self.eat("from") {
            if library.is_none() {
                return Err(error(line, "from is valid only for extern functions"));
            }
            library = Some(self.string()?);
        }
        let mut guarantees = BTreeSet::new();
        if self.eat("guarantees") {
            self.need("(")?;
            loop {
                guarantees.insert(self.ident()?);
                if self.eat(")") {
                    break;
                }
                self.need(",")?;
            }
        }
        let body = if library.is_some() || ordinary.is_some() {
            self.need(";")?;
            None
        } else {
            Some(self.block()?)
        };
        Ok(Function {
            name,
            params,
            ret,
            borrow_result,
            body,
            export,
            library,
            ordinary,
            unsafe_,
            guarantees,
            line,
            source: crate::SourceFile::default(),
        })
    }
    fn ty(&mut self) -> Result<Ty, Diagnostic> {
        if self.depth >= 128 {
            return Err(error(self.line(), "native type nesting exceeds 128"));
        }
        self.depth += 1;
        let result = self.ty_inner();
        self.depth -= 1;
        result
    }
    fn ty_inner(&mut self) -> Result<Ty, Diagnostic> {
        if self.eat("&") {
            let m = self.eat("mut");
            return Ok(Ty::Ref(Box::new(self.ty()?), m));
        }
        let line = self.line();
        let n = self.ident()?;
        if let Some(s) = Scalar::parse(&n) {
            return Ok(Ty::Scalar(s));
        }
        match n.as_str() {
            "Bool" => return Ok(Ty::Bool),
            "Unit" => return Ok(Ty::Unit),
            "Utf8" => return Ok(Ty::Str),
            "Window" => return Ok(Ty::Window),
            "OpaqueHandle" => return Ok(Ty::Handle),
            "AtomicBool" => return Ok(Ty::Atomic(Box::new(Ty::Bool))),
            "AtomicU32" => return Ok(Ty::Atomic(Box::new(Ty::Scalar(Scalar::U32)))),
            "AtomicU64" => return Ok(Ty::Atomic(Box::new(Ty::Scalar(Scalar::U64)))),
            "AtomicF32" => return Ok(Ty::Atomic(Box::new(Ty::Scalar(Scalar::F32)))),
            _ => {}
        }
        if !self.eat("<") {
            return Ok(Ty::Record(n));
        }
        if n == "FnPtr" {
            let mut a = vec![self.ty()?];
            while self.eat(",") {
                a.push(self.ty()?);
            }
            self.close_angle()?;
            let r = a.pop().unwrap();
            return Ok(Ty::FnPtr(a, Box::new(r)));
        }
        let t = self.ty()?;
        let r = match n.as_str() {
            "Array" => {
                self.need(",")?;
                Ty::Array(Box::new(t), self.uint()?)
            }
            "Simd" => {
                self.need(",")?;
                let s = t
                    .scalar()
                    .ok_or_else(|| error(line, "SIMD requires a machine scalar"))?;
                Ty::Simd(s, self.uint()?)
            }
            "ThreadBound" => Ty::ThreadBound(Box::new(t)),
            "Resource" => {
                self.need(",")?;
                Ty::Resource(Box::new(t), self.ident()?)
            }
            "Split" => Ty::Split(Box::new(t)),
            "Buffer" => Ty::Buffer(Box::new(t)),
            "Box" => Ty::Boxed(Box::new(t)),
            "Slice" => Ty::Slice(Box::new(t), false),
            "MutSlice" => Ty::Slice(Box::new(t), true),
            "Ref" => Ty::Ref(Box::new(t), false),
            "MutRef" => Ty::Ref(Box::new(t), true),
            "Ptr" => Ty::Ptr(Box::new(t)),
            "NonNull" => Ty::NonNull(Box::new(t)),
            "BorrowedPtr" => Ty::BorrowedPtr(Box::new(t)),
            "ForeignBuffer" => Ty::ForeignBuffer(Box::new(t)),
            "Atomic" => Ty::Atomic(Box::new(t)),
            _ => return Err(error(line, format!("unknown native type constructor {n}"))),
        };
        self.close_angle()?;
        Ok(r)
    }
    fn close_angle(&mut self) -> Result<(), Diagnostic> {
        if self.peek() == ">=" {
            let mut t = self.ts[self.i].clone();
            t.s = ">".into();
            self.ts[self.i].s = "=".into();
            self.ts.insert(self.i, t);
        }
        if self.peek() == ">>" {
            let mut t = self.ts[self.i].clone();
            t.s = ">".into();
            self.ts[self.i].s = ">".into();
            self.ts.insert(self.i, t);
        }
        self.need(">")
    }
    fn block(&mut self) -> Result<Vec<Stmt>, Diagnostic> {
        self.need("{")?;
        let mut s = Vec::new();
        while !self.eat("}") {
            if self.peek().is_empty() {
                return Err(error(self.line(), "unclosed statement block"));
            }
            s.push(self.stmt()?);
        }
        Ok(s)
    }
    fn stmt(&mut self) -> Result<Stmt, Diagnostic> {
        let l = self.line();
        if self.eat("let") {
            let mutable = self.eat("mut");
            let name = self.ident()?;
            self.need(":")?;
            let ty = self.ty()?;
            self.need("=")?;
            let value = self.expr(0)?;
            self.need(";")?;
            return Ok(Stmt::Let {
                name,
                ty,
                mutable,
                value,
                line: l,
            });
        }
        if self.eat("return") {
            let v = if self.peek() == ";" {
                None
            } else {
                Some(self.expr(0)?)
            };
            self.need(";")?;
            return Ok(Stmt::Return(v, l));
        }
        if self.eat("if") {
            let c = self.expr(0)?;
            let a = self.block()?;
            let b = if self.eat("else") {
                if self.peek() == "if" {
                    vec![self.stmt()?]
                } else {
                    self.block()?
                }
            } else {
                vec![]
            };
            return Ok(Stmt::If(c, a, b));
        }
        if self.eat("while") {
            let c = self.expr(0)?;
            return Ok(Stmt::While(c, self.block()?));
        }
        if self.eat("for") {
            let n = self.ident()?;
            self.need("in")?;
            let a = self.expr(0)?;
            self.need("..")?;
            let b = self.expr(0)?;
            return Ok(Stmt::For(n, a, b, self.block()?));
        }
        if self.eat("break") {
            self.need(";")?;
            return Ok(Stmt::Break(l));
        }
        if self.eat("continue") {
            self.need(";")?;
            return Ok(Stmt::Continue(l));
        }
        if self.eat("unsafe") {
            return Ok(Stmt::Unsafe(self.block()?));
        }
        if self.peek() == "{" {
            return Ok(Stmt::Block(self.block()?));
        }
        let e = self.expr(0)?;
        if self.eat("=") {
            let v = self.expr(0)?;
            self.need(";")?;
            return Ok(Stmt::Set(e, v));
        }

        self.need(";")?;
        Ok(Stmt::Expr(e))
    }
    fn type_call(&self) -> bool {
        if self.peek() != "<" {
            return false;
        }
        let mut depth = 0isize;
        for (i, t) in self.ts.iter().enumerate().skip(self.i) {
            if t.string {
                return false;
            }
            match t.s.as_str() {
                "<" => depth += 1,
                ">" => depth -= 1,
                ">>" => depth -= 2,
                "," | "&" => {}
                s if s.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_') && !s.is_empty() => {
                }
                _ => return false,
            }
            if depth <= 0 {
                return depth == 0 && self.ts.get(i + 1).is_some_and(|t| t.s == "(");
            }
        }
        false
    }
    fn expr(&mut self, min: u8) -> Result<Expr, Diagnostic> {
        if self.depth >= 128 {
            return Err(error(self.line(), "native expression nesting exceeds 128"));
        }
        self.depth += 1;
        let result = self.expr_inner(min);
        self.depth -= 1;
        result
    }
    fn expr_inner(&mut self, min: u8) -> Result<Expr, Diagnostic> {
        let l = self.line();
        let mut a = if self.eat("&") {
            let m = self.eat("mut");
            Expr::new(Op::Borrow(Box::new(self.expr(22)?), m), l)
        } else if self.eat("move") {
            Expr::new(Op::Move(Box::new(self.expr(22)?)), l)
        } else if matches!(self.peek(), "-" | "!" | "~" | "*") {
            let op = self.take().s;
            Expr::new(Op::Unary(op, Box::new(self.expr(22)?)), l)
        } else if self.eat("(") {
            let e = self.expr(0)?;
            self.need(")")?;
            e
        } else {
            let t = self.take();
            if t.string {
                Expr::new(Op::Text(t.s), l)
            } else if t.s == "true" || t.s == "false" {
                Expr::new(Op::Bool(t.s == "true"), l)
            } else if t.s.as_bytes().first().is_some_and(u8::is_ascii_digit) {
                Expr::new(Op::Number(t.s), l)
            } else if t
                .s
                .as_bytes()
                .first()
                .is_some_and(|b| b.is_ascii_alphabetic() || *b == b'_')
            {
                Expr::new(Op::Name(t.s), l)
            } else {
                return Err(error(l, format!("expected expression, found '{}'", t.s)));
            }
        };
        loop {
            if self.eat(".") {
                let n = self.ident()?;
                a = Expr::new(Op::Field(Box::new(a), n), l);
                continue;
            }
            if self.eat("[") {
                let i = self.expr(0)?;
                self.need("]")?;
                a = Expr::new(Op::Index(Box::new(a), Box::new(i)), l);
                continue;
            }
            let mut generics = Vec::new();
            if matches!(&a.op, Op::Name(_)) && self.type_call() {
                self.need("<")?;
                generics.push(self.ty()?);
                while self.eat(",") {
                    generics.push(self.ty()?);
                }
                self.close_angle()?;
            }
            if self.eat("(") {
                let mut args = Vec::new();
                if !self.eat(")") {
                    loop {
                        args.push(self.expr(0)?);
                        if self.eat(")") {
                            break;
                        }
                        self.need(",")?;
                    }
                }
                a = match a.op {
                    Op::Name(n) => Expr::new(Op::Call(n, generics, args), l),
                    _ => Expr::new(Op::Invoke(Box::new(a), args), l),
                };
                continue;
            }
            let prec = match self.peek() {
                "||" => 1,
                "&&" => 3,
                "|" => 5,
                "^" => 7,
                "&" => 9,
                "==" | "!=" => 11,
                "<" | ">" | "<=" | ">=" => 13,
                "<<" | ">>" => 15,
                "+" | "-" => 17,
                "*" | "/" | "%" => 19,
                _ => break,
            };
            if prec < min {
                break;
            }
            let op = self.take().s;
            let b = self.expr(prec + 1)?;
            a = Expr::new(Op::Binary(op, Box::new(a), Box::new(b)), l);
        }
        Ok(a)
    }
}

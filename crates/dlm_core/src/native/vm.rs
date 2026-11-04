//! Independent reference execution for the checked native core. Foreign ABI
//! addresses are deliberately not synthesized by the interpreter.
use super::*;
use std::cell::RefCell;
use std::rc::Rc;
type Cell = Rc<RefCell<Value>>;
pub struct WindowObject {
    table: Rc<RefCell<crate::window::WindowTable>>,
    id: u128,
}
impl std::fmt::Debug for WindowObject {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_tuple("Window").field(&self.id).finish()
    }
}
impl Drop for WindowObject {
    fn drop(&mut self) {
        let _ = self.table.borrow_mut().close(self.id);
    }
}

pub struct ResourceObject {
    value: RefCell<Option<Cell>>,
    ty: Ty,
    finalizer: String,
    mir: Rc<Mir>,
    errors: Rc<RefCell<Vec<String>>>,
}
impl std::fmt::Debug for ResourceObject {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Resource")
            .field("finalizer", &self.finalizer)
            .field("active", &self.value.borrow().is_some())
            .finish()
    }
}
impl Drop for ResourceObject {
    fn drop(&mut self) {
        if let Some(payload) = self.value.get_mut().take() {
            let mut vm = Vm::new(&self.mir).with_step_limit(1_000_000);
            vm.owned_mir = Some(self.mir.clone());
            if let Err(error) = vm.call(&self.finalizer, vec![Value::Ref(payload.clone())]) {
                self.errors.borrow_mut().push(error);
            }
            dispose_cell(&payload, &self.ty, &self.mir);
        }
    }
}
fn dispose_cell(p: &Cell, t: &Ty, m: &Mir) {
    if !check::owned(t, &m.unit.records) {
        return;
    }
    let value = p.replace(Value::Null);
    match (value, t) {
        (Value::Record(fields), Ty::Record(name)) => {
            let r = m.unit.records.iter().find(|r| r.name == *name).unwrap();
            for (n, t) in r.fields.iter().rev() {
                if let Some(p) = fields.get(n) {
                    dispose_cell(p, t, m);
                }
            }
        }
        (Value::Seq(values), Ty::Buffer(t) | Ty::Array(t, _)) => {
            for p in values.borrow().iter() {
                dispose_cell(p, t, m);
            }
        }
        (Value::Ref(p), Ty::Boxed(t)) | (Value::Bound(p, _), Ty::ThreadBound(t)) => {
            dispose_cell(&p, t, m)
        }
        _ => {}
    }
}
#[derive(Clone, Debug)]
pub enum Value {
    U(u64),
    I(i64),
    F(f64),
    F32(u32),
    Bool(bool),
    Unit,
    Text(String),
    Seq(Rc<RefCell<Vec<Cell>>>),
    Record(BTreeMap<String, Cell>),
    Ref(Cell),
    Slice(Rc<RefCell<Vec<Cell>>>, usize, usize),
    Null,
    Function(String),
    Window(Rc<WindowObject>),
    Bound(Cell, std::thread::ThreadId),
    Resource(Rc<ResourceObject>),
    Atomic(Cell),
}
fn cell(v: Value) -> Cell {
    Rc::new(RefCell::new(v))
}
impl Value {
    pub fn unsigned(&self) -> Result<u64, String> {
        match self {
            Self::U(n) => Ok(*n),
            Self::I(n) if *n >= 0 => Ok(*n as u64),
            _ => Err("expected unsigned scalar".into()),
        }
    }
    fn bool(&self) -> Result<bool, String> {
        if let Self::Bool(b) = self {
            Ok(*b)
        } else {
            Err("expected Bool".into())
        }
    }
    fn number(&self) -> Result<f64, String> {
        match self {
            Self::U(n) => Ok(*n as f64),
            Self::I(n) => Ok(*n as f64),
            Self::F(n) => Ok(*n),
            Self::F32(n) => Ok(f32::from_bits(*n) as f64),
            _ => Err("expected numeric value".into()),
        }
    }
    fn text(&self) -> String {
        match self {
            Self::U(n) => n.to_string(),
            Self::I(n) => n.to_string(),
            Self::F(n) => float_text(*n),
            Self::F32(n) => Value::F(f32::from_bits(*n) as f64).text(),
            Self::Bool(b) => b.to_string(),
            Self::Text(s) => s.clone(),
            _ => "unit".into(),
        }
    }
}
#[derive(Debug)]
enum Flow {
    Next,
    Return(Value),
    Break,
    Continue,
}
pub struct Vm<'a> {
    pub(super) mir: &'a Mir,
    owned_mir: Option<Rc<Mir>>,
    cleanup_errors: Rc<RefCell<Vec<String>>>,
    pub output: Vec<String>,
    steps: u64,
    limit: u64,
    depth: usize,
    windows: Rc<RefCell<crate::window::WindowTable>>,
}
impl<'a> Vm<'a> {
    pub fn new(mir: &'a Mir) -> Self {
        Self {
            mir,
            owned_mir: None,
            cleanup_errors: Rc::new(RefCell::new(Vec::new())),
            output: vec![],
            steps: 0,
            limit: 100_000_000,
            depth: 0,
            windows: Rc::new(RefCell::new(Default::default())),
        }
    }
    pub fn with_step_limit(mut self, n: u64) -> Self {
        self.limit = n;
        self
    }
    fn step(&mut self) -> Result<(), String> {
        if let Some(error) = self.cleanup_errors.borrow_mut().pop() {
            return Err(error);
        }
        self.steps += 1;
        if self.steps > self.limit {
            Err("interpreter work budget exceeded".into())
        } else {
            Ok(())
        }
    }
    pub fn zero(&self, t: &Ty) -> Value {
        match t {
            Ty::Scalar(Scalar::F32) => Value::F32(0),
            Ty::Scalar(s) if s.float() => Value::F(0.0),
            Ty::Scalar(s) if s.signed() => Value::I(0),
            Ty::Scalar(_) => Value::U(0),
            Ty::Bool => Value::Bool(false),
            Ty::Unit => Value::Unit,
            Ty::Str => Value::Text(String::new()),
            Ty::Array(t, n) => Value::Seq(Rc::new(RefCell::new(
                (0..*n).map(|_| cell(self.zero(t))).collect(),
            ))),
            Ty::Buffer(_) => Value::Seq(Rc::new(RefCell::new(vec![]))),
            Ty::Split(t) => Value::Record(
                [
                    ("left".into(), cell(self.zero(&Ty::Slice(t.clone(), true)))),
                    ("right".into(), cell(self.zero(&Ty::Slice(t.clone(), true)))),
                ]
                .into_iter()
                .collect(),
            ),
            Ty::Slice(..) | Ty::ForeignBuffer(_) => {
                Value::Slice(Rc::new(RefCell::new(vec![])), 0, 0)
            }
            Ty::Record(n) => Value::Record(
                self.mir
                    .unit
                    .records
                    .iter()
                    .find(|r| r.name == *n)
                    .unwrap()
                    .fields
                    .iter()
                    .map(|(n, t)| (n.clone(), cell(self.zero(t))))
                    .collect(),
            ),
            Ty::Simd(s, n) => Value::Seq(Rc::new(RefCell::new(
                (0..*n).map(|_| cell(self.zero(&Ty::Scalar(*s)))).collect(),
            ))),
            Ty::Atomic(t) => Value::Atomic(cell(self.zero(t))),
            Ty::ThreadBound(t) => Value::Bound(cell(self.zero(t)), std::thread::current().id()),
            _ => Value::Null,
        }
    }
    pub fn call(&mut self, name: &str, args: Vec<Value>) -> Result<Value, String> {
        self.step()?;
        if self.depth >= 1024 {
            return Err("native interpreter call depth exceeded".into());
        }
        let f = self
            .mir
            .unit
            .functions
            .iter()
            .find(|f| f.name == name)
            .ok_or_else(|| format!("unknown native function {name}"))?
            .clone();
        if f.params.len() != args.len() {
            return Err("native argument arity mismatch".into());
        }
        if f.ordinary.is_some() {
            return super::bridge::call(self, &f, args);
        }
        let body = f.body.as_ref().ok_or_else(|| {
            format!("foreign function {name} requires a native host; use dlm build")
        })?;
        let mut env: BTreeMap<String, Cell> = f
            .params
            .iter()
            .zip(args)
            .map(|((n, _), v)| (n.clone(), cell(v)))
            .collect();
        self.depth += 1;
        let result = self.block(body, &mut env);
        for (name, ty) in f.params.iter().rev() {
            if let Some(value) = env.remove(name) {
                dispose_cell(&value, ty, self.mir);
            }
        }
        self.depth -= 1;
        if result.is_ok() {
            if let Some(error) = self.cleanup_errors.borrow_mut().pop() {
                return Err(error);
            }
        }
        match result? {
            Flow::Return(v) => Ok(v),
            Flow::Next if f.ret == Ty::Unit => Ok(Value::Unit),
            _ => Err("missing native return".into()),
        }
    }
    pub fn run(&mut self) -> Result<(), String> {
        if self.mir.unit.functions.iter().any(|f| f.name == "main") {
            self.call("main", vec![])?;
        }
        Ok(())
    }
    fn copy_value(&self, v: &Value, t: &Ty) -> Value {
        match (v, t) {
            (Value::Record(fields), Ty::Record(n)) => {
                let r = self.mir.unit.records.iter().find(|r| r.name == *n).unwrap();
                Value::Record(
                    r.fields
                        .iter()
                        .map(|(n, t)| (n.clone(), cell(self.copy_value(&fields[n].borrow(), t))))
                        .collect(),
                )
            }
            (Value::Seq(values), Ty::Array(t, _)) => Value::Seq(Rc::new(RefCell::new(
                values
                    .borrow()
                    .iter()
                    .map(|v| cell(self.copy_value(&v.borrow(), t)))
                    .collect(),
            ))),
            (Value::Seq(values), Ty::Simd(s, _)) => Value::Seq(Rc::new(RefCell::new(
                values
                    .borrow()
                    .iter()
                    .map(|v| cell(self.copy_value(&v.borrow(), &Ty::Scalar(*s))))
                    .collect(),
            ))),
            _ => v.clone(),
        }
    }
    fn place(&mut self, e: &Expr, env: &mut BTreeMap<String, Cell>) -> Result<Cell, String> {
        match &e.op {
            Op::Name(n) => env
                .get(n)
                .cloned()
                .ok_or_else(|| format!("unknown native local {n}")),
            Op::Field(a, n) => {
                let mut v = if place_exists(a) {
                    self.place(a, env)?.borrow().clone()
                } else {
                    self.expr(a, env)?
                };
                if let Value::Ref(c) = v {
                    v = c.borrow().clone();
                }
                let Value::Record(r) = v else {
                    return Err("field of non-record".into());
                };
                r.get(n).cloned().ok_or_else(|| "unknown field".into())
            }
            Op::Index(a, i) | Op::IndexProven(a, i) => {
                let v = if place_exists(a) {
                    self.place(a, env)?.borrow().clone()
                } else {
                    self.expr(a, env)?
                };
                let n = self.expr(i, env)?.unsigned()? as usize;
                let (data, start, len) = self.storage(v)?;
                if n >= len {
                    return Err("index out of bounds".into());
                }
                let c = data.borrow()[start + n].clone();
                Ok(c)
            }
            Op::Unary(op, a) if op == "*" => match self.expr(a, env)? {
                Value::Ref(c) => Ok(c),
                _ => Err("null or foreign pointer in interpreter".into()),
            },
            _ => Err("not a native place".into()),
        }
    }
    fn storage(&self, mut v: Value) -> Result<(Rc<RefCell<Vec<Cell>>>, usize, usize), String> {
        if let Value::Ref(c) = v {
            v = c.borrow().clone();
        }
        match v {
            Value::Seq(v) => {
                let n = v.borrow().len();
                Ok((v, 0, n))
            }
            Value::Slice(v, s, n) => Ok((v, s, n)),
            Value::Text(s) => {
                let v = Rc::new(RefCell::new(
                    s.bytes()
                        .map(|b| cell(Value::U(b as u64)))
                        .collect::<Vec<_>>(),
                ));
                let n = v.borrow().len();
                Ok((v, 0, n))
            }
            _ => Err("not dense storage".into()),
        }
    }
    fn expr(&mut self, e: &Expr, env: &mut BTreeMap<String, Cell>) -> Result<Value, String> {
        self.step()?;
        match &e.op {
            Op::Number(n) => {
                let n = n.replace('_', "");
                let s = e.ty().scalar().unwrap();
                if s.float() {
                    let x = float_literal(&n, s).ok_or("invalid floating literal")?;
                    Ok(if s == Scalar::F32 {
                        Value::F32((x as f32).to_bits())
                    } else {
                        Value::F(x)
                    })
                } else {
                    let n = integer_literal(&n).ok_or("invalid machine literal")? as u64;
                    Ok(if s.signed() {
                        Value::I(n as i64)
                    } else {
                        Value::U(n)
                    })
                }
            }
            Op::Bool(b) => Ok(Value::Bool(*b)),
            Op::Text(s) => Ok(Value::Text(s.clone())),
            Op::Name(n) => {
                if ["Relaxed", "Acquire", "Release", "AcqRel", "SeqCst"].contains(&n.as_str()) {
                    return Ok(Value::U(0));
                }
                Ok(self.copy_value(&env.get(n).ok_or("unknown local")?.borrow(), e.ty()))
            }
            Op::Borrow(a, _) => Ok(Value::Ref(self.place(a, env)?)),
            Op::Move(a) => {
                if !check::affine(a.ty(), &self.mir.unit.records) {
                    return self.expr(a, env);
                }
                let c = self.place(a, env)?;
                let value = std::mem::replace(&mut *c.borrow_mut(), self.zero(a.ty()));
                Ok(value)
            }
            Op::Field(..) | Op::Index(..) | Op::IndexProven(..) => {
                let p = self.place(e, env)?;
                let value = self.copy_value(&p.borrow(), e.ty());
                Ok(value)
            }
            Op::Unary(op, a) => {
                if op == "*" {
                    return Ok(self.place(e, env)?.borrow().clone());
                }
                let v = self.expr(a, env)?;
                match op.as_str() {
                    "!" => Ok(Value::Bool(!v.bool()?)),
                    "-" => {
                        if let Value::F(x) = v {
                            return Ok(Value::F(-x));
                        }
                        if let Value::F32(x) = v {
                            return Ok(Value::F32(x ^ 0x80000000));
                        }
                        let s = e.ty().scalar().unwrap();
                        if let Op::Number(n) = &a.op {
                            if integer_literal(n) == Some(1u128 << (s.bits() - 1)) {
                                return Ok(Value::I(if s.bits() == 64 {
                                    i64::MIN
                                } else {
                                    -(1i64 << (s.bits() - 1))
                                }));
                            }
                        }
                        let Value::I(x) = v else {
                            return Err("invalid negation".into());
                        };
                        checked_int(-(x as i128), s, "checked")
                    }
                    "~" => {
                        let s = e.ty().scalar().unwrap();
                        Ok(from_bits(!bits(&v)?, s))
                    }
                    _ => Err("unknown unary operation".into()),
                }
            }
            Op::Binary(op, a, b) => {
                let av = self.expr(a, env)?;
                if op == "&&" && !av.bool()? {
                    return Ok(Value::Bool(false));
                }
                if op == "||" && av.bool()? {
                    return Ok(Value::Bool(true));
                }
                let bv = self.expr(b, env)?;
                binary(op, av, bv, a.ty().scalar(), "checked")
            }
            Op::Call(n, types, args) => {
                if n == "call_trusted" {
                    let target = self.expr(&args[1], env)?;
                    let Value::Function(name) = target else {
                        return Err("foreign callback address requires a native host".into());
                    };
                    let mut values = Vec::new();
                    for a in &args[2..] {
                        values.push(self.expr(a, env)?);
                    }
                    return self.call(&name, values);
                }
                if n == "fn_ptr" {
                    let Op::Name(n) = &args[0].op else {
                        unreachable!()
                    };
                    return Ok(Value::Function(n.clone()));
                }
                let mut values = Vec::new();
                for a in args {
                    values.push(self.expr(a, env)?);
                }
                self.intrinsic(n, types, values, e.ty(), args)
            }
            Op::Invoke(f, args) => {
                let Value::Function(n) = self.expr(f, env)? else {
                    return Err("invalid function pointer".into());
                };
                let mut values = vec![];
                for a in args {
                    values.push(self.expr(a, env)?);
                }
                self.call(&n, values)
            }
        }
    }
    fn intrinsic(
        &mut self,
        n: &str,
        types: &[Ty],
        v: Vec<Value>,
        ret: &Ty,
        args: &[Expr],
    ) -> Result<Value, String> {
        if self.mir.unit.functions.iter().any(|f| f.name == n) {
            return self.call(n, v);
        }
        if let Some(r) = self.mir.unit.records.iter().find(|r| r.name == n) {
            return Ok(Value::Record(
                r.fields
                    .iter()
                    .zip(v)
                    .map(|((n, _), v)| (n.clone(), cell(v)))
                    .collect(),
            ));
        }
        if n == "cast" || Scalar::parse(n).is_some() {
            return cast(&v[0], ret.scalar().unwrap());
        }
        match n {
            "size_of" | "align_of" => {
                let (size, align) = check::layout_ty(
                    &types[0],
                    &self.mir.unit.records,
                    &mut BTreeMap::new(),
                    &mut BTreeSet::new(),
                )?;
                Ok(Value::U(if n == "size_of" { size } else { align } as u64))
            }
            "abi_version" => {
                let Ty::Record(name) = &types[0] else {
                    unreachable!()
                };
                Ok(Value::U(self.mir.layouts[name].version as u64))
            }
            "resource" => {
                let Ty::Resource(ty, finalizer) = ret else {
                    unreachable!()
                };
                let mir = self
                    .owned_mir
                    .get_or_insert_with(|| Rc::new(self.mir.clone()))
                    .clone();
                Ok(Value::Resource(Rc::new(ResourceObject {
                    value: RefCell::new(Some(cell(v.into_iter().next().unwrap()))),
                    ty: (**ty).clone(),
                    finalizer: finalizer.clone(),
                    mir,
                    errors: self.cleanup_errors.clone(),
                })))
            }
            "resource_get" | "resource_get_mut" => {
                let Value::Ref(p) = &v[0] else { unreachable!() };
                let guard = p.borrow();
                let Value::Resource(r) = &*guard else {
                    unreachable!()
                };
                let payload = r.value.borrow().as_ref().ok_or("closed resource")?.clone();
                Ok(Value::Ref(payload))
            }
            "resource_close" => {
                drop(v);
                Ok(Value::Unit)
            }
            "resource_into_inner" => {
                let Value::Resource(r) = v.into_iter().next().unwrap() else {
                    unreachable!()
                };
                let p = r.value.borrow_mut().take().ok_or("closed resource")?;
                let value = p.replace(Value::Null);
                Ok(value)
            }
            "thread_bind" => Ok(Value::Bound(
                cell(v[0].clone()),
                std::thread::current().id(),
            )),
            "thread_get" | "thread_get_mut" => {
                let Value::Ref(c) = &v[0] else { unreachable!() };
                let Value::Bound(c, owner) = c.borrow().clone() else {
                    unreachable!()
                };
                if owner != std::thread::current().id() {
                    return Err("resource belongs to another thread".into());
                }
                Ok(Value::Ref(c))
            }
            "window_open" | "window_child" | "window_attach" => {
                let title = |i: usize| {
                    if let Value::Text(s) = &v[i] {
                        s.as_str()
                    } else {
                        ""
                    }
                };
                let id = match n {
                    "window_open" => self.windows.borrow_mut().open(
                        title(0),
                        v[1].unsigned()? as u128,
                        v[2].unsigned()? as u128,
                    ),
                    "window_child" => self.windows.borrow_mut().child(
                        v[0].unsigned()? as u128,
                        title(1),
                        v[2].unsigned()? as u128,
                        v[3].unsigned()? as u128,
                    ),
                    _ => self.windows.borrow_mut().attach(
                        v[0].unsigned()? as u128,
                        v[1].unsigned()? as u128,
                        v[2].unsigned()? as u128,
                    ),
                }?;
                Ok(Value::Window(Rc::new(WindowObject {
                    table: self.windows.clone(),
                    id,
                })))
            }
            "window_resize" | "window_focus" | "window_dpi" | "window_poll" | "window_gpu"
            | "window_scene" | "window_input" | "window_handle" | "window_read" => {
                let Value::Ref(c) = &v[0] else { unreachable!() };
                let Value::Window(w) = c.borrow().clone() else {
                    unreachable!()
                };
                let id = w.id;
                match n {
                    "window_resize" => {
                        self.windows.borrow_mut().resize(
                            id,
                            v[1].unsigned()? as u128,
                            v[2].unsigned()? as u128,
                        )?;
                        Ok(Value::Unit)
                    }
                    "window_focus" => {
                        self.windows.borrow_mut().focus(id)?;
                        Ok(Value::Unit)
                    }
                    "window_input" => {
                        self.windows.borrow_mut().input(
                            id,
                            [
                                v[1].unsigned()? as u128,
                                v[2].unsigned()? as u128,
                                v[3].unsigned()? as u128,
                                v[4].unsigned()? as u128,
                            ],
                        )?;
                        Ok(Value::Unit)
                    }
                    "window_handle" => {
                        Ok(Value::U(self.windows.borrow_mut().native_handle(id)? as u64))
                    }
                    "window_read" => {
                        let image = self.windows.borrow_mut().read(id)?;
                        Ok(Value::Seq(Rc::new(RefCell::new(
                            image
                                .into_iter()
                                .map(|n| cell(Value::U(n as u64)))
                                .collect(),
                        ))))
                    }
                    "window_dpi" => Ok(Value::F(self.windows.borrow_mut().dpi(id)?)),
                    "window_poll" => {
                        let e = self
                            .windows
                            .borrow_mut()
                            .poll(id, v[1].unsigned()? as u128)?;
                        Ok(Value::Seq(Rc::new(RefCell::new(
                            e.iter().map(|n| cell(Value::U(*n as u64))).collect(),
                        ))))
                    }
                    "window_gpu" => {
                        self.windows.borrow_mut().gpu(id)?;
                        Ok(Value::Unit)
                    }
                    _ => {
                        let (data, start, len) = self.storage(v[1].clone())?;
                        let scene = data.borrow()[start..start + len]
                            .iter()
                            .map(|c| c.borrow().unsigned().map(u128::from))
                            .collect::<Result<Vec<_>, _>>()?;
                        let (w, h, num, den) = (
                            v[2].unsigned()? as u128,
                            v[3].unsigned()? as u128,
                            v[4].unsigned()? as u128,
                            v[5].unsigned()? as u128,
                        );
                        let fonts = crate::font::FontTable::default();
                        if self.windows.borrow_mut().is_gpu(id)? {
                            let items = crate::vector::gpu_items(&scene, w, h, num, den, &fonts)
                                .ok_or("invalid scene")?;
                            self.windows.borrow_mut().present_gpu(id, items, w, h)?;
                        } else {
                            let img = crate::vector::render(&scene, w, h, num, den)
                                .ok_or("invalid scene")?;
                            self.windows.borrow_mut().present(id, &img)?;
                        }
                        Ok(Value::Unit)
                    }
                }
            }
            "array" => Ok(Value::Seq(Rc::new(RefCell::new(
                v.into_iter().map(cell).collect(),
            )))),
            "push" => {
                let Value::Ref(c) = &v[0] else { unreachable!() };
                let Value::Seq(data) = c.borrow().clone() else {
                    unreachable!()
                };
                data.borrow_mut().push(cell(v[1].clone()));
                Ok(Value::Unit)
            }
            "buffer" | "buffer_aligned" => {
                if n == "buffer_aligned" {
                    let a = v[1].unsigned()?;
                    let Ty::Buffer(t) = ret else { unreachable!() };
                    let alignment = t.scalar().map(|s| s.bits() as u64 / 8).unwrap_or(8);
                    if a == 0 || !a.is_power_of_two() || a < alignment || a > 1048576 {
                        return Err("invalid buffer alignment".into());
                    }
                }
                let len = v[0].unsigned()? as usize;
                if len > 16_777_216 {
                    return Err("interpreter allocation budget exceeded".into());
                }
                let Ty::Buffer(t) = ret else { unreachable!() };
                Ok(Value::Seq(Rc::new(RefCell::new(
                    (0..len).map(|_| cell(self.zero(t))).collect(),
                ))))
            }
            "box" => Ok(Value::Ref(cell(v[0].clone()))),
            "zero" | "union_zero" => Ok(self.zero(ret)),
            "len" => Ok(Value::U(self.storage(v[0].clone())?.2 as u64)),
            "resize" => {
                let Value::Ref(c) = &v[0] else { unreachable!() };
                let Value::Seq(seq) = c.borrow().clone() else {
                    unreachable!()
                };
                let len = v[1].unsigned()? as usize;
                if len > 16_777_216 {
                    return Err("interpreter allocation budget exceeded".into());
                }
                let Ty::Ref(t, _) = args[0].ty() else {
                    unreachable!()
                };
                let Ty::Buffer(t) = &**t else { unreachable!() };
                seq.borrow_mut().resize_with(len, || cell(self.zero(t)));
                Ok(Value::Unit)
            }
            "split_at_mut" => {
                let (data, start, len) = self.storage(v[0].clone())?;
                let mid = v[1].unsigned()? as usize;
                if mid > len {
                    return Err("range out of bounds".into());
                }
                Ok(Value::Record(
                    [
                        ("left".into(), cell(Value::Slice(data.clone(), start, mid))),
                        (
                            "right".into(),
                            cell(Value::Slice(data, start + mid, len - mid)),
                        ),
                    ]
                    .into_iter()
                    .collect(),
                ))
            }
            "slice" | "mut_slice" => {
                let (data, start, len) = self.storage(v[0].clone())?;
                let off = v[1].unsigned()? as usize;
                let n = v[2].unsigned()? as usize;
                if off > len || n > len - off {
                    return Err("range out of bounds".into());
                }
                Ok(Value::Slice(data, start + off, n))
            }
            "borrowed_ptr" | "as_raw" | "data" | "nonnull" | "into_raw" | "from_raw" | "handle"
            | "handle_ptr" => {
                if matches!(v[0], Value::Null) && ["nonnull", "from_raw"].contains(&n) {
                    return Err("null pointer".into());
                }
                Ok(v[0].clone())
            }
            "null" => Ok(Value::Null),
            "address" | "ptr_offset" | "foreign" => {
                Err("foreign addresses require a native host; use dlm build".into())
            }
            "atomic" => Ok(Value::Atomic(cell(v[0].clone()))),
            "atomic_load"
            | "atomic_store"
            | "atomic_exchange"
            | "atomic_compare_exchange"
            | "atomic_is_lock_free" => {
                let Value::Ref(a) = &v[0] else { unreachable!() };
                let Value::Atomic(a) = a.borrow().clone() else {
                    unreachable!()
                };
                let old = a.borrow().clone();
                match n {
                    "atomic_load" => Ok(old),
                    "atomic_store" => {
                        *a.borrow_mut() = v[1].clone();
                        Ok(Value::Unit)
                    }
                    "atomic_exchange" => {
                        *a.borrow_mut() = v[1].clone();
                        Ok(old)
                    }
                    "atomic_compare_exchange" => {
                        let eq = if matches!(old, Value::F(_) | Value::F32(_)) {
                            bits(&old)? == bits(&v[1])?
                        } else {
                            binary("==", old, v[1].clone(), None, "checked")?.bool()?
                        };
                        if eq {
                            *a.borrow_mut() = v[2].clone();
                        }
                        Ok(Value::Bool(eq))
                    }
                    _ => Ok(Value::Bool(true)),
                }
            }
            "simd_splat" => {
                let Ty::Simd(_, n) = ret else { unreachable!() };
                Ok(Value::Seq(Rc::new(RefCell::new(
                    (0..*n).map(|_| cell(v[0].clone())).collect(),
                ))))
            }
            "simd_add" | "simd_mul" | "simd_eq" => {
                let (a, _, _) = self.storage(v[0].clone())?;
                let (b, _, _) = self.storage(v[1].clone())?;
                let Ty::Simd(s, _) = ret else { unreachable!() };
                let mut out = vec![];
                for (x, y) in a.borrow().iter().zip(b.borrow().iter()) {
                    let value = binary(
                        if n == "simd_add" {
                            "+"
                        } else if n == "simd_mul" {
                            "*"
                        } else {
                            "=="
                        },
                        x.borrow().clone(),
                        y.borrow().clone(),
                        Some(*s),
                        "checked",
                    )?;
                    out.push(cell(if let Value::Bool(b) = value {
                        if *s == Scalar::F32 {
                            Value::F32(if b { 1.0f32 } else { 0.0f32 }.to_bits())
                        } else {
                            Value::F(if b { 1.0 } else { 0.0 })
                        }
                    } else {
                        value
                    }));
                }
                Ok(Value::Seq(Rc::new(RefCell::new(out))))
            }
            "simd_load" | "simd_load_aligned" => {
                let (data, start, len) = self.storage(v[0].clone())?;
                let off = v[1].unsigned()? as usize;
                let Ty::Simd(_, n) = ret else { unreachable!() };
                if off > len || *n > len - off {
                    return Err("range out of bounds".into());
                }
                {
                    let copied = data.borrow()[start + off..start + off + n]
                        .iter()
                        .map(|v| cell(v.borrow().clone()))
                        .collect();
                    Ok(Value::Seq(Rc::new(RefCell::new(copied))))
                }
            }
            "simd_store" | "simd_store_aligned" => {
                let (data, start, len) = self.storage(v[0].clone())?;
                let off = v[1].unsigned()? as usize;
                let (src, _, n) = self.storage(v[2].clone())?;
                if off > len || n > len - off {
                    return Err("range out of bounds".into());
                }
                for i in 0..n {
                    *data.borrow()[start + off + i].borrow_mut() = src.borrow()[i].borrow().clone();
                }
                Ok(Value::Unit)
            }
            "read_le" | "read_be" | "write_le" | "write_be" => {
                let (data, start, len) = self.storage(v[0].clone())?;
                let off = v[1].unsigned()? as usize;
                let t = if n.starts_with("write") {
                    &types[0]
                } else {
                    ret
                };
                let s = t.scalar().unwrap();
                let size = s.bits() as usize / 8;
                if off > len || size > len - off {
                    return Err("range out of bounds".into());
                }
                let big = n.ends_with("be");
                if n.starts_with("write") {
                    let b = bits(&v[2])?;
                    for i in 0..size {
                        *data.borrow()[start + off + i].borrow_mut() =
                            Value::U((b >> (8 * if big { size - 1 - i } else { i })) & 255);
                    }
                    Ok(Value::Unit)
                } else {
                    let mut b = 0;
                    for i in 0..size {
                        b |= data.borrow()[start + off + i].borrow().unsigned()?
                            << (8 * if big { size - 1 - i } else { i });
                    }
                    Ok(if s.float() {
                        if s == Scalar::F32 {
                            Value::F32(b as u32)
                        } else {
                            Value::F(f64::from_bits(b))
                        }
                    } else {
                        from_bits(b, s)
                    })
                }
            }
            "bits" => Ok(Value::U(if args[0].ty() == &Ty::Scalar(Scalar::F32) {
                if let Value::F32(b) = v[0] {
                    b as u64
                } else {
                    unreachable!()
                }
            } else {
                v[0].number()?.to_bits()
            })),
            "from_bits" => Ok(if ret == &Ty::Scalar(Scalar::F32) {
                Value::F32(v[0].unsigned()? as u32)
            } else {
                Value::F(f64::from_bits(v[0].unsigned()?))
            }),
            "nan" => Ok(if ret == &Ty::Scalar(Scalar::F32) {
                Value::F32(f32::NAN.to_bits())
            } else {
                Value::F(f64::NAN)
            }),
            "infinity" => Ok(if ret == &Ty::Scalar(Scalar::F32) {
                Value::F32(f32::INFINITY.to_bits())
            } else {
                Value::F(f64::INFINITY)
            }),
            "sqrt" | "sin" | "cos" | "exp" | "log" | "abs" | "floor" | "ceil" | "trunc"
            | "is_nan" | "is_finite" | "sign_bit" => {
                let x = v[0].number()?;
                if n == "is_nan" {
                    return Ok(Value::Bool(x.is_nan()));
                }
                if n == "is_finite" {
                    return Ok(Value::Bool(x.is_finite()));
                }
                if n == "sign_bit" {
                    return Ok(Value::Bool(x.is_sign_negative()));
                }
                let y = match n {
                    "sqrt" => x.sqrt(),
                    "sin" => x.sin(),
                    "cos" => x.cos(),
                    "exp" => x.exp(),
                    "log" => x.ln(),
                    "abs" => x.abs(),
                    "floor" => x.floor(),
                    "ceil" => x.ceil(),
                    _ => x.trunc(),
                };
                Ok(if ret == &Ty::Scalar(Scalar::F32) {
                    Value::F32((y as f32).to_bits())
                } else {
                    Value::F(y)
                })
            }
            "assert" => {
                if !v[0].bool()? {
                    return Err("assertion failed".into());
                }
                Ok(Value::Unit)
            }
            "print" => {
                self.output.push(v[0].text());
                Ok(Value::Unit)
            }
            "utf8_bytes" => {
                let Value::Text(s) = &v[0] else {
                    unreachable!()
                };
                let n = s.len();
                Ok(Value::Slice(
                    Rc::new(RefCell::new(
                        s.bytes().map(|b| cell(Value::U(b as u64))).collect(),
                    )),
                    0,
                    n,
                ))
            }
            n if n.starts_with("wrapping_") || n.starts_with("saturating_") => binary(
                match n.split('_').nth(1).unwrap() {
                    "add" => "+",
                    "sub" => "-",
                    _ => "*",
                },
                v[0].clone(),
                v[1].clone(),
                ret.scalar(),
                if n.starts_with("wrapping_") {
                    "wrapping"
                } else {
                    "saturating"
                },
            ),
            _ => Err(format!("unknown native interpreter operation {n}")),
        }
    }
    fn block(&mut self, b: &[Stmt], env: &mut BTreeMap<String, Cell>) -> Result<Flow, String> {
        let names: BTreeSet<_> = env.keys().cloned().collect();
        let result = (|| {
            for s in b {
                self.step()?;
                match s {
                    Stmt::Let { name, value, .. } => {
                        let v = self.expr(value, env)?;
                        env.insert(name.clone(), cell(v));
                    }
                    Stmt::Set(a, b) => {
                        let v = self.expr(b, env)?;
                        let p = self.place(a, env)?;
                        dispose_cell(&p, a.ty(), self.mir);
                        *p.borrow_mut() = v;
                    }
                    Stmt::Expr(e) => {
                        self.expr(e, env)?;
                    }
                    Stmt::Return(e, _) => {
                        return Ok(Flow::Return(if let Some(e) = e {
                            self.expr(e, env)?
                        } else {
                            Value::Unit
                        }))
                    }
                    Stmt::If(c, a, b) => {
                        let c = self.expr(c, env)?.bool()?;
                        match self.block(if c { a } else { b }, env)? {
                            Flow::Next => {}
                            f => return Ok(f),
                        }
                    }
                    Stmt::While(c, b) => {
                        while self.expr(c, env)?.bool()? {
                            match self.block(b, env)? {
                                Flow::Break => break,
                                Flow::Return(v) => return Ok(Flow::Return(v)),
                                _ => {}
                            }
                        }
                    }
                    Stmt::For(n, a, b, body) => {
                        let a = self.expr(a, env)?.unsigned()?;
                        let b = self.expr(b, env)?.unsigned()?;
                        let c = cell(Value::U(a));
                        env.insert(n.clone(), c.clone());
                        for i in a..b {
                            *c.borrow_mut() = Value::U(i);
                            match self.block(body, env)? {
                                Flow::Break => break,
                                Flow::Return(v) => return Ok(Flow::Return(v)),
                                _ => {}
                            }
                        }
                        env.remove(n);
                    }
                    Stmt::Break(_) => return Ok(Flow::Break),
                    Stmt::Continue(_) => return Ok(Flow::Continue),
                    Stmt::Unsafe(b) | Stmt::Block(b) => match self.block(b, env)? {
                        Flow::Next => {}
                        f => return Ok(f),
                    },
                }
            }
            Ok(Flow::Next)
        })();
        for stmt in b.iter().rev() {
            if let Stmt::Let { name, ty, .. } = stmt {
                if !names.contains(name) {
                    if let Some(p) = env.remove(name) {
                        dispose_cell(&p, ty, self.mir);
                    }
                }
            }
        }
        env.retain(|n, _| names.contains(n));
        if result.is_ok() {
            if let Some(error) = self.cleanup_errors.borrow_mut().pop() {
                return Err(error);
            }
        }
        result
    }
}
fn bits(v: &Value) -> Result<u64, String> {
    match v {
        Value::U(n) => Ok(*n),
        Value::I(n) => Ok(*n as u64),
        Value::F(n) => Ok(n.to_bits()),
        Value::F32(n) => Ok(*n as u64),
        _ => Err("not a machine scalar".into()),
    }
}
fn from_bits(n: u64, s: Scalar) -> Value {
    let bits = s.bits();
    let n = if bits < 64 {
        n & ((1u64 << bits) - 1)
    } else {
        n
    };
    if s.signed() {
        Value::I(if bits < 64 {
            ((n << (64 - bits)) as i64) >> (64 - bits)
        } else {
            n as i64
        })
    } else {
        Value::U(n)
    }
}
fn checked_int(n: i128, s: Scalar, mode: &str) -> Result<Value, String> {
    let bits = s.bits();
    let min = if s.signed() {
        -(1i128 << (bits - 1))
    } else {
        0
    };
    let max = (1i128 << (bits - u32::from(s.signed()))) - 1;
    let n = if mode == "saturating" {
        n.clamp(min, max)
    } else if mode == "wrapping" {
        n
    } else {
        if n < min || n > max {
            return Err("integer overflow".into());
        }
        n
    };
    Ok(from_bits(n as u64, s))
}
fn cast(v: &Value, s: Scalar) -> Result<Value, String> {
    if s.float() {
        if s == Scalar::F32 {
            return Ok(Value::F32(match v {
                Value::F32(b) => *b,
                Value::U(n) => (*n as f32).to_bits(),
                Value::I(n) => (*n as f32).to_bits(),
                Value::F(n) => (*n as f32).to_bits(),
                _ => return Err("numeric conversion requires a scalar".into()),
            }));
        }
        let n = v.number()?;
        return Ok(Value::F(n));
    }
    match v {
        Value::F32(n) => return cast(&Value::F(f32::from_bits(*n) as f64), s),
        Value::F(n) => {
            let bits = s.bits();
            let min = if s.signed() {
                -(2f64).powi((bits - 1) as i32)
            } else {
                0.0
            };
            let lim = (2f64).powi((bits - u32::from(s.signed())) as i32);
            if !n.is_finite() || n.trunc() != *n || *n < min || *n >= lim {
                return Err("numeric conversion out of range".into());
            }
            Ok(if s.signed() {
                Value::I(*n as i64)
            } else {
                Value::U(*n as u64)
            })
        }
        Value::U(n) => checked_int(*n as i128, s, "checked")
            .map_err(|_| "numeric conversion out of range".into()),
        Value::I(n) => checked_int(*n as i128, s, "checked")
            .map_err(|_| "numeric conversion out of range".into()),
        _ => Err("numeric conversion requires a scalar".into()),
    }
}
fn binary(op: &str, a: Value, b: Value, s: Option<Scalar>, mode: &str) -> Result<Value, String> {
    if let (Value::Bool(a), Value::Bool(b)) = (&a, &b) {
        return Ok(Value::Bool(match op {
            "&&" => *a && *b,
            "||" => *a || *b,
            "==" => a == b,
            "!=" => a != b,
            "<" => a < b,
            ">" => a > b,
            "<=" => a <= b,
            ">=" => a >= b,
            _ => return Err("invalid Bool comparison".into()),
        }));
    }
    if matches!(op, "==" | "!=" | "<" | ">" | "<=" | ">=") {
        let cmp = match (&a, &b) {
            (Value::U(a), Value::U(b)) => a.partial_cmp(b),
            (Value::I(a), Value::I(b)) => a.partial_cmp(b),
            (Value::F(a), Value::F(b)) => a.partial_cmp(b),
            (Value::F32(a), Value::F32(b)) => f32::from_bits(*a).partial_cmp(&f32::from_bits(*b)),
            (Value::Null, Value::Null) => Some(std::cmp::Ordering::Equal),
            _ => return Err("incompatible comparison".into()),
        };
        let eq = cmp == Some(std::cmp::Ordering::Equal);
        return Ok(Value::Bool(match op {
            "==" => eq,
            "!=" => !eq,
            "<" => cmp == Some(std::cmp::Ordering::Less),
            ">" => cmp == Some(std::cmp::Ordering::Greater),
            "<=" => cmp.is_some_and(|c| c != std::cmp::Ordering::Greater),
            _ => cmp.is_some_and(|c| c != std::cmp::Ordering::Less),
        }));
    }
    let s = s.ok_or("arithmetic requires a scalar")?;
    if s.float() {
        let (a, b) = (a.number()?, b.number()?);
        let r = if s == Scalar::F32 {
            let (a, b) = (a as f32, b as f32);
            (match op {
                "+" => a + b,
                "-" => a - b,
                "*" => a * b,
                "/" => a / b,
                _ => return Err("invalid float operator".into()),
            }) as f64
        } else {
            match op {
                "+" => a + b,
                "-" => a - b,
                "*" => a * b,
                "/" => a / b,
                _ => return Err("invalid float operator".into()),
            }
        };
        return Ok(if s == Scalar::F32 {
            Value::F32((r as f32).to_bits())
        } else {
            Value::F(r)
        });
    }
    let int = |v: &Value| match v {
        Value::I(n) => *n as i128,
        Value::U(n) => *n as i128,
        _ => unreachable!(),
    };
    let (x, y) = (int(&a), int(&b));
    if matches!(op, "&" | "|" | "^" | "<<" | ">>") {
        let (a, b) = (bits(&a)?, bits(&b)?);
        if (op == "<<" || op == ">>") && (y < 0 || y >= s.bits() as i128) {
            return Err("shift count out of range".into());
        }
        let n = match op {
            "&" => a & b,
            "|" => a | b,
            "^" => a ^ b,
            "<<" => a << b,
            ">>" => {
                if s.signed() {
                    (x as i64 >> b) as u64
                } else {
                    a >> b
                }
            }
            _ => unreachable!(),
        };
        return Ok(from_bits(n, s));
    }
    if (op == "/" || op == "%") && y == 0 {
        return Err("division by zero".into());
    }
    if (op == "/" || op == "%") && s.signed() && x == -(1i128 << (s.bits() - 1)) && y == -1 {
        return Err("integer overflow".into());
    }
    if op == "*" && !s.signed() {
        let wide = (x as u128) * (y as u128);
        let max = (1u128 << s.bits()) - 1;
        return if mode == "checked" && wide > max {
            Err("integer overflow".into())
        } else {
            Ok(from_bits(
                if mode == "saturating" {
                    wide.min(max) as u64
                } else {
                    wide as u64
                },
                s,
            ))
        };
    }
    checked_int(
        match op {
            "+" => x + y,
            "-" => x - y,
            "*" => x * y,
            "/" => x / y,
            "%" => x % y,
            _ => return Err("unknown arithmetic operation".into()),
        },
        s,
        mode,
    )
}

fn place_exists(e: &Expr) -> bool {
    matches!(
        e.op,
        Op::Name(_) | Op::Field(..) | Op::Index(..) | Op::IndexProven(..)
    ) || matches!(&e.op,Op::Unary(n,_)if n=="*")
}

fn float_text(x: f64) -> String {
    if x.is_nan() {
        return "nan".into();
    }
    if x.is_infinite() {
        return if x.is_sign_negative() { "-inf" } else { "inf" }.into();
    }
    if x == 0.0 {
        return if x.is_sign_negative() { "-0" } else { "0" }.into();
    }
    let sign = if x.is_sign_negative() { "-" } else { "" };
    let text = format!("{:.16e}", x.abs());
    let (m, e) = text.split_once('e').unwrap();
    let e = e.parse::<i32>().unwrap();
    let digits = m.replace('.', "");
    let digits = digits.trim_end_matches('0');
    if (-4..17).contains(&e) {
        if e >= 0 {
            let n = e as usize + 1;
            if digits.len() <= n {
                format!("{sign}{digits}{}", "0".repeat(n - digits.len()))
            } else {
                format!("{sign}{}.{}", &digits[..n], &digits[n..])
            }
        } else {
            format!("{sign}0.{}{digits}", "0".repeat((-e - 1) as usize))
        }
    } else {
        format!(
            "{sign}{}{}e{}{:02}",
            &digits[..1],
            if digits.len() > 1 {
                format!(".{}", &digits[1..])
            } else {
                String::new()
            },
            if e >= 0 { "+" } else { "-" },
            e.abs()
        )
    }
}

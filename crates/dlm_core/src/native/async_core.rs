use super::*;

#[derive(Debug, Clone)]
pub(super) struct Slot {
    pub name: String,
    pub ty: Ty,
}
#[derive(Debug, Clone)]
pub(super) enum Node {
    Eval(usize, Expr),
    Set(Expr, Expr),
    Drop(Vec<usize>),
    Branch(Expr, usize, usize),
    Jump(usize),
    Await { input: usize, output: usize },
    Return(Expr),
}
#[derive(Debug, Clone)]
pub(super) struct Graph {
    pub slots: Vec<Slot>,
    pub nodes: Vec<Node>,
    pub parameters: Vec<usize>,
}
struct Loop {
    start: usize,
    breaks: Vec<usize>,
    continues: Vec<usize>,
    depth: usize,
}
struct Builder<'a> {
    unit: &'a Unit,
    records: &'a [Record],
    graph: Graph,
    bindings: BTreeMap<String, usize>,
    scopes: Vec<Vec<usize>>,
    loops: Vec<Loop>,
}
fn named(slot: &Slot, line: usize) -> Expr {
    Expr {
        op: Op::Name(slot.name.clone()),
        ty: Some(slot.ty.clone()),
        line,
    }
}
pub(super) fn suspends(e: &Expr) -> bool {
    match &e.op {
        Op::Await(_) => true,
        Op::Unary(_, a) | Op::Borrow(a, _) | Op::Move(a) | Op::Field(a, _) => suspends(a),
        Op::Binary(_, a, b) | Op::Index(a, b) | Op::IndexProven(a, b) => suspends(a) || suspends(b),
        Op::Call(_, _, a) => a.iter().any(suspends),
        Op::Invoke(f, a) => suspends(f) || a.iter().any(suspends),
        _ => false,
    }
}
impl Builder<'_> {
    fn slot(&mut self, ty: Ty) -> usize {
        let id = self.graph.slots.len();
        self.graph.slots.push(Slot {
            name: format!("slot_{id}"),
            ty,
        });
        self.scopes.last_mut().unwrap().push(id);
        id
    }
    fn emit(&mut self, n: Node) -> usize {
        let i = self.graph.nodes.len();
        self.graph.nodes.push(n);
        i
    }
    fn value(&self, id: usize, line: usize) -> Expr {
        let e = named(&self.graph.slots[id], line);
        if super::check::affine(e.ty(), self.records) {
            Expr {
                ty: e.ty.clone(),
                line,
                op: Op::Move(Box::new(e)),
            }
        } else {
            e
        }
    }
    fn save(&mut self, e: Expr, temps: &mut Vec<usize>) -> usize {
        let id = self.slot(e.ty().clone());
        temps.push(id);
        self.emit(Node::Eval(id, e));
        id
    }
    fn callback_symbol(&self, name: &str, index: usize, arg: &Expr) -> bool {
        if self.unit.shadows_intrinsic(name) {
            return false;
        }
        match name {
            "fn_ptr" | "call_trusted" | "spawn_blocking" => index == 0,
            "spawn" => index == 1 && *arg.ty() == Ty::Unit && matches!(&arg.op, Op::Name(_)),
            "mutex_with" | "rw_read" | "rw_write" => index == 1,
            "parallel_for" | "parallel_map" => index == 4,
            _ => false,
        }
    }
    fn renamed(&self, e: &Expr) -> Expr {
        let mut e = e.clone();
        match &mut e.op {
            Op::Name(n) => {
                if let Some(id) = self.bindings.get(n) {
                    *n = self.graph.slots[*id].name.clone();
                }
            }
            Op::Unary(_, a) | Op::Borrow(a, _) | Op::Move(a) | Op::Await(a) | Op::Field(a, _) => {
                **a = self.renamed(a)
            }
            Op::Binary(_, a, b) | Op::Index(a, b) | Op::IndexProven(a, b) => {
                **a = self.renamed(a);
                **b = self.renamed(b);
            }
            Op::Call(n, _, args) => {
                for (i, a) in args.iter_mut().enumerate() {
                    if !self.callback_symbol(n, i, a) {
                        *a = self.renamed(a);
                    }
                }
            }
            Op::Invoke(f, args) => {
                **f = self.renamed(f);
                for a in args {
                    *a = self.renamed(a);
                }
            }
            _ => {}
        }
        e
    }
    fn place(&mut self, e: &Expr, temps: &mut Vec<usize>) -> Expr {
        let mut e = e.clone();
        e.op = match &e.op {
            Op::Name(_) => return self.renamed(&e),
            Op::Field(a, n) => Op::Field(Box::new(self.place(a, temps)), n.clone()),
            Op::Index(a, b) => Op::Index(
                Box::new(self.place(a, temps)),
                Box::new(self.expr(b, temps)),
            ),
            Op::IndexProven(a, b) => Op::IndexProven(
                Box::new(self.place(a, temps)),
                Box::new(self.expr(b, temps)),
            ),
            Op::Unary(n, a) if n == "*" => Op::Unary(n.clone(), Box::new(self.expr(a, temps))),
            _ => return self.expr(&e, temps),
        };
        e
    }
    fn expr(&mut self, e: &Expr, temps: &mut Vec<usize>) -> Expr {
        if !suspends(e) {
            return self.renamed(e);
        }
        let mut out = e.clone();
        out.op = match &e.op {
            Op::Await(a) => {
                let a = self.expr(a, temps);
                let input = self.save(a, temps);
                let output = self.slot(e.ty().clone());
                temps.push(output);
                self.emit(Node::Await { input, output });
                return self.value(output, e.line);
            }
            Op::Binary(n, a, b) if n == "&&" || n == "||" => {
                let a = self.expr(a, temps);
                let result = self.slot(Ty::Bool);
                temps.push(result);
                let branch = self.emit(Node::Branch(a, 0, 0));
                let yes = self.graph.nodes.len();
                let b = self.expr(b, temps);
                self.emit(Node::Eval(result, b));
                let jump = self.emit(Node::Jump(0));
                let no = self.graph.nodes.len();
                self.emit(Node::Eval(
                    result,
                    Expr {
                        op: Op::Bool(n == "||"),
                        ty: Some(Ty::Bool),
                        line: e.line,
                    },
                ));
                let end = self.graph.nodes.len();
                self.graph.nodes[jump] = Node::Jump(end);
                if let Node::Branch(_, a, b) = &mut self.graph.nodes[branch] {
                    if n == "&&" {
                        *a = yes;
                        *b = no;
                    } else {
                        *a = no;
                        *b = yes;
                    }
                }
                return self.value(result, e.line);
            }
            Op::Binary(n, a, b) => {
                let a = self.expr(a, temps);
                let id = self.save(a, temps);
                let b = self.expr(b, temps);
                Op::Binary(n.clone(), Box::new(self.value(id, e.line)), Box::new(b))
            }
            Op::Call(n, types, args) => {
                let mut values = Vec::new();
                for (i, a) in args.iter().enumerate() {
                    if self.callback_symbol(n, i, a) {
                        values.push(a.clone());
                        continue;
                    }
                    let a = self.expr(a, temps);
                    let id = self.save(a, temps);
                    values.push(self.value(id, e.line));
                }
                Op::Call(n.clone(), types.clone(), values)
            }
            Op::Invoke(f, args) => {
                let f = self.expr(f, temps);
                let fid = self.save(f, temps);
                let mut values = Vec::new();
                for a in args {
                    let a = self.expr(a, temps);
                    let id = self.save(a, temps);
                    values.push(self.value(id, e.line));
                }
                Op::Invoke(Box::new(self.value(fid, e.line)), values)
            }
            Op::Borrow(a, m) => Op::Borrow(Box::new(self.place(a, temps)), *m),
            Op::Move(a) => Op::Move(Box::new(self.place(a, temps))),
            Op::Unary(n, a) => Op::Unary(n.clone(), Box::new(self.expr(a, temps))),
            Op::Field(a, n) => {
                let a = self.expr(a, temps);
                let id = self.save(a, temps);
                Op::Field(Box::new(named(&self.graph.slots[id], e.line)), n.clone())
            }
            Op::Index(a, b) | Op::IndexProven(a, b) => {
                let a = self.place(a, temps);
                let b = self.expr(b, temps);
                Op::Index(Box::new(a), Box::new(b))
            }
            _ => unreachable!("checked suspension expression"),
        };
        out
    }
    fn patch(&mut self, pc: usize, target: usize) {
        self.graph.nodes[pc] = Node::Jump(target);
    }
    fn drop_temps(&mut self, temps: Vec<usize>) {
        if !temps.is_empty() {
            self.emit(Node::Drop(temps));
        }
    }
    fn block(&mut self, body: &[Stmt]) {
        let before = self.bindings.clone();
        self.scopes.push(Vec::new());
        for stmt in body {
            self.stmt(stmt);
        }
        let vars = self.scopes.pop().unwrap();
        self.emit(Node::Drop(vars));
        self.bindings = before;
    }
    fn exit_loop(&mut self, is_break: bool) {
        let depth = self.loops.last().unwrap().depth;
        let scopes = self.scopes[depth..]
            .iter()
            .flatten()
            .copied()
            .collect::<Vec<_>>();
        self.emit(Node::Drop(scopes));
        let pc = self.emit(Node::Jump(0));
        let l = self.loops.last_mut().unwrap();
        if is_break {
            l.breaks.push(pc)
        } else {
            l.continues.push(pc)
        }
    }
    fn stmt(&mut self, s: &Stmt) {
        match s {
            Stmt::Let {
                name, ty, value, ..
            } => {
                let mut temps = Vec::new();
                let value = self.expr(value, &mut temps);
                let id = self.slot(ty.clone());
                self.emit(Node::Eval(id, value));
                self.bindings.insert(name.clone(), id);
                self.drop_temps(temps);
            }
            Stmt::Set(a, b) => {
                let mut temps = Vec::new();
                let b = self.expr(b, &mut temps);
                let id = self.save(b, &mut temps);
                let a = self.place(a, &mut temps);
                self.emit(Node::Set(a, self.value(id, b_line(s))));
                self.drop_temps(temps);
            }
            Stmt::Expr(e) => {
                let mut temps = Vec::new();
                let e = self.expr(e, &mut temps);
                let id = self.save(e, &mut temps);
                let _ = id;
                self.drop_temps(temps);
            }
            Stmt::Return(e, line) => {
                let mut temps = Vec::new();
                let value = e
                    .as_ref()
                    .map(|e| self.expr(e, &mut temps))
                    .unwrap_or(Expr {
                        op: Op::Call("zero".into(), vec![Ty::Unit], vec![]),
                        ty: Some(Ty::Unit),
                        line: *line,
                    });
                self.emit(Node::Return(value));
            }
            Stmt::If(c, a, b) => {
                let mut temps = Vec::new();
                let c = self.expr(c, &mut temps);
                let id = self.save(c, &mut temps);
                let branch = self.emit(Node::Branch(self.value(id, c_line(s)), 0, 0));
                let yes = self.graph.nodes.len();
                self.drop_temps(temps.clone());
                self.block(a);
                let jump = self.emit(Node::Jump(0));
                let no = self.graph.nodes.len();
                self.drop_temps(temps);
                self.block(b);
                let end = self.graph.nodes.len();
                self.patch(jump, end);
                if let Node::Branch(_, a, b) = &mut self.graph.nodes[branch] {
                    *a = yes;
                    *b = no;
                }
            }
            Stmt::While(c, b) => {
                let start = self.graph.nodes.len();
                let mut temps = Vec::new();
                let line = c.line;
                let c = self.expr(c, &mut temps);
                let id = self.save(c, &mut temps);
                let branch = self.emit(Node::Branch(self.value(id, line), 0, 0));
                let body = self.graph.nodes.len();
                self.drop_temps(temps.clone());
                self.loops.push(Loop {
                    start,
                    breaks: Vec::new(),
                    continues: Vec::new(),
                    depth: self.scopes.len(),
                });
                self.block(b);
                self.emit(Node::Jump(start));
                let end = self.graph.nodes.len();
                self.drop_temps(temps);
                if let Node::Branch(_, a, b) = &mut self.graph.nodes[branch] {
                    *a = body;
                    *b = end;
                }
                let l = self.loops.pop().unwrap();
                let after = self.graph.nodes.len();
                for pc in l.breaks {
                    self.patch(pc, after)
                }
                for pc in l.continues {
                    self.patch(pc, l.start)
                }
            }
            Stmt::For(name, a, b, body) => {
                let mut temps = Vec::new();
                let a = self.expr(a, &mut temps);
                let index = self.slot(a.ty().clone());
                self.emit(Node::Eval(index, a));
                let b = self.expr(b, &mut temps);
                let end = self.save(b, &mut temps);
                let old = self.bindings.insert(name.clone(), index);
                let start = self.graph.nodes.len();
                let cond = Expr {
                    op: Op::Binary(
                        "<".into(),
                        Box::new(self.value(index, 0)),
                        Box::new(self.value(end, 0)),
                    ),
                    ty: Some(Ty::Bool),
                    line: 0,
                };
                let branch = self.emit(Node::Branch(cond, 0, 0));
                let body_pc = self.graph.nodes.len();
                self.loops.push(Loop {
                    start,
                    breaks: Vec::new(),
                    continues: Vec::new(),
                    depth: self.scopes.len(),
                });
                self.block(body);
                let increment = self.graph.nodes.len();
                let add = Expr {
                    op: Op::Binary(
                        "+".into(),
                        Box::new(self.value(index, 0)),
                        Box::new(Expr {
                            op: Op::Number("1".into()),
                            ty: Some(self.graph.slots[index].ty.clone()),
                            line: 0,
                        }),
                    ),
                    ty: Some(self.graph.slots[index].ty.clone()),
                    line: 0,
                };
                self.emit(Node::Eval(index, add));
                self.emit(Node::Jump(start));
                let after = self.graph.nodes.len();
                if let Node::Branch(_, a, b) = &mut self.graph.nodes[branch] {
                    *a = body_pc;
                    *b = after;
                }
                let l = self.loops.pop().unwrap();
                for pc in l.breaks {
                    self.patch(pc, after)
                }
                for pc in l.continues {
                    self.patch(pc, increment)
                }
                temps.push(index);
                self.drop_temps(temps);
                if let Some(old) = old {
                    self.bindings.insert(name.clone(), old);
                } else {
                    self.bindings.remove(name);
                }
            }
            Stmt::Break(_) => self.exit_loop(true),
            Stmt::Continue(_) => self.exit_loop(false),
            Stmt::Block(b) | Stmt::Unsafe(b) => self.block(b),
        }
    }
}
fn b_line(s: &Stmt) -> usize {
    if let Stmt::Set(_, b) = s {
        b.line
    } else {
        0
    }
}
fn c_line(s: &Stmt) -> usize {
    if let Stmt::If(c, _, _) = s {
        c.line
    } else {
        0
    }
}
pub(super) fn lower(f: &Function, unit: &Unit) -> Graph {
    let mut b = Builder {
        unit,
        records: &unit.records,
        graph: Graph {
            slots: Vec::new(),
            nodes: Vec::new(),
            parameters: Vec::new(),
        },
        bindings: BTreeMap::new(),
        scopes: vec![Vec::new()],
        loops: Vec::new(),
    };
    for (name, ty) in &f.params {
        let id = b.slot(ty.clone());
        b.graph.parameters.push(id);
        b.bindings.insert(name.clone(), id);
    }
    b.block(f.body.as_ref().unwrap());
    if f.ret == Ty::Unit {
        b.emit(Node::Return(Expr {
            op: Op::Call("zero".into(), vec![Ty::Unit], vec![]),
            ty: Some(Ty::Unit),
            line: f.line,
        }));
    }
    b.graph
}

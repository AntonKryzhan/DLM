use super::{
    async_core::{Graph, Node},
    concurrent::{Cancellation, Endpoint, PollWake, Pool, Wire},
    vm::{dispose_cell, Cell, Value, Vm},
    *,
};
use std::{
    cell::{Cell as DeadlineCell, RefCell},
    rc::Rc,
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

struct Frame {
    mir: Arc<Mir>,
    graph: Graph,
    pc: usize,
    values: BTreeMap<String, Wire>,
    steps: u64,
}
enum State {
    Function(Frame),
    Timer {
        start: Option<Instant>,
        virtual_start: Option<u64>,
        delay: Duration,
        yielded: bool,
        is_yield: bool,
    },
    Send {
        endpoint: Arc<Endpoint>,
        value: Option<Wire>,
    },
    Receive(Arc<Endpoint>),
    Select(Arc<Future>, Arc<Future>),
    Ready(Option<Wire>),
    Failed(String),
    Polling,
}
pub struct Future {
    state: Mutex<State>,
}
impl std::fmt::Debug for Future {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("Future")
    }
}
pub(super) struct PollContext {
    cancel: Arc<Cancellation>,
    pool: Arc<Pool>,
    output: Arc<Mutex<Vec<String>>>,
    limit: u64,
    wake: Arc<PollWake>,
    deadline: DeadlineCell<Option<Instant>>,
}
impl PollContext {
    pub(super) fn new(
        cancel: Arc<Cancellation>,
        pool: Arc<Pool>,
        output: Arc<Mutex<Vec<String>>>,
        limit: u64,
        wake: Arc<PollWake>,
    ) -> Self {
        Self {
            cancel,
            pool,
            output,
            limit,
            wake,
            deadline: DeadlineCell::new(None),
        }
    }
    fn park(&self, deadline: Instant) {
        self.deadline.set(Some(
            self.deadline
                .get()
                .map_or(deadline, |other| deadline.min(other)),
        ));
    }
    pub(super) fn deadline(&self) -> Option<Instant> {
        self.deadline.get()
    }
}
impl Future {
    pub(super) fn function(mir: Mir, f: &Function, args: Vec<Value>) -> Result<Arc<Self>, String> {
        let graph = async_core::lower(f, &mir.unit);
        let mut values = BTreeMap::new();
        for (id, value) in graph.parameters.iter().zip(args) {
            values.insert(graph.slots[*id].name.clone(), Wire::take(value)?);
        }
        Ok(Arc::new(Self {
            state: Mutex::new(State::Function(Frame {
                mir: Arc::new(mir),
                graph,
                pc: 0,
                values,
                steps: 0,
            })),
        }))
    }
    pub(super) fn timer(ms: u64, is_yield: bool) -> Arc<Self> {
        Arc::new(Self {
            state: Mutex::new(State::Timer {
                start: None,
                virtual_start: None,
                delay: Duration::from_millis(ms),
                yielded: false,
                is_yield,
            }),
        })
    }
    pub(super) fn send(endpoint: Arc<Endpoint>, value: Wire) -> Arc<Self> {
        Arc::new(Self {
            state: Mutex::new(State::Send {
                endpoint,
                value: Some(value),
            }),
        })
    }
    pub(super) fn receive(endpoint: Arc<Endpoint>) -> Arc<Self> {
        Arc::new(Self {
            state: Mutex::new(State::Receive(endpoint)),
        })
    }
    pub(super) fn select(left: Arc<Future>, right: Arc<Future>) -> Arc<Self> {
        Arc::new(Self {
            state: Mutex::new(State::Select(left, right)),
        })
    }
    pub(super) fn poll(&self, ctx: &PollContext) -> Result<bool, String> {
        let old = std::mem::replace(&mut *self.state.lock().unwrap(), State::Polling);
        let result = (|| {
            Ok(match old {
                State::Ready(value) => (State::Ready(value), true),
                State::Failed(e) => return Err(e),
                State::Polling => return Err("future is already being polled".into()),
                State::Function(mut frame) => {
                    if let Some(result) = poll_frame(&mut frame, ctx)? {
                        (State::Ready(Some(result)), true)
                    } else {
                        (State::Function(frame), false)
                    }
                }
                State::Timer {
                    mut start,
                    mut virtual_start,
                    delay,
                    mut yielded,
                    is_yield,
                } => {
                    let elapsed = if ctx.pool.deterministic {
                        let now = ctx.pool.ticks.load(std::sync::atomic::Ordering::Relaxed);
                        Duration::from_millis(now.saturating_sub(*virtual_start.get_or_insert(now)))
                    } else {
                        start.get_or_insert_with(Instant::now).elapsed()
                    };
                    if is_yield && !yielded {
                        yielded = true;
                        ctx.park(Instant::now());
                        (
                            State::Timer {
                                start,
                                virtual_start,
                                delay,
                                yielded,
                                is_yield,
                            },
                            false,
                        )
                    } else if elapsed >= delay {
                        (State::Ready(Some(Wire::Unit)), true)
                    } else {
                        if let Some(start) = start {
                            ctx.park(start + delay);
                        }
                        (
                            State::Timer {
                                start,
                                virtual_start,
                                delay,
                                yielded,
                                is_yield,
                            },
                            false,
                        )
                    }
                }
                State::Send {
                    endpoint,
                    mut value,
                } => match endpoint.poll_send(&mut value, &ctx.wake) {
                    Some(sent) => (State::Ready(Some(Wire::Bool(sent))), true),
                    None => (State::Send { endpoint, value }, false),
                },
                State::Receive(endpoint) => match endpoint.poll_receive(&ctx.wake)? {
                    Some(value) => (State::Ready(Some(value)), true),
                    None => (State::Receive(endpoint), false),
                },
                State::Select(left, right) => {
                    if left.poll(ctx)? {
                        let value = left.take()?;
                        drop(right);
                        (State::Ready(Some(value)), true)
                    } else if right.poll(ctx)? {
                        let value = right.take()?;
                        drop(left);
                        (State::Ready(Some(value)), true)
                    } else {
                        (State::Select(left, right), false)
                    }
                }
            })
        })();
        match result {
            Ok((next, ready)) => {
                *self.state.lock().unwrap() = next;
                Ok(ready)
            }
            Err(e) => {
                *self.state.lock().unwrap() = State::Failed(e.clone());
                Err(e)
            }
        }
    }
    pub(super) fn take(&self) -> Result<Wire, String> {
        let mut s = self.state.lock().unwrap();
        match &mut *s {
            State::Ready(value) => value
                .take()
                .ok_or_else(|| "future result already consumed".into()),
            State::Failed(e) => Err(e.clone()),
            _ => Err("future result is not ready".into()),
        }
    }
}
fn clean(frame: &Frame, env: &mut BTreeMap<String, Cell>) {
    for slot in frame.graph.slots.iter().rev() {
        if let Some(value) = env.remove(&slot.name) {
            dispose_cell(&value, &slot.ty, &frame.mir);
        }
    }
}
fn put(frame: &Frame, env: &mut BTreeMap<String, Cell>, id: usize, value: Value) {
    let slot = &frame.graph.slots[id];
    if let Some(old) = env.remove(&slot.name) {
        dispose_cell(&old, &slot.ty, &frame.mir);
    }
    env.insert(slot.name.clone(), Rc::new(RefCell::new(value)));
}
fn poll_frame(frame: &mut Frame, ctx: &PollContext) -> Result<Option<Wire>, String> {
    let mir = frame.mir.clone();
    let mut vm = Vm::new(&mir).with_step_limit(ctx.limit);
    vm.deterministic = ctx.pool.deterministic;
    vm.steps = frame.steps;
    vm.cancellation = Some(ctx.cancel.clone());
    vm.task_pool = Some(ctx.pool.clone());
    vm.task_output = Some(ctx.output.clone());
    let mut env = std::mem::take(&mut frame.values)
        .into_iter()
        .map(|(name, v)| (name, Rc::new(RefCell::new(v.value()))))
        .collect::<BTreeMap<_, _>>();
    let result = (|| loop {
        vm.step()?;
        let node = frame
            .graph
            .nodes
            .get(frame.pc)
            .ok_or("invalid async continuation")?
            .clone();
        match node {
            Node::Eval(id, e) => {
                let value = vm.expr(&e, &mut env)?;
                put(frame, &mut env, id, value);
                frame.pc += 1;
            }
            Node::Set(a, b) => {
                let value = vm.expr(&b, &mut env)?;
                let place = vm.place(&a, &mut env)?;
                dispose_cell(&place, a.ty(), &frame.mir);
                *place.borrow_mut() = value;
                frame.pc += 1;
            }
            Node::Drop(ids) => {
                for id in ids.into_iter().rev() {
                    let slot = &frame.graph.slots[id];
                    if let Some(value) = env.remove(&slot.name) {
                        dispose_cell(&value, &slot.ty, &frame.mir);
                    }
                }
                frame.pc += 1;
            }
            Node::Jump(pc) => frame.pc = pc,
            Node::Branch(e, a, b) => {
                let Value::Bool(test) = vm.expr(&e, &mut env)? else {
                    return Err("invalid async branch".into());
                };
                frame.pc = if test { a } else { b };
            }
            Node::Return(e) => {
                let result = Wire::take(vm.expr(&e, &mut env)?)?;
                return Ok(Some(result));
            }
            Node::Await { input, output } => {
                let value = env
                    .get(&frame.graph.slots[input].name)
                    .ok_or("missing await operand")?
                    .borrow()
                    .clone();
                let value = match value {
                    Value::Future(f) => {
                        if !f.poll(ctx)? {
                            return Ok(None);
                        }
                        f.take()?.value()
                    }
                    Value::Task(t) => {
                        let status = t.poll_ready(&ctx.wake);
                        if status < 2 {
                            return Ok(None);
                        }
                        t.take()?
                    }
                    _ => return Err("invalid await operand".into()),
                };
                let slot = &frame.graph.slots[input];
                if let Some(old) = env.remove(&slot.name) {
                    dispose_cell(&old, &slot.ty, &frame.mir);
                }
                put(frame, &mut env, output, value);
                frame.pc += 1;
            }
        }
    })();
    frame.steps = vm.steps;
    ctx.output.lock().unwrap().extend(vm.output);
    match result {
        Ok(None) => {
            for (name, value) in env {
                frame
                    .values
                    .insert(name, Wire::take(value.replace(Value::Null))?);
            }
            Ok(None)
        }
        Ok(Some(value)) => {
            clean(frame, &mut env);
            Ok(Some(value))
        }
        Err(e) => {
            clean(frame, &mut env);
            Err(e)
        }
    }
}

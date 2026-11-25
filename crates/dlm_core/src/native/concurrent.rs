use super::async_vm::{Future, PollContext};
use super::{
    vm::{Value, Vm},
    Mir, Ty,
};
use std::collections::{BTreeMap, VecDeque};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex, Weak};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

#[derive(Debug)]
pub(super) enum Wire {
    U(u64),
    I(i64),
    F(f64),
    F32(u32),
    Bool(bool),
    Unit,
    Null,
    Seq(Vec<Wire>),
    Record(BTreeMap<String, Wire>),
    Boxed(Box<Wire>),
    Endpoint(Arc<Endpoint>),
    Future(Arc<Future>),
    Task(Arc<Task>),
    Shared(Arc<Shared>),
    Atomic(super::atomic_vm::AtomicValue),
}
impl Wire {
    pub(super) fn take(v: Value) -> Result<Self, String> {
        Ok(match v {
            Value::U(n) => Self::U(n),
            Value::I(n) => Self::I(n),
            Value::F(n) => Self::F(n),
            Value::F32(n) => Self::F32(n),
            Value::Bool(n) => Self::Bool(n),
            Value::Unit => Self::Unit,
            Value::Null => Self::Null,
            Value::Seq(v) => Self::Seq(
                v.borrow()
                    .iter()
                    .map(|v| Self::take(v.borrow().clone()))
                    .collect::<Result<_, _>>()?,
            ),
            Value::Record(v) => Self::Record(
                v.into_iter()
                    .map(|(n, v)| Ok((n, Self::take(v.borrow().clone())?)))
                    .collect::<Result<_, String>>()?,
            ),
            Value::Ref(v) => Self::Boxed(Box::new(Self::take(v.borrow().clone())?)),
            Value::Endpoint(v) => Self::Endpoint(v),
            Value::Future(v) => Self::Future(v),
            Value::Task(v) => Self::Task(v),
            Value::Shared(v) => Self::Shared(v),
            Value::Atomic(v) => Self::Atomic(v),
            _ => return Err("value cannot be transferred between tasks".into()),
        })
    }
    pub(super) fn value(self) -> Value {
        let cell = |v| std::rc::Rc::new(std::cell::RefCell::new(v));
        match self {
            Self::U(v) => Value::U(v),
            Self::I(v) => Value::I(v),
            Self::F(v) => Value::F(v),
            Self::F32(v) => Value::F32(v),
            Self::Bool(v) => Value::Bool(v),
            Self::Unit => Value::Unit,
            Self::Null => Value::Null,
            Self::Seq(v) => Value::Seq(std::rc::Rc::new(std::cell::RefCell::new(
                v.into_iter().map(|v| cell(v.value())).collect(),
            ))),
            Self::Record(v) => {
                Value::Record(v.into_iter().map(|(n, v)| (n, cell(v.value()))).collect())
            }
            Self::Boxed(v) => Value::Ref(cell(v.value())),
            Self::Endpoint(v) => Value::Endpoint(v),
            Self::Future(v) => Value::Future(v),
            Self::Task(v) => Value::Task(v),
            Self::Shared(v) => Value::Shared(v),
            Self::Atomic(v) => Value::Atomic(v),
        }
    }
}
enum Work {
    Native {
        mir: Mir,
        name: String,
        args: Vec<Wire>,
    },
    Future(Arc<Future>),
    Parallel(Arc<Parallel>),
}
struct Job {
    state: Arc<TaskState>,
    work: Work,
    limit: u64,
    polls: u64,
    output: Arc<Mutex<Vec<String>>>,
    ready_at: Option<Instant>,
    wake: Option<Arc<PollWake>>,
}
struct Queue {
    jobs: VecDeque<Job>,
    polling: usize,
    tasks: Vec<Weak<TaskState>>,
    closing: bool,
}
pub(super) struct Pool {
    queue: Mutex<Queue>,
    wake: Condvar,
    capacity: usize,
    workers: usize,
    blocking: Mutex<Option<Arc<Executor>>>,
    pub(super) deterministic: bool,
    pub(super) ticks: AtomicU64,
}
#[derive(Debug)]
pub(super) struct PollWake {
    pool: Weak<Pool>,
    notified: AtomicBool,
}
impl PollWake {
    fn new(pool: &Arc<Pool>) -> Arc<Self> {
        Arc::new(Self {
            pool: Arc::downgrade(pool),
            notified: AtomicBool::new(false),
        })
    }
    fn notify(&self) {
        if let Some(pool) = self.pool.upgrade() {
            let _queue = pool.queue.lock().unwrap();
            self.notified.store(true, Ordering::Release);
            pool.wake.notify_all();
        }
    }
}
pub struct Executor {
    pub(super) pool: Arc<Pool>,
    workers: Mutex<Vec<JoinHandle<()>>>,
}
impl std::fmt::Debug for Executor {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Executor")
            .field("capacity", &self.pool.capacity)
            .finish()
    }
}
struct Progress {
    status: u32,
    result: Option<Result<Wire, String>>,
    observer: Option<Weak<PollWake>>,
}
struct TaskState {
    progress: Mutex<Progress>,
    wake: Condvar,
    cancelled: Arc<Cancellation>,
}
impl TaskState {
    fn new(parent: Option<Arc<Cancellation>>) -> Self {
        Self {
            progress: Mutex::new(Progress {
                status: 0,
                result: None,
                observer: None,
            }),
            wake: Condvar::new(),
            cancelled: Arc::new(Cancellation::new(parent)),
        }
    }
    fn complete(&self, status: u32, result: Option<Result<Wire, String>>) {
        let observer = {
            let mut progress = self.progress.lock().unwrap();
            progress.status = status;
            progress.result = result;
            self.wake.notify_all();
            progress.observer.take()
        };
        if let Some(wake) = observer.and_then(|observer| observer.upgrade()) {
            wake.notify();
        }
    }
}
pub(super) struct Cancellation {
    flag: AtomicBool,
    parent: Option<Arc<Self>>,
}
impl Cancellation {
    fn new(parent: Option<Arc<Self>>) -> Self {
        Self {
            flag: AtomicBool::new(false),
            parent,
        }
    }
    fn request(&self) {
        self.flag.store(true, Ordering::Release);
    }
    fn requested(&self) -> bool {
        let mut current = Some(self);
        while let Some(cancel) = current {
            if cancel.flag.load(Ordering::Acquire) {
                return true;
            }
            current = cancel.parent.as_deref();
        }
        false
    }
}
pub struct Task {
    state: Arc<TaskState>,
    pool: Weak<Pool>,
}
impl std::fmt::Debug for Task {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Task")
            .field("status", &self.status())
            .finish()
    }
}
thread_local! { static CURRENT_POOL:std::cell::RefCell<Option<Weak<Pool>>>=const{std::cell::RefCell::new(None)}; }
impl Task {
    pub(super) fn status(&self) -> u32 {
        self.state.progress.lock().unwrap().status
    }
    pub(super) fn poll_ready(&self, wake: &Arc<PollWake>) -> u32 {
        if self.status() < 2 {
            if let Some(pool) = self.pool.upgrade().filter(|p| p.deterministic) {
                help(&pool);
            }
        }
        let mut progress = self.state.progress.lock().unwrap();
        if progress.status < 2 {
            progress.observer = Some(Arc::downgrade(wake));
        }
        progress.status
    }
    fn cancel(&self) -> bool {
        {
            let progress = self.state.progress.lock().unwrap();
            if progress.status >= 2 {
                return false;
            }
            self.state.cancelled.request();
        }
        if let Some(pool) = self.pool.upgrade() {
            let _queue = pool.queue.lock().unwrap();
            pool.wake.notify_all();
        }
        true
    }
    fn wait(&self, ms: u32) -> u32 {
        let start = Instant::now();
        let pool = self.pool.upgrade();
        let virtual_start = pool.as_ref().map_or(0, |p| p.ticks.load(Ordering::Relaxed));
        let deterministic = pool.as_ref().is_some_and(|p| p.deterministic);
        let mut p = self.state.progress.lock().unwrap();
        while p.status < 2 {
            let can_help = deterministic
                || CURRENT_POOL.with(|current| {
                    current
                        .borrow()
                        .as_ref()
                        .is_some_and(|pool| Weak::ptr_eq(pool, &self.pool))
                });
            let elapsed = if deterministic {
                pool.as_ref()
                    .unwrap()
                    .ticks
                    .load(Ordering::Relaxed)
                    .saturating_sub(virtual_start)
            } else {
                start.elapsed().as_millis().min(u64::MAX as u128) as u64
            };
            if ms != u32::MAX && elapsed >= ms as u64 {
                break;
            }
            if can_help {
                drop(p);
                if let Some(pool) = &pool {
                    if !help(pool) {
                        if deterministic {
                            pool.ticks.fetch_add(1, Ordering::Relaxed);
                        } else {
                            std::thread::sleep(Duration::from_millis(1));
                        }
                    }
                }
                p = self.state.progress.lock().unwrap();
            } else if ms == u32::MAX {
                p = self.state.wake.wait(p).unwrap();
            } else {
                let left = Duration::from_millis(ms as u64).saturating_sub(start.elapsed());
                let (next, timeout) = self.state.wake.wait_timeout(p, left).unwrap();
                p = next;
                if timeout.timed_out() {
                    break;
                }
            }
        }
        p.status
    }
    pub(super) fn take(&self) -> Result<Value, String> {
        let status = self.wait(u32::MAX);
        let mut p = self.state.progress.lock().unwrap();
        if status != 2 {
            return Err(match status {
                3 => "task cancelled",
                5 => "executor queue full or closed",
                _ => "task failed",
            }
            .into());
        }
        p.result
            .take()
            .ok_or_else(|| "task result already consumed".into())
            .and_then(|v| v.map(Wire::value))
    }
}
impl Drop for Task {
    fn drop(&mut self) {
        self.cancel();
        self.wait(u32::MAX);
    }
}
impl Pool {
    fn submit(
        self: &Arc<Self>,
        work: Work,
        limit: u64,
        output: Arc<Mutex<Vec<String>>>,
    ) -> Arc<Task> {
        let state = Arc::new(TaskState::new(None));
        let mut q = self.queue.lock().unwrap();
        if q.closing || q.jobs.len() + q.polling >= self.capacity {
            state.progress.lock().unwrap().status = 5;
        } else {
            q.tasks.retain(|t| t.strong_count() > 0);
            q.tasks.push(Arc::downgrade(&state));
            q.jobs.push_back(Job {
                state: state.clone(),
                work,
                limit,
                polls: 0,
                output,
                ready_at: Some(Instant::now()),
                wake: None,
            });
            self.wake.notify_one();
        }
        Arc::new(Task {
            state,
            pool: Arc::downgrade(self),
        })
    }
    pub(super) fn spawn_blocking(
        self: &Arc<Self>,
        mir: Mir,
        name: String,
        args: Vec<Wire>,
        limit: u64,
        output: Arc<Mutex<Vec<String>>>,
    ) -> Result<Arc<Task>, String> {
        if self.deterministic {
            return Ok(self.submit(Work::Native { mir, name, args }, limit, output));
        }
        let executor = self
            .blocking
            .lock()
            .unwrap()
            .as_ref()
            .cloned()
            .ok_or("blocking task requires an async scope")?;
        Ok(executor.spawn(mir, name, args, limit, output))
    }
    pub(super) fn spawn_future(
        self: &Arc<Self>,
        future: Arc<Future>,
        limit: u64,
        output: Arc<Mutex<Vec<String>>>,
    ) -> Arc<Task> {
        self.submit(Work::Future(future), limit, output)
    }
}
fn ready(q: &mut Queue) -> Option<Job> {
    let now = Instant::now();
    let index = q.jobs.iter().position(|j| {
        j.ready_at.is_some_and(|deadline| deadline <= now)
            || j.wake
                .as_ref()
                .is_some_and(|wake| wake.notified.load(Ordering::Acquire))
            || j.state.cancelled.requested()
    })?;
    let job = q.jobs.remove(index)?;
    if matches!(&job.work, Work::Future(_)) {
        q.polling += 1;
    }
    Some(job)
}
fn help(pool: &Arc<Pool>) -> bool {
    let job = { ready(&mut pool.queue.lock().unwrap()) };
    if let Some(job) = job {
        execute(pool, job);
        true
    } else {
        false
    }
}
fn execute(pool: &Arc<Pool>, job: Job) {
    struct ActivePool(Option<Weak<Pool>>);
    impl Drop for ActivePool {
        fn drop(&mut self) {
            CURRENT_POOL.with(|current| {
                current.replace(self.0.take());
            });
        }
    }
    let _active =
        ActivePool(CURRENT_POOL.with(|current| current.replace(Some(Arc::downgrade(pool)))));
    let Job {
        state,
        work,
        limit,
        polls,
        output,
        ..
    } = job;
    let polling = matches!(&work, Work::Future(_));
    if state.cancelled.requested() {
        drop(work);
        if polling {
            pool.queue.lock().unwrap().polling -= 1;
        }
        state.complete(3, None);
        return;
    }
    if pool.deterministic {
        pool.ticks.fetch_add(1, Ordering::Relaxed);
    }
    state.progress.lock().unwrap().status = 1;
    let group_cancel = match &work {
        Work::Parallel(group) => Some(group.cancel.clone()),
        _ => None,
    };
    let mut suspended = None;
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if polls >= limit {
            return Err("task execution step limit exceeded".into());
        }
        match work {
            Work::Native { mir, name, args } => {
                let mut vm = Vm::new(&mir).with_step_limit(limit);
                vm.deterministic = pool.deterministic;
                vm.cancellation = Some(state.cancelled.clone());
                vm.task_output = Some(output.clone());
                vm.task_pool = Some(pool.clone());
                let result = vm
                    .call(&name, args.into_iter().map(Wire::value).collect())
                    .and_then(Wire::take);
                output.lock().unwrap().extend(vm.output);
                result.map(Some)
            }
            Work::Parallel(group) => {
                let mut vm = Vm::new(&group.mir).with_step_limit(limit);
                vm.deterministic = pool.deterministic;
                vm.cancellation = Some(state.cancelled.clone());
                vm.task_output = Some(output.clone());
                vm.task_pool = Some(pool.clone());
                let result = group.run(&mut vm);
                output.lock().unwrap().extend(vm.output);
                result.map(Some)
            }
            Work::Future(future) => {
                let wake = PollWake::new(pool);
                let context = PollContext::new(
                    state.cancelled.clone(),
                    pool.clone(),
                    output.clone(),
                    limit,
                    wake.clone(),
                );
                if future.poll(&context)? {
                    future.take().map(Some)
                } else {
                    suspended = Some((future, wake, context.deadline()));
                    Ok(None)
                }
            }
        }
    }))
    .unwrap_or_else(|_| Err("task interpreter panicked".into()));
    if result.is_err() {
        if let Some(cancel) = group_cancel {
            cancel.request();
        }
    }
    if let Ok(None) = result {
        state.progress.lock().unwrap().status = 0;
        let (future, wake, deadline) = suspended.unwrap();
        let mut queue = pool.queue.lock().unwrap();
        queue.polling -= 1;
        queue.jobs.push_back(Job {
            state,
            work: Work::Future(future),
            limit,
            polls: polls + 1,
            output,
            ready_at: if pool.deterministic {
                Some(Instant::now())
            } else {
                deadline
            },
            wake: Some(wake),
        });
        pool.wake.notify_all();
    } else {
        if polling {
            pool.queue.lock().unwrap().polling -= 1;
        }
        let result = result.map(|v| v.unwrap());
        state.complete(if result.is_ok() { 2 } else { 4 }, Some(result));
    }
}
impl Executor {
    pub(super) fn open(
        workers: usize,
        capacity: usize,
        deterministic: bool,
    ) -> Result<Arc<Self>, String> {
        Self::open_internal(workers, capacity, true, deterministic)
    }
    fn open_internal(
        workers: usize,
        capacity: usize,
        blocking: bool,
        deterministic: bool,
    ) -> Result<Arc<Self>, String> {
        if !(1..=256).contains(&workers) || !(1..=1048576).contains(&capacity) {
            return Err("executor limits out of range".into());
        }
        let pool = Arc::new(Pool {
            queue: Mutex::new(Queue {
                jobs: VecDeque::new(),
                polling: 0,
                tasks: Vec::new(),
                closing: false,
            }),
            wake: Condvar::new(),
            capacity,
            workers: if deterministic { 1 } else { workers },
            blocking: Mutex::new(None),
            deterministic,
            ticks: AtomicU64::new(0),
        });
        let result = Arc::new(Self {
            pool: pool.clone(),
            workers: Mutex::new(Vec::new()),
        });
        if blocking && !deterministic {
            *pool.blocking.lock().unwrap() =
                Some(Self::open_internal(workers.min(4), capacity, false, false)?);
        }
        for _ in 0..if deterministic { 0 } else { workers } {
            let pool = pool.clone();
            let handle = std::thread::Builder::new()
                .name("dlm-cpu".into())
                .spawn(move || loop {
                    let job = {
                        let mut q = pool.queue.lock().unwrap();
                        loop {
                            if let Some(job) = ready(&mut q) {
                                break job;
                            }
                            if q.closing && q.jobs.is_empty() {
                                return;
                            }
                            q = if let Some(deadline) =
                                q.jobs.iter().filter_map(|job| job.ready_at).min()
                            {
                                pool.wake
                                    .wait_timeout(
                                        q,
                                        deadline.saturating_duration_since(Instant::now()),
                                    )
                                    .unwrap()
                                    .0
                            } else {
                                pool.wake.wait(q).unwrap()
                            };
                        }
                    };
                    execute(&pool, job);
                })
                .map_err(|e| format!("executor worker creation failed: {e}"))?;
            result.workers.lock().unwrap().push(handle);
        }
        Ok(result)
    }
    pub(super) fn spawn(
        &self,
        mir: Mir,
        name: String,
        args: Vec<Wire>,
        limit: u64,
        output: Arc<Mutex<Vec<String>>>,
    ) -> Arc<Task> {
        self.pool
            .submit(Work::Native { mir, name, args }, limit, output)
    }
}
impl Drop for Executor {
    fn drop(&mut self) {
        let states = {
            let mut queue = self.pool.queue.lock().unwrap();
            queue.closing = true;
            let states = queue
                .tasks
                .iter()
                .filter_map(Weak::upgrade)
                .collect::<Vec<_>>();
            for state in &states {
                state.cancelled.request();
            }
            self.pool.wake.notify_all();
            states
        };
        drop(states);
        if self.pool.deterministic {
            while help(&self.pool) {}
        }
        for worker in self.workers.get_mut().unwrap().drain(..) {
            let _ = worker.join();
        }
        self.pool.blocking.lock().unwrap().take();
    }
}
#[derive(Debug)]
struct ChannelState {
    queue: VecDeque<Wire>,
    senders: usize,
    receivers: usize,
    closed: bool,
    watches: Vec<Weak<PollWake>>,
}
#[derive(Debug)]
struct Channel {
    state: Mutex<ChannelState>,
    changed: Condvar,
    capacity: usize,
}
#[derive(Debug)]
pub struct Endpoint {
    channel: Arc<Channel>,
    sender: bool,
}
impl Channel {
    fn notify(&self, state: &mut ChannelState) {
        self.changed.notify_all();
        for watch in state.watches.drain(..).filter_map(|watch| watch.upgrade()) {
            watch.notify();
        }
    }
}
impl ChannelState {
    fn watch(&mut self, wake: &Arc<PollWake>) {
        self.watches.retain(|watch| watch.strong_count() > 0);
        let wake = Arc::downgrade(wake);
        if !self.watches.iter().any(|watch| Weak::ptr_eq(watch, &wake)) {
            self.watches.push(wake);
        }
    }
}
impl Endpoint {
    pub(super) fn poll_send(&self, value: &mut Option<Wire>, wake: &Arc<PollWake>) -> Option<bool> {
        let mut state = self.channel.state.lock().unwrap();
        if state.receivers == 0 || state.closed {
            drop(state);
            drop(value.take());
            return Some(false);
        }
        if state.queue.len() == self.channel.capacity {
            state.watch(wake);
            return None;
        }
        state.queue.push_back(value.take().unwrap());
        self.channel.notify(&mut state);
        Some(true)
    }
    pub(super) fn poll_receive(&self, wake: &Arc<PollWake>) -> Result<Option<Wire>, String> {
        let mut state = self.channel.state.lock().unwrap();
        if let Some(value) = state.queue.pop_front() {
            self.channel.notify(&mut state);
            return Ok(Some(value));
        }
        if state.senders == 0 {
            return Err("channel closed".into());
        }
        state.watch(wake);
        Ok(None)
    }

    fn open(capacity: usize, stride: usize) -> Result<Arc<Self>, String> {
        if !(1..=1048576).contains(&capacity) || stride == 0 || stride > 67108864 / capacity {
            return Err("channel limits out of range".into());
        }
        Ok(Arc::new(Self {
            channel: Arc::new(Channel {
                state: Mutex::new(ChannelState {
                    queue: VecDeque::new(),
                    senders: 1,
                    receivers: 0,
                    closed: false,
                    watches: Vec::new(),
                }),
                changed: Condvar::new(),
                capacity,
            }),
            sender: true,
        }))
    }
    fn duplicate(&self, sender: bool) -> Result<Arc<Self>, String> {
        let mut s = self.channel.state.lock().unwrap();
        if !sender && s.closed {
            return Err("closed channel endpoint".into());
        }
        if sender {
            s.senders += 1;
        } else {
            s.receivers += 1;
        }
        Ok(Arc::new(Self {
            channel: self.channel.clone(),
            sender,
        }))
    }
    fn send(&self, v: Wire, timeout_ms: u32, cancel: Option<&Cancellation>) -> bool {
        let mut s = self.channel.state.lock().unwrap();
        let deadline = (timeout_ms != u32::MAX).then(|| Instant::now() + Duration::from_millis(timeout_ms.into()));
        while timeout_ms != 0
            && s.queue.len() == self.channel.capacity
            && s.receivers > 0
            && !cancelled(cancel)
        {
            let delay = deadline.map_or(Duration::from_millis(10), |limit| {
                limit.saturating_duration_since(Instant::now()).min(Duration::from_millis(10))
            });
            if delay.is_zero() { break; }
            s = self
                .channel
                .changed
                .wait_timeout(s, delay)
                .unwrap()
                .0;
        }
        if s.queue.len() == self.channel.capacity
            || s.receivers == 0
            || s.closed
            || cancelled(cancel)
        {
            drop(s);
            drop(v);
            return false;
        }
        s.queue.push_back(v);
        self.channel.notify(&mut s);
        true
    }
    fn receive(&self, wait: bool, cancel: Option<&Cancellation>) -> Option<Wire> {
        let mut s = self.channel.state.lock().unwrap();
        while wait && s.queue.is_empty() && s.senders > 0 && !cancelled(cancel) {
            s = self
                .channel
                .changed
                .wait_timeout(s, Duration::from_millis(10))
                .unwrap()
                .0;
        }
        if cancelled(cancel) {
            return None;
        }
        let result = s.queue.pop_front();
        if result.is_some() {
            self.channel.notify(&mut s);
        }
        result
    }
}
impl Drop for Endpoint {
    fn drop(&mut self) {
        let discarded = {
            let mut state = self.channel.state.lock().unwrap();
            let discarded = if self.sender {
                state.senders -= 1;
                VecDeque::new()
            } else {
                state.receivers -= 1;
                if state.receivers == 0 {
                    state.closed = true;
                    std::mem::take(&mut state.queue)
                } else {
                    VecDeque::new()
                }
            };
            self.channel.notify(&mut state);
            discarded
        };
        drop(discarded);
    }
}

fn cancelled(flag: Option<&Cancellation>) -> bool {
    flag.is_some_and(Cancellation::requested)
}
fn borrowed(v: &Value) -> Value {
    let Value::Ref(v) = v else { unreachable!() };
    v.borrow().clone()
}
pub(super) fn intrinsic(
    vm: &mut Vm<'_>,
    name: &str,
    v: &[Value],
    ret: &Ty,
) -> Option<Result<Value, String>> {
    if !matches!(
        name,
        "mutex"
            | "rwlock"
            | "mutex_clone"
            | "rwlock_clone"
            | "lock_poisoned"
            | "lock_clear_poison"
            | "sleep"
            | "yield_now"
            | "select"
            | "block_on"
            | "spawn_child"
            | "send_async"
            | "receive_async"
            | "executor"
            | "join"
            | "task_status"
            | "task_cancel"
            | "task_wait"
            | "task_cancelled"
            | "task_sleep"
            | "sender_connected"
            | "sender_ready"
            | "receiver_connected"
            | "channel"
            | "channel_receiver"
            | "sender_clone"
            | "receiver_clone"
            | "send"
            | "send_timeout"
            | "try_send"
            | "receive"
            | "try_receive"
    ) {
        return None;
    }
    if vm.deterministic && matches!(name, "send" | "send_timeout" | "receive" | "task_sleep") {
        return Some(Err(
            "deterministic VM requires async channels and timers for blocking operations".into(),
        ));
    }
    let result = (|| {
        Ok(match name {
            "mutex" | "rwlock" => Value::Shared(Shared::new(Wire::take(v[0].clone())?)),
            "mutex_clone" | "rwlock_clone" => {
                let Value::Shared(shared) = borrowed(&v[0]) else {
                    unreachable!()
                };
                Value::Shared(shared)
            }
            "lock_poisoned" => {
                let Value::Shared(shared) = borrowed(&v[0]) else {
                    unreachable!()
                };
                Value::Bool(shared.poisoned.load(Ordering::Acquire))
            }
            "lock_clear_poison" => {
                let Value::Shared(shared) = borrowed(&v[0]) else {
                    unreachable!()
                };
                shared.poisoned.store(false, Ordering::Release);
                Value::Unit
            }
            "sleep" => Value::Future(Future::timer(v[0].unsigned()?, false)),
            "yield_now" => Value::Future(Future::timer(0, true)),
            "select" => {
                let (Value::Future(a), Value::Future(b)) = (&v[0], &v[1]) else {
                    unreachable!()
                };
                Value::Future(Future::select(a.clone(), b.clone()))
            }
            "block_on" => {
                let Value::Future(f) = &v[0] else {
                    unreachable!()
                };
                return block_on(vm, f.clone());
            }
            "spawn_child" => {
                let Value::Future(f) = &v[0] else {
                    unreachable!()
                };
                let pool = vm
                    .task_pool
                    .as_ref()
                    .ok_or("spawn_child requires an executor scope")?
                    .clone();
                Value::Task(pool.spawn_future(
                    f.clone(),
                    vm.limit,
                    vm.task_output.get_or_insert_with(Default::default).clone(),
                ))
            }
            "send_async" => {
                let Value::Endpoint(e) = &v[0] else {
                    unreachable!()
                };
                Value::Future(Future::send(e.clone(), Wire::take(v[1].clone())?))
            }
            "receive_async" => {
                let Value::Endpoint(e) = &v[0] else {
                    unreachable!()
                };
                Value::Future(Future::receive(e.clone()))
            }
            "executor" => Value::Executor(Executor::open(
                v[0].unsigned()? as usize,
                v[1].unsigned()? as usize,
                vm.deterministic,
            )?),
            "join" => {
                let Value::Task(t) = &v[0] else {
                    return Err("closed task".into());
                };
                let result = t.take();
                vm.flush_tasks();
                return result;
            }
            "task_status" | "task_cancel" | "task_wait" => {
                let Value::Task(t) = borrowed(&v[0]) else {
                    return Err("closed task".into());
                };
                if name == "task_cancel" {
                    Value::Bool(t.cancel())
                } else {
                    Value::U(if name == "task_wait" {
                        t.wait(v[1].unsigned()? as u32)
                    } else {
                        t.status()
                    } as u64)
                }
            }
            "task_cancelled" => Value::Bool(cancelled(vm.cancellation.as_deref())),
            "task_sleep" => {
                let delay = Duration::from_millis(v[0].unsigned()?);
                let start = Instant::now();
                while start.elapsed() < delay && !cancelled(vm.cancellation.as_deref()) {
                    std::thread::sleep(
                        delay
                            .saturating_sub(start.elapsed())
                            .min(Duration::from_millis(10)),
                    );
                }
                Value::Bool(!cancelled(vm.cancellation.as_deref()))
            }
            "sender_ready" | "receiver_connected" => {
                let Value::Endpoint(endpoint) = borrowed(&v[0]) else { unreachable!() };
                let channel = &endpoint.channel;
                let state = channel.state.lock().unwrap();
                Value::Bool(if name == "sender_ready" {
                    !state.closed && state.receivers != 0 && state.queue.len() < channel.capacity
                } else { state.senders != 0 || !state.queue.is_empty() })
            }
            "sender_connected" => {
                let Value::Endpoint(e) = borrowed(&v[0]) else {
                    unreachable!()
                };
                let state = e.channel.state.lock().unwrap();
                Value::Bool(state.receivers > 0 && !state.closed)
            }
            "channel" => {
                let Ty::Sender(t) = ret else { unreachable!() };
                let (size, _) = super::check::layout_ty(
                    t,
                    &vm.mir.unit.records,
                    &mut BTreeMap::new(),
                    &mut Default::default(),
                )?;
                Value::Endpoint(Endpoint::open(v[0].unsigned()? as usize, size)?)
            }
            "channel_receiver" | "sender_clone" | "receiver_clone" => {
                let Value::Endpoint(e) = borrowed(&v[0]) else {
                    unreachable!()
                };
                Value::Endpoint(e.duplicate(name == "sender_clone")?)
            }
            "send" | "send_timeout" | "try_send" => {
                let Value::Endpoint(e) = borrowed(&v[0]) else {
                    unreachable!()
                };
                Value::Bool(e.send(
                    Wire::take(v[1].clone())?,
                    match name {
                        "send" => u32::MAX,
                        "send_timeout" => v[2].unsigned()? as u32,
                        _ => 0,
                    },
                    vm.cancellation.as_deref(),
                ))
            }
            "receive" | "try_receive" => {
                let Value::Endpoint(e) = borrowed(&v[0]) else {
                    unreachable!()
                };
                let result = e.receive(name == "receive", vm.cancellation.as_deref());
                if let Some(value) = result {
                    let Value::Ref(out) = &v[1] else {
                        unreachable!()
                    };
                    *out.borrow_mut() = value.value();
                    Value::Bool(true)
                } else {
                    Value::Bool(false)
                }
            }
            _ => return Err("not a concurrency operation".into()),
        })
    })();
    Some(result)
}

pub(super) fn block_on(vm: &mut Vm<'_>, future: Arc<Future>) -> Result<Value, String> {
    let pool = Executor::open(1, 1024, vm.deterministic)?;
    let task = pool.pool.spawn_future(
        future,
        vm.limit,
        vm.task_output.get_or_insert_with(Default::default).clone(),
    );
    let result = task.take();
    vm.flush_tasks();
    drop(task);
    drop(pool);
    result
}

include!("shared_vm.rs");

include!("parallel_vm.rs");

struct Parallel {
    mir: Arc<Mir>,
    name: String,
    captures: Vec<Wire>,
    types: Vec<Ty>,
    first: usize,
    end: usize,
    grain: usize,
    next: std::sync::atomic::AtomicUsize,
    output: Option<Mutex<Vec<Option<Wire>>>>,
    cancel: Arc<Cancellation>,
}

impl Parallel {
    fn range(&self) -> Option<std::ops::Range<usize>> {
        let mut first = self.next.load(Ordering::Relaxed);
        while first < self.end {
            let end = first + self.grain.min(self.end - first);
            match self
                .next
                .compare_exchange_weak(first, end, Ordering::Relaxed, Ordering::Relaxed)
            {
                Ok(_) => return Some(first..end),
                Err(actual) => first = actual,
            }
        }
        None
    }

    fn run(&self, vm: &mut Vm<'_>) -> Result<Wire, String> {
        let captures = self
            .captures
            .iter()
            .map(|value| value.duplicate().value())
            .collect::<Vec<_>>();
        while !cancelled(vm.cancellation.as_deref()) {
            let Some(range) = self.range() else {
                break;
            };
            let start = range.start - self.first;
            let mut results = Vec::new();
            for index in range {
                if cancelled(vm.cancellation.as_deref()) {
                    break;
                }
                let mut args = Vec::with_capacity(captures.len() + 1);
                args.push(Value::U(index as u64));
                args.extend(
                    captures
                        .iter()
                        .zip(&self.types)
                        .map(|(value, ty)| vm.copy_value(value, ty)),
                );
                let result = vm.call(&self.name, args)?;
                if self.output.is_some() {
                    results.push(Wire::take(result)?);
                }
            }
            if let Some(output) = &self.output {
                let mut output = output.lock().unwrap();
                for (slot, value) in output[start..].iter_mut().zip(results) {
                    *slot = Some(value);
                }
            }
        }
        Ok(Wire::Unit)
    }
}

pub(super) fn parallel(
    vm: &mut Vm<'_>,
    executor: &Executor,
    name: &str,
    bounds: [usize; 3],
    values: Vec<Value>,
    map: bool,
) -> Result<Value, String> {
    let [first, end, grain] = bounds;
    if grain == 0 {
        return Err("parallel grain must be positive".into());
    }
    let len = end.saturating_sub(first);
    if len == 0 {
        return Ok(if map {
            Wire::Seq(Vec::new()).value()
        } else {
            Value::Unit
        });
    }
    if cancelled(vm.cancellation.as_deref()) {
        return Err("parallel operation cancelled".into());
    }
    let output = if map {
        if len as u64 > vm.limit {
            return Err("parallel map exceeds interpreter work budget".into());
        }
        let mut values = Vec::new();
        values
            .try_reserve_exact(len)
            .map_err(|_| "parallel result allocation failed")?;
        values.resize_with(len, || None);
        Some(Mutex::new(values))
    } else {
        None
    };
    let f = vm
        .mir
        .unit
        .functions
        .iter()
        .find(|f| f.name == name)
        .ok_or("unknown parallel callback")?;
    let captures = values
        .into_iter()
        .map(Wire::take)
        .collect::<Result<_, _>>()?;
    let group = Arc::new(Parallel {
        mir: Arc::new(vm.mir.clone()),
        name: name.into(),
        captures,
        types: f.params.iter().skip(1).map(|(_, t)| t.clone()).collect(),
        first,
        end,
        grain,
        next: std::sync::atomic::AtomicUsize::new(first),
        output,
        cancel: Arc::new(Cancellation::new(vm.cancellation.clone())),
    });
    let count = (1 + (len - 1) / grain).min(executor.pool.workers);
    let output = vm.task_output.get_or_insert_with(Default::default).clone();
    let lanes = executor
        .pool
        .submit_range(group.clone(), count, vm.limit, output)?;
    let mut status = 2;
    for lane in lanes {
        status = status.max(lane.wait(u32::MAX));
    }
    vm.flush_tasks();
    match status {
        4 => return Err("parallel callback failed".into()),
        3 => return Err("parallel operation cancelled".into()),
        _ => {}
    }
    if cancelled(vm.cancellation.as_deref()) {
        return Err("parallel operation cancelled".into());
    }
    let Some(output) = &group.output else {
        return Ok(Value::Unit);
    };
    let values = std::mem::take(&mut *output.lock().unwrap())
        .into_iter()
        .collect::<Option<Vec<_>>>()
        .ok_or("incomplete parallel result")?;
    Ok(Wire::Seq(values).value())
}

impl Pool {
    fn submit_range(
        self: &Arc<Self>,
        group: Arc<Parallel>,
        count: usize,
        limit: u64,
        output: Arc<Mutex<Vec<String>>>,
    ) -> Result<Vec<Arc<Task>>, String> {
        let mut queue = self.queue.lock().unwrap();
        let available = self
            .capacity
            .saturating_sub(queue.jobs.len() + queue.polling);
        if queue.closing || available == 0 {
            return Err("executor queue full or closed".into());
        }
        let count = count.min(available);
        let mut lanes = Vec::with_capacity(count);
        queue.tasks.retain(|t| t.strong_count() > 0);
        for _ in 0..count {
            let state = Arc::new(TaskState::new(Some(group.cancel.clone())));
            queue.tasks.push(Arc::downgrade(&state));
            queue.jobs.push_back(Job {
                state: state.clone(),
                work: Work::Parallel(group.clone()),
                limit,
                polls: 0,
                output: output.clone(),
                ready_at: Some(Instant::now()),
                wake: None,
            });
            lanes.push(Arc::new(Task {
                state,
                pool: Arc::downgrade(self),
            }));
        }
        self.wake.notify_all();
        Ok(lanes)
    }
}

pub struct Shared {
    value: std::sync::RwLock<Wire>,
    poisoned: AtomicBool,
}
impl std::fmt::Debug for Shared {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Shared")
            .field("poisoned", &self.poisoned.load(Ordering::Acquire))
            .finish()
    }
}
thread_local! {static HELD_SHARED:std::cell::RefCell<Vec<usize>>=const{std::cell::RefCell::new(Vec::new())};}
struct HeldShared;
impl Drop for HeldShared {
    fn drop(&mut self) {
        HELD_SHARED.with(|held| {
            held.borrow_mut().pop();
        });
    }
}
impl Shared {
    fn new(value: Wire) -> Arc<Self> {
        Arc::new(Self {
            value: std::sync::RwLock::new(value),
            poisoned: AtomicBool::new(false),
        })
    }
    pub(super) fn call(
        &self,
        vm: &mut Vm<'_>,
        name: &str,
        mut args: Vec<Value>,
        write: bool,
    ) -> Result<Value, String> {
        let id = self as *const Self as usize;
        if HELD_SHARED.with(|held| held.borrow().contains(&id)) {
            return Err("recursive shared lock acquisition".into());
        }
        HELD_SHARED.with(|held| held.borrow_mut().push(id));
        let _held = HeldShared;
        if write {
            let mut guard = self.value.write().map_err(|_| "shared lock poisoned")?;
            if self.poisoned.load(Ordering::Acquire) {
                return Err("shared lock poisoned".into());
            }
            let value = std::mem::replace(&mut *guard, Wire::Null).value();
            let cell = std::rc::Rc::new(std::cell::RefCell::new(value));
            args.insert(0, Value::Ref(cell.clone()));
            let result = vm.call(name, args);
            *guard = Wire::take(cell.replace(Value::Null))?;
            if result.is_err() {
                self.poisoned.store(true, Ordering::Release);
            }
            result
        } else {
            let guard = self.value.read().map_err(|_| "shared lock poisoned")?;
            if self.poisoned.load(Ordering::Acquire) {
                return Err("shared lock poisoned".into());
            }
            let value = guard.duplicate().value();
            args.insert(
                0,
                Value::Ref(std::rc::Rc::new(std::cell::RefCell::new(value))),
            );
            vm.call(name, args)
        }
    }
}
impl Wire {
    fn duplicate(&self) -> Self {
        match self {
            Self::U(v) => Self::U(*v),
            Self::I(v) => Self::I(*v),
            Self::F(v) => Self::F(*v),
            Self::F32(v) => Self::F32(*v),
            Self::Bool(v) => Self::Bool(*v),
            Self::Unit => Self::Unit,
            Self::Null => Self::Null,
            Self::Seq(values) => Self::Seq(values.iter().map(Self::duplicate).collect()),
            Self::Record(values) => Self::Record(
                values
                    .iter()
                    .map(|(name, v)| (name.clone(), v.duplicate()))
                    .collect(),
            ),
            Self::Boxed(v) => Self::Boxed(Box::new(v.duplicate())),
            Self::Endpoint(v) => Self::Endpoint(v.clone()),
            Self::Future(v) => Self::Future(v.clone()),
            Self::Task(v) => Self::Task(v.clone()),
            Self::Shared(v) => Self::Shared(v.clone()),
            Self::Atomic(v) => Self::Atomic(v.clone()),
        }
    }
}

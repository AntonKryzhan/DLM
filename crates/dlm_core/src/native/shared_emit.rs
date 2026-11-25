fn shared_helpers(m: &Mir) -> String {
    let mut out = String::new();
    let mut values = BTreeSet::new();
    for ty in all_types(m) {
        if let Ty::Mutex(t) | Ty::RwLock(t) = ty {
            values.insert(*t);
        }
    }
    for t in &values {
        let c = c_type(t);
        let k = key(t);
        let drop = if check::owned(t, &m.unit.records) {
            format!("{}(({c}*)raw);", drop_name(t))
        } else {
            "(void)raw;".into()
        };
        let retain = if check::owned(t, &m.unit.records) {
            format!("{}(({c}*)raw);", retain_name(t))
        } else {
            "(void)raw;".into()
        };
        writeln!(out,"static void dn_shared_drop_{k}(void*raw){{{drop}}}\nstatic void dn_shared_retain_{k}(void*raw){{{retain}}}\nstatic dn_shared_handle*dn_shared_new_{k}({c} value,bool rw){{dn_shared_handle*h=dn_shared_open(sizeof({c}),_Alignof({c}),dn_shared_drop_{k},dn_shared_retain_{k},rw);*({c}*)h->state->value=value;h->state->active=true;dn_shared_retain_{k}(h->state->value);return h;}}").unwrap();
    }
    for f in &m.unit.functions {
        let Some((_, Ty::Ref(t, write))) = f.params.first() else {
            continue;
        };
        if f.async_
            || f.unsafe_
            || f.body.is_none()
            || !f.guarantees.contains("ThreadSafe")
            || !values.contains(&**t)
            || !check::sendable(&f.ret, &m.unit.records)
        {
            continue;
        }
        let c = c_type(t);
        let name = &f.name;
        let ret = c_type(&f.ret);
        let mut params = String::new();
        let mut args = vec![format!("({c}*)state->value")];
        for (i, (_, t)) in f.params.iter().skip(1).enumerate() {
            write!(params, ",{} a{i}", c_type(t)).unwrap();
            args.push(format!("a{i}"));
        }
        let write = if *write { "true" } else { "false" };
        writeln!(out,"static {ret} dn_shared_call_{name}(dn_shared_handle*const*handle{params}){{dnc_shared*state=(*handle)->state;dnc_shared_acquire(state,{write});dnc_held held={{state,dnc_held_locks}};dnc_held_locks=&held;dn_boundary guard={{0}};jmp_buf jump;guard.jump=&jump;guard.previous=dn_current;guard.foreign_marker=dn_foreign_context;dn_current=&guard;if(setjmp(jump)){{dn_current=guard.previous;state->retain(state->value);dnc_held_locks=held.previous;dnc_shared_release(state,{write},true);dn_boundary_end(&guard,1);dn_raise();}}{ret} result={}({});state->retain(state->value);dnc_held_locks=held.previous;dnc_shared_release(state,{write},false);dn_boundary_end(&guard,0);return result;}}",cname(name),args.join(",")).unwrap();
    }
    out
}

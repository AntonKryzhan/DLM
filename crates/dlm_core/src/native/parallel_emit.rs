fn parallel_helpers(m: &Mir, all: &BTreeSet<Ty>) -> String {
    let mut out = String::new();
    for f in &m.unit.functions {
        let buffer = Ty::Buffer(Box::new(f.ret.clone()));
        let map = all.contains(&buffer);
        if !check::parallel_callback(f, &m.unit.records) || (!map && f.ret != Ty::Unit) {
            continue;
        }
        let name = &f.name;
        let result = c_type(&f.ret);
        let mut fields = String::new();
        let mut params = String::new();
        let mut init = String::new();
        let mut arguments = String::from("index");
        for (i, (_, t)) in f.params.iter().enumerate().skip(1) {
            let c = c_type(t);
            write!(fields, " {c} arg{i};").unwrap();
            write!(params, ", {c} arg{i}").unwrap();
            write!(init, ", .arg{i}=arg{i}").unwrap();
            write!(arguments, ", state->arg{i}").unwrap();
        }
        let invoke = format!("{}({arguments})", cname(name));
        let retain = if check::owned(&f.ret, &m.unit.records) {
            format!(" {}(&state->out[pos]);", retain_name(&f.ret))
        } else {
            String::new()
        };
        writeln!(
            out,
            "typedef struct {{ size_t first; {result} *out;{fields} }} dn_parallel_args_{name};"
        )
        .unwrap();
        writeln!(out, "static void dn_parallel_visit_{name}(void *raw, size_t index) {{\n \
            dn_parallel_args_{name} *state=raw;\n \
            {result} value={invoke};\n \
            if (state->out) {{ size_t pos=index-state->first; state->out[pos]=value;{retain} }}\n}}").unwrap();
        let signature =
            format!("dn_executor *const *executor, size_t first, size_t end, size_t grain{params}");
        let setup = "dn_pool *pool=dn_executor_pool(*executor); if (!grain) dn_fail(\"parallel grain must be positive\");";
        let run = format!("dn_parallel(pool,first,end,grain,dn_parallel_visit_{name},&state)");
        if f.ret == Ty::Unit {
            writeln!(
                out,
                "static uint8_t dn_parallel_for_{name}({signature}) {{\n \
                {setup}\n \
                dn_parallel_args_{name} state={{ .first=first{init} }};\n \
                dn_parallel_check({run}); return 0;\n}}"
            )
            .unwrap();
        }
        if map {
            let c = c_type(&buffer);
            writeln!(
                out,
                "static {c} dn_parallel_map_{name}({signature}) {{\n \
                {setup}\n \
                {c} output=dn_new_{}(end>first ? end-first : 0);\n \
                dn_parallel_args_{name} state={{ .first=first, .out=output.data{init} }};\n \
                unsigned status={run};\n \
                if (status!=2) {{ {}(&output); dn_parallel_check(status); }}\n \
                return output;\n}}",
                key(&buffer),
                drop_name(&buffer)
            )
            .unwrap();
        }
    }
    out
}

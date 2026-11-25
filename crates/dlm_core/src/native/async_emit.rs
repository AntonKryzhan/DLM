fn async_functions(m: &Mir) -> String {
    use super::async_core::Node;
    let mut out = String::new();
    let all = all_types(m);
    for t in &all {
        if let Ty::Future(t) = t {
            let c = c_type(t);
            let k = key(t);
            writeln!(out,"static {c} dn_block_on_{k}(dn_future*f){{dn_executor*pool=dn_executor_open(1,1024);dn_task*t=dn_spawn_future(pool,f);{c} value=dn_join_{k}(t);dn_drop_executor(&pool);return value;}}").unwrap();
        }
    }
    let mut messages = BTreeSet::new();
    for t in &all {
        if let Ty::Sender(t) | Ty::Receiver(t) = t {
            messages.insert((**t).clone());
        }
    }
    for t in messages {
        let c = c_type(&t);
        let k = key(&t);
        let se = Ty::Sender(Box::new(t.clone()));
        let re = Ty::Receiver(Box::new(t.clone()));
        let drop_value = if check::owned(&t, &m.unit.records) {
            format!("{}(&p->value);", drop_name(&t))
        } else {
            String::new()
        };
        let retain_value = if check::owned(&t, &m.unit.records) {
            format!("{}(&p->value);", retain_name(&t))
        } else {
            String::new()
        };
        writeln!(out,"typedef struct{{dn_endpoint*endpoint;dnc_channel_watch watch;{c} value;bool result;}}dn_send_frame_{k};\nstatic bool dn_send_poll_{k}(void*raw){{dn_send_frame_{k}*p=raw;unsigned status=dn_channel_poll_send(p->endpoint,&p->value,&p->watch);if(!status)return false;p->result=status==1;return true;}}\nstatic void dn_send_drop_{k}(void*raw){{dn_send_frame_{k}*p=raw;dnc_channel_watch_drop(p->endpoint,&p->watch);{drop_value}{}(&p->endpoint);}}\nstatic dn_future*dn_send_future_{k}(dn_endpoint*endpoint,{c} value){{dn_future*f=dn_future_open(sizeof(dn_send_frame_{k}),_Alignof(dn_send_frame_{k}),dn_send_poll_{k},dn_send_drop_{k});dn_send_frame_{k}*p=f->data;p->endpoint=endpoint;p->value=value;dn_detach(endpoint);{retain_value}f->result=&p->result;return f;}}",drop_name(&se)).unwrap();
        writeln!(out,"typedef struct{{dn_endpoint*endpoint;dnc_channel_watch watch;{c} value;}}dn_receive_frame_{k};\nstatic bool dn_receive_poll_{k}(void*raw){{dn_receive_frame_{k}*p=raw;unsigned status=dn_channel_poll_receive(p->endpoint,&p->value,&p->watch);if(status==2)dn_fail(\"channel closed\");return status==1;}}\nstatic void dn_receive_drop_{k}(void*raw){{dn_receive_frame_{k}*p=raw;dnc_channel_watch_drop(p->endpoint,&p->watch);{drop_value}{}(&p->endpoint);}}\nstatic dn_future*dn_receive_future_{k}(dn_endpoint*endpoint){{dn_future*f=dn_future_open(sizeof(dn_receive_frame_{k}),_Alignof(dn_receive_frame_{k}),dn_receive_poll_{k},dn_receive_drop_{k});dn_receive_frame_{k}*p=f->data;p->endpoint=endpoint;dn_detach(endpoint);f->result=&p->value;return f;}}",drop_name(&re)).unwrap();
    }
    for f in &m.unit.functions {
        if !f.async_ {
            continue;
        }
        let g = super::async_core::lower(f, &m.unit);
        let n = &f.name;
        let result = c_type(&f.ret);
        let mut fields = String::new();
        let mut drops = String::new();
        let mut retains = String::new();
        for (i, slot) in g.slots.iter().enumerate() {
            write!(fields, "{} {};", c_type(&slot.ty), slot.name).unwrap();
            if check::owned(&slot.ty, &m.unit.records) {
                drops = format!(
                    "if(frame->active[{i}]){{{}(&frame->{});frame->active[{i}]=false;}}{drops}",
                    drop_name(&slot.ty),
                    slot.name
                );
                write!(
                    retains,
                    "if(frame->active[{i}]){}(&frame->{});",
                    retain_name(&slot.ty),
                    slot.name
                )
                .unwrap();
            } else {
                write!(drops, "frame->active[{i}]=false;").unwrap();
            }
        }
        let result_drop = if check::owned(&f.ret, &m.unit.records) {
            format!(
                "if(frame->result_active){{{}(&frame->result);frame->result_active=false;}}",
                drop_name(&f.ret)
            )
        } else {
            String::new()
        };
        let result_retain = if check::owned(&f.ret, &m.unit.records) {
            format!("{}(&frame->result);", retain_name(&f.ret))
        } else {
            String::new()
        };
        writeln!(out,"typedef struct dn_async_frame_{n}{{size_t pc;bool active[{}],result_active;{fields}{result} result;}}dn_async_frame_{n};\nstatic void dn_async_drop_{n}(void*raw){{dn_async_frame_{n}*frame=raw;{result_drop}{drops}}}",g.slots.len().max(1)).unwrap();
        writeln!(out,"static bool dn_async_poll_{n}(void*raw){{dn_async_frame_{n}*frame=raw;dn_boundary guard={{0}};jmp_buf jump;guard.jump=&jump;guard.previous=dn_current;guard.foreign_marker=dn_foreign_context;dn_current=&guard;if(setjmp(jump)){{dn_current=guard.previous;dn_async_drop_{n}(frame);dn_boundary_end(&guard,1);dn_raise();}}for(;;){{switch(frame->pc){{").unwrap();
        let mut e = Emitter {
            mir: m,
            temp: 0,
            locals: Vec::new(),
            guards: Vec::new(),
            owned_params: BTreeSet::new(),
            loops: Vec::new(),
            frame: true,
        };
        for (pc, node) in g.nodes.iter().enumerate() {
            writeln!(out, "case {pc}:{{").unwrap();
            match node {
                Node::Eval(id, value) => {
                    let slot = &g.slots[*id];
                    let c = c_type(&slot.ty);
                    let value = e.expr(value);
                    let drop = if check::owned(&slot.ty, &m.unit.records) {
                        format!(
                            "if(frame->active[{id}]){}(&frame->{});",
                            drop_name(&slot.ty),
                            slot.name
                        )
                    } else {
                        String::new()
                    };
                    let retain = if check::owned(&slot.ty, &m.unit.records) {
                        format!("{}(&frame->{});", retain_name(&slot.ty), slot.name)
                    } else {
                        String::new()
                    };
                    writeln!(out,"{c} next={value};{drop}frame->{}=next;frame->active[{id}]=true;{retain}frame->pc={};continue;",slot.name,pc+1).unwrap();
                }
                Node::Set(a, b) => {
                    out.push_str(&e.stmt(&Stmt::Set(a.clone(), b.clone())));
                    writeln!(out, "frame->pc={};continue;", pc + 1).unwrap();
                }
                Node::Drop(ids) => {
                    for id in ids.iter().rev() {
                        let slot = &g.slots[*id];
                        if check::owned(&slot.ty, &m.unit.records) {
                            writeln!(
                                out,
                                "if(frame->active[{id}]){{{}(&frame->{});}}",
                                drop_name(&slot.ty),
                                slot.name
                            )
                            .unwrap();
                        }
                        writeln!(out, "frame->active[{id}]=false;").unwrap();
                    }
                    writeln!(out, "frame->pc={};continue;", pc + 1).unwrap();
                }
                Node::Jump(target) => {
                    writeln!(out, "frame->pc={target};continue;").unwrap();
                }
                Node::Branch(cond, a, b) => {
                    writeln!(out, "frame->pc=({})?{a}:{b};continue;", e.expr(cond)).unwrap();
                }
                Node::Await { input, output } => {
                    let src = &g.slots[*input];
                    let dst = &g.slots[*output];
                    let c = c_type(&dst.ty);
                    let wait=match &src.ty{Ty::Future(_)=>format!("if(!dn_future_poll(frame->{}))goto pending;",src.name),Ty::Task(_)=>format!("unsigned status=dn_task_poll_ready(frame->{});if(status<2)goto pending;if(status!=2)dn_fail(status==3?\"task cancelled\":status==5?\"executor queue full or closed\":\"task failed\");",src.name),_=>unreachable!()};
                    let drop_dst = if check::owned(&dst.ty, &m.unit.records) {
                        format!(
                            "if(frame->active[{output}]){}(&frame->{});",
                            drop_name(&dst.ty),
                            dst.name
                        )
                    } else {
                        String::new()
                    };
                    writeln!(out,"{wait}{drop_dst}frame->{}=*({c}*)frame->{}->result;memset(frame->{}->result,0,sizeof({c}));frame->active[{output}]=true;{}(&frame->{});frame->active[{input}]=false;frame->pc={};continue;",dst.name,src.name,src.name,drop_name(&src.ty),src.name,pc+1).unwrap();
                }
                Node::Return(value) => {
                    writeln!(out,"frame->result={};frame->result_active=true;{result_retain}{drops}dn_boundary_end(&guard,0);return true;",e.expr(value)).unwrap();
                }
            }
            out.push_str("}\n");
        }
        writeln!(out,"default:dn_fail(\"invalid async continuation\");}}}}pending:{retains}dn_boundary_end(&guard,0);return false;}}\nstatic {}{{dn_future*future=dn_future_open(sizeof(dn_async_frame_{n}),_Alignof(dn_async_frame_{n}),dn_async_poll_{n},dn_async_drop_{n});dn_async_frame_{n}*frame=future->data;future->result=&frame->result;",signature(f,&cname(n))).unwrap();
        for ((name, ty), id) in f.params.iter().zip(&g.parameters) {
            let slot = &g.slots[*id];
            writeln!(
                out,
                "frame->{}={};frame->active[{id}]=true;",
                slot.name,
                local(name)
            )
            .unwrap();
            if check::owned(ty, &m.unit.records) {
                writeln!(out, "{}(&frame->{});", retain_name(ty), slot.name).unwrap();
            }
        }
        out.push_str("return future;}\n");
    }
    out
}

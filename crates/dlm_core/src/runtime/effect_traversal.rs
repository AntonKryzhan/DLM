//! Ordered finite traversals with explicit callback contracts.
use super::*;

impl Runtime {
    pub(super) fn eval_effect_traversal(
        &mut self,
        name: &str,
        args: &[Expr],
        theory: &str,
        line: usize,
    ) -> Result<RuntimeValue, Diagnostic> {
        if name == "map_effect" || name == "fold_effect" {
            return self.eval_ordinary_traversal(
                if name == "map_effect" { "map" } else { "fold" },
                args,
                theory,
                line,
            );
        }
        arity(name, args, 2, line)?;
        let input = self.eval_expr(&args[0], theory)?;
        let (sequence, items) = expect_collection(name, input, line)?;
        let action = self.function_value(&args[1], theory, name, line)?;
        let mut selected = Vec::new();
        let mut count = 0u128;
        for item in items {
            let result = self.apply_callable(&action, vec![item.clone()], theory, line)?;
            count += 1;
            match name {
                "each_effect" => {}
                "try_map_effect" | "try_each_effect" => match result {
                    RuntimeValue::ResultValue {
                        is_ok: false,
                        value,
                    } => {
                        return Ok(RuntimeValue::ResultValue {
                            is_ok: false,
                            value,
                        })
                    }
                    RuntimeValue::ResultValue { is_ok: true, value } => {
                        if name == "try_map_effect" {
                            selected.push(*value);
                        }
                    }
                    _ => {
                        return Err(runtime_error(
                            line,
                            format!("{name} callback must return Result<T,E>"),
                        ))
                    }
                },
                _ => {
                    let RuntimeValue::Bool(yes) = result else {
                        return Err(runtime_error(
                            line,
                            format!("{name} predicate must return Bool"),
                        ));
                    };
                    match name {
                        "filter_effect" if yes => selected.push(item),
                        "find_effect" if yes => {
                            return Ok(RuntimeValue::OptionValue(Some(Box::new(item))))
                        }
                        "any_effect" if yes => return Ok(RuntimeValue::Bool(true)),
                        "all_effect" if !yes => return Ok(RuntimeValue::Bool(false)),
                        _ => {}
                    }
                }
            }
        }
        Ok(match name {
            "each_effect" => RuntimeValue::NatExact(count),
            "try_each_effect" => RuntimeValue::ResultValue {
                is_ok: true,
                value: Box::new(RuntimeValue::NatExact(count)),
            },
            "try_map_effect" => RuntimeValue::ResultValue {
                is_ok: true,
                value: Box::new(rebuild(sequence, selected)),
            },
            "filter_effect" => rebuild(sequence, selected),
            "find_effect" => RuntimeValue::OptionValue(None),
            "any_effect" => RuntimeValue::Bool(false),
            "all_effect" => RuntimeValue::Bool(true),
            _ => return Err(runtime_error(line, format!("unknown traversal {name}"))),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn closures_copy_only_free_value_locals_and_skip_builtin_and_type_metadata() {
        let mut runtime = Runtime::new();
        runtime.locals.push(BTreeMap::from([
            ("needed".to_string(), RuntimeValue::NatExact(7)),
            ("unused".to_string(), RuntimeValue::Bytes(vec![1, 2, 3])),
            ("print_line".to_string(), RuntimeValue::Bytes(vec![4])),
            ("Stdout".to_string(), RuntimeValue::Bytes(vec![5])),
        ]));
        for text in [
            "let ignored = print_line(\"x\") in needed + n",
            "effect_fn(FnEffect(Stdout, Nat, Nat), fun(m: Nat, needed + m))",
        ] {
            let body = crate::parser::parse_expr(text, 1).unwrap();
            let closure = runtime.make_closure(
                &[FnParam {
                    name: "n".to_string(),
                    ty: "Nat".to_string(),
                }],
                &body,
            );
            let RuntimeValue::Closure { env, .. } = closure else {
                panic!("not a closure")
            };
            assert_eq!(
                env.keys().map(String::as_str).collect::<Vec<_>>(),
                ["needed"]
            );
        }
    }
}

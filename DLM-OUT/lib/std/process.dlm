// Child processes and the environment (v0.126, docs/OS.md).
module std.process

theory StdProcess {
    record ProcResult(code: Nat, out: Text, err: Text)

    private fn pr_of(xs: List<Text>) -> ProcResult =
        ProcResult(unwrap_or(text_to_nat(unwrap_or(index(xs, 0), "1")), 1), unwrap_or(index(xs, 1), ""), unwrap_or(index(xs, 2), ""))
    // runs `program` with `args`, feeding `input` to its stdin; waits for it to exit
    fn run_with_input(program: Text, args: List<Text>, input: Text) -> Result<ProcResult, Text> =
        match proc_run(program, args, input) { ok(xs) => ok(pr_of(xs), Text), err(e) => err(e, ProcResult) }
    fn run(program: Text, args: List<Text>) -> Result<ProcResult, Text> = run_with_input(program, args, "")
    // POSIX shell command line (`sh -c`)
    fn shell(command: Text) -> Result<ProcResult, Text> = run("sh", list("-c", command))
    fn succeeded(r: Result<ProcResult, Text>) -> Bool = match r { ok(p) => nat_eq(field(p, code), 0), err(e) => false }
    // standard output of a successful run, the error text otherwise
    fn output_of(r: Result<ProcResult, Text>) -> Result<Text, Text> =
        match r { err(e) => err(e, Text), ok(p) => if_then_else(nat_eq(field(p, code), 0), ok(field(p, out), Text), err(concat(concat("exit ", nat_to_text(field(p, code))), concat(": ", field(p, err))), Text)) }
    fn env_or(name: Text, default: Text) -> Text = unwrap_or(proc_env(name), default)
    // background children: start several, then collect them (they run concurrently)
    fn spawn(program: Text, args: List<Text>) -> Result<Nat, Text> = proc_spawn(program, args, "")
    fn wait_for(id: Nat) -> Result<ProcResult, Text> = match proc_wait(id) { ok(xs) => ok(pr_of(xs), Text), err(e) => err(e, ProcResult) }
    fn is_running(id: Nat) -> Bool = match proc_poll(id) { ok(o) => is_none(o), err(e) => false }
    private fn pr_spawn_all(cmds: List<Text>, ids: List<Nat>) -> Result<List<Nat>, Text> loop =
        match cmds { [] => ok(ids, Text), cons(c, rest) => match proc_spawn("sh", list("-c", c), "") { err(e) => err(e, List(Nat)), ok(id) => pr_spawn_all(rest, cons(id, ids)) } }
    private fn pr_wait_all(ids: List<Nat>, done: List<ProcResult>) -> Result<List<ProcResult>, Text> loop =
        match ids { [] => ok(done, Text), cons(i, rest) => match wait_for(i) { err(e) => err(e, List(ProcResult)), ok(r) => pr_wait_all(rest, cons(r, done)) } }
    // runs every shell command concurrently; results in command order
    fn run_parallel(cmds: List<Text>) -> Result<List<ProcResult>, Text> =
        match pr_spawn_all(cmds, empty_list(Nat)) { err(e) => err(e, List(ProcResult)), ok(ids) => pr_wait_all(ids, empty_list(ProcResult)) }

    theorem failed_result_is_not_success: not(succeeded(err("x", ProcResult)))
}

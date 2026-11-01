// Files and directories (v0.125, docs/FILES.md): whole-file text I/O, atomic replacement,
// paths, recursive copy and search, stdin as lines. Loops are `loop` functions (constant stack).
module std.files

theory StdFiles {
    fn read_text(path: Text) -> Result<Text, Text> =
        let f = file_open(path, "r") in let t = file_read_all(f) in match file_close(f) { ok(n) => ok(t, Text), err(e) => err(e, Text) }
    fn read_lines(path: Text) -> Result<List<Text>, Text> = match read_text(path) { ok(t) => ok(lines(t), Text), err(e) => err(e, List(Text)) }
    // the new contents replace the old ones in one rename: readers see either version
    fn write_text(path: Text, t: Text) -> Result<Nat, Text> = file_write_atomic(path, t)
    fn write_lines(path: Text, xs: List<Text>) -> Result<Nat, Text> = file_write_atomic(path, fold(xs, "", fun(acc: Text, x: Text, concat(concat(acc, x), "\n"))))
    fn append_line(path: Text, t: Text) -> Result<Nat, Text> =
        let f = file_open(path, "a") in let w = file_write(f, concat(t, "\n")) in file_close(f)
    fn join_path(a: Text, b: Text) -> Text = if_then_else(nat_eq(text_length(b), 0), a, if_then_else(nat_eq(text_length(a), 0), b, concat(concat(a, "/"), b)))
    private fn fs_last_slash(t: Text, i: Nat, found: Nat) -> Nat decreasing i =
        if_then_else(nat_eq(i, 0), found, if_then_else(text_eq(substring(t, pred(i), 1), "/"), i, fs_last_slash(t, pred(i), found)))
    // "a/b/c.txt" -> "a/b"; "c.txt" -> ""
    fn parent_dir(path: Text) -> Text = let k = fs_last_slash(path, text_length(path), 0) in if_then_else(nat_eq(k, 0), "", substring(path, 0, pred(k)))
    fn base_name(path: Text) -> Text = let k = fs_last_slash(path, text_length(path), 0) in substring(path, k, monus(text_length(path), k))
    fn has_suffix(t: Text, s: Text) -> Bool =
        and(not(text_length(s) > text_length(t)), text_eq(substring(t, monus(text_length(t), text_length(s)), text_length(s)), s))
    private fn fs_rev(xs: List<Text>) -> List<Text> = fold(xs, empty_list(Text), fun(acc: List<Text>, x: Text, cons(x, acc)))
    // files below `root` (relative paths) whose name ends with `suffix`
    fn find_files(root: Text, suffix: Text) -> Result<List<Text>, Text> =
        match file_walk(root) { err(e) => err(e, List(Text)), ok(fs) => ok(fs_rev(fold(fs, empty_list(Text), fun(acc: List<Text>, f: Text, if_then_else(has_suffix(f, suffix), cons(f, acc), acc)))), Text) }
    private fn fs_copy_each(src: Text, dst: Text, files: List<Text>, n: Nat) -> Result<Nat, Text> loop =
        match files { [] => ok(n, Text), cons(f, rest) =>
            match file_make_dir(join_path(dst, parent_dir(f))) { err(e) => err(e, Nat), ok(z) =>
                match file_copy(join_path(src, f), join_path(dst, f)) { err(e) => err(e, Nat), ok(b) => fs_copy_each(src, dst, rest, n + b) } } }
    // copies every regular file below `src` into `dst` (directories are created); total bytes
    fn copy_tree(src: Text, dst: Text) -> Result<Nat, Text> =
        match file_walk(src) { err(e) => err(e, Nat), ok(fs) => match file_make_dir(dst) { err(e) => err(e, Nat), ok(z) => fs_copy_each(src, dst, fs, 0) } }
    private fn fs_size_each(root: Text, files: List<Text>, n: Nat) -> Result<Nat, Text> loop =
        match files { [] => ok(n, Text), cons(f, rest) => match file_size_of(join_path(root, f)) { err(e) => err(e, Nat), ok(b) => fs_size_each(root, rest, n + b) } }
    fn tree_size(root: Text) -> Result<Nat, Text> = match file_walk(root) { err(e) => err(e, Nat), ok(fs) => fs_size_each(root, fs, 0) }
    private fn fs_stdin_rest(acc: List<Text>) -> List<Text> loop = match read_line() { none => fs_rev(acc), some(l) => fs_stdin_rest(cons(l, acc)) }
    // every remaining stdin line, read one at a time
    fn stdin_lines() -> List<Text> = fs_stdin_rest(empty_list(Text))

    // The reader is always closed on normal completion and Result failure.
    private fn fs_each_line<F: UnaryCallable>(f: &File, action: F, count: Nat) -> Nat loop =
        match file_read_line(f) { none => count, some(line) => let done = action(line) in fs_each_line(f, action, count + 1) }
    fn each_line<F: UnaryCallable>(path: Text, action: F) -> Result<Nat, Text> =
        let f = file_open(path, "r") in let count = fs_each_line(f, action, 0) in
        match file_close(f) { err(e) => err(e, Nat), ok(n) => ok(count, Text) }
    private fn fs_try_each_line<F: UnaryCallable>(f: &File, action: F, count: Nat) -> Result<Nat, Text> loop =
        match file_read_line(f) { none => ok(count, Text), some(line) =>
            match action(line) { err(e) => err(e, Nat), ok(value) => fs_try_each_line(f, action, count + 1) } }
    fn try_each_line<F: UnaryCallable>(path: Text, action: F) -> Result<Nat, Text> =
        let f = file_open(path, "r") in let result = fs_try_each_line(f, action, 0) in let closed = file_close(f) in
        match result { err(e) => err(e, Nat), ok(count) => match closed { err(e) => err(e, Nat), ok(n) => ok(count, Text) } }
    // Close the input before invoking user code. Replace the destination atomically.
    fn transform_text<F: UnaryCallable>(src: Text, dst: Text, transform: F) -> Result<Nat, Text> =
        match read_text(src) { err(e) => err(e, Nat), ok(text) => let changed = transform(text) in file_write_atomic(dst, changed) }
    fn try_transform_text<F: UnaryCallable>(src: Text, dst: Text, transform: F) -> Result<Nat, Text> =
        match read_text(src) { err(e) => err(e, Nat), ok(text) =>
            match transform(text) { err(e) => err(e, Nat), ok(changed) => file_write_atomic(dst, changed) } }

    theorem parent_of_nested_path: text_eq(parent_dir("a/b/c.txt"), "a/b")
    theorem join_with_empty_is_identity: text_eq(join_path("a", ""), "a")
}

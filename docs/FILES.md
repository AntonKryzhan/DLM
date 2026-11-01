# External file handles (stage D), 0.109.0

`File` is an owned, **linear** resource: on every path a handle is closed with `file_close`, returned, or moved
into a `File` parameter; it is never released implicitly (E0943: `File 'f' is not closed`, `consumed on one branch
only`). Other rules are those of `ByteBuf` (OWNERSHIP.md): `&File` borrows, no storage in data or function values.

| Operation | Positions | Result |
|---|---|---|
| `file_open(path, mode)` | — | `File`; mode `r`, `w` (truncate), `a` (append); binary |
| `file_ok(&f)`, `file_error(&f)` | borrow | `Bool`; first error text or `""` |
| `file_write(&f, t)`, `file_write_bytes(&f, &b)` | borrow | `Bool`; every write is flushed |
| `file_read_line(&f)` | borrow | `Option<Text>` without `\n`; `none` at the end |
| `file_read_all(&f)`, `file_read_bytes(&f, n)` | borrow | `Text` (strict UTF-8), `ByteBuf` (≤ n bytes) |
| `file_flush(&f)` | borrow | `Bool` |
| `file_close(f)` | move | `ok(bytes written)` or `err(first error / close error)` |
| `file_remove(path)` | — | `ok(0)` or `err(...)` |

Failures never stop the program: the first one is kept in the handle (sticky), later operations do nothing and
return `false`/`none`/`""`/empty, and `file_close` reports it. Messages (identical in interpreter and C):
`cannot open file`, `invalid file mode`, `file … is not open for reading|writing`, `cannot read|write|flush|close
file`, `file … is not valid UTF-8`, `byte buffer length exceeds 268435456`, `cannot remove file`. Effect `io@file`.
Native C: `DV_FILE` header pointing outside the regions; handles left open by a runtime error are closed at exit
(`DLM_FILE_TRACE=1`). Tests: `tests/files.rs` (parity of 15 operations, exit cleanup, 10 negative and 4 accepted
ownership cases).

## v0.125: positioning, buffering, paths and directories

| Operation | Positions | Result |
|---|---|---|
| `file_seek(&f, pos)`, `file_tell(&f)` | borrow | `Bool`; byte offset (`a` handles start at the end) |
| `file_size(&f)`, `file_truncate(&f, n)` | borrow | `Nat`; `Bool` (writable handles) |
| `file_set_buffered(&f, on)` | borrow | `Bool`; buffered writes reach the file on `file_flush`, `seek`/`tell`/`size`, `close` |
| `file_exists(p)`, `file_is_dir(p)` | — | `Bool` |
| `file_size_of(p)`, `file_mtime(p)` | — | `Result<Nat,Text>` (bytes, seconds since 1970) |
| `file_list_dir(p)` | — | `Result<List<Text>,Text>`: entry names, sorted bytewise, UTF-8 names only |
| `file_walk(p)` | — | regular files below `p` as relative `a/b` paths, sorted, symlinks not followed |
| `file_make_dir(p)` | — | creates missing parents; `ok(0)` if it already is a directory |
| `file_remove_dir(p)`, `file_rename(a, b)`, `file_copy(a, b)` | — | `ok(0)` / `ok(0)` (replaces `b`) / `ok(bytes)` |
| `file_write_atomic(p, t)` | — | writes `p.dlm-tmp` and renames it over `p`: `ok(bytes)` |

Path errors: `cannot <stat|list directory|create directory|remove directory|rename|copy|replace> `p` (os error N)` — `N`
is `errno`, identical in interpreter and C on POSIX. `std.files`: `read_text`, `read_lines`, `write_text`,
`write_lines`, `append_line`, `join_path`, `parent_dir`, `base_name`, `has_suffix`, `find_files`, `copy_tree`,
`tree_size`, `stdin_lines`. Tests: `tests/apps.rs`.

# Syscall Discipline

Every `access()` / `stat()` / `mkdir()` syscall is measurable overhead. In a
container runtime, the cumulative cost of redundant filesystem probes directly
affects startup latency. Strace-based analysis is the standard detection tool.

Common mistakes:
- Calling `stat()` on a path that may not exist without a prior `access(F_OK)`.
  `stat()` is heavier than `access()`; if the file doesn't exist, the lighter
  `access()` would have sufficed to discover that.
- Calling `access()` or `stat()` on a file just created by `open()` or
  `mkdir()` — if you just created it, you already know it exists.

Rules (violations are considered bugs):
1. Two or more `access()` calls to the **same file** is a bug. Same for two or
   more `stat()` calls to the same file. Cache the first result or restructure
   the code to probe once. (`access()` followed by `stat()` is not a violation
   — see Rule 2.)
2. `stat()` returning `ENOENT` is a bug. `stat()` is a metadata syscall —
   heavier than `access(F_OK)`. It should only be called on paths already
   known to exist. Use `access(F_OK)` (via `io_exists`) to check existence;
   if the file exists and metadata is needed, call `stat()` afterwards.
3. `access()` or `stat()` to a file/directory that the caller itself just
   created is a bug. If you just wrote it, you already know it exists.
4. `mkdir()` over existing directories (handling EEXIST) is a bug. Always
   `access(F_OK)` first, then `mkdir` only if the directory does not exist.
5. **Only one `unshare()` call is permitted**, with the exception of cgroup
   unshare when cgroupfs is not already mounted on the host. Multiple unshare
   calls create redundant kernel work and risk namespace mismatch bugs.
6. **Only one `getresuid()` and one `getresgid()` call** — capture all three
   UID/GID values at once at the program entry point. Do not follow them with
   `getuid()`, `getgid()`, `geteuid()`, or `getegid()`. The captured values
   must be passed via function arguments — static caches are prohibited
   (see [§Lifecycle Rules](lifecycle.md#lifecycle-rules)).
7. **Lazy passwd/group reads via getters** — `parse_user` never opens
   `"${root}/etc/passwd"` or `"${root}/etc/group"`. Each field is resolved on-demand
   through getter functions (`get_uid`, `get_name`, `get_shell`, etc.).
   The first getter that requires passwd data triggers a single forward scan
   that caches the matched user entry, the root fallback, and the nobody fallback
   in one pass. The group file is read on first `get_groups` call if any group
   token was a name. Subsequent getters return cached values without reopening.
   **Exception:** When `--initscript` is given, the initscript may modify
   `"${root}/etc/passwd"` and/or `"${root}/etc/group"` (e.g. `useradd`). In this
   case two reads are permitted: one before the initscript runs (to resolve
   the target user/group for the init process) and one after (to pick up
   changes made by the initscript).
8. **No `chmod()` on directories we just created with `mkdir()`.** The mode
   was set at creation time; a redundant `chmod()` adds an unnecessary
   syscall and may temporarily expose the wrong permissions.

When modifying syscall code, check:
- Every `access()` call: is there a preceding `access()` or `stat()` on the
  same path that could be reused?
- Every `stat()` call: would `ENOENT` be a normal outcome? If so, replace the
  `stat()` with `access(F_OK)` — existence should be checked with `access`,
  `stat` should only see paths that exist.
- Every `io_exists()` call: is the caller about to `stat`, `mkdir`, or
  `open` the same path anyway?
- Every `mkdir()` call: is the directory expected to exist already? If so,
  `access(F_OK)` first.
- Every `O_CREAT | O_EXCL` open: is there a preceding existence check that
  can be eliminated (O_EXCL itself is atomic)?

## io_mkdir vs raw mkdir

`io_mkdir` is a convenience wrapper that creates missing parent directories
recursively. It should be used **only** when the caller needs "create this
directory and all missing ancestors" semantics. Calling `io_mkdir` when the
parent is known to exist wastes a `stat()` syscall on the target dir and a
recursive `io_mkdir` traversal on the parent.

Rules:
- Use `io_mkdir` when the parent's existence is uncertain (e.g. early setup,
  generic path construction).
- Use raw `mkdir` when the caller has already verified the parent exists (e.g.
  just created it, or the dir is inside an already-established hierarchy like
  `/run/cage/`, `/var/lib/cage/`, or a tmpdir).
- Never use `io_mkdir` on a path whose parent was just created by the same
  function — that `stat` on the parent inside `mkdir_parent` is redundant.

```c
// WRONG — parent just created, io_mkdir stats it again
io_mkdir("/a/b");
io_mkdir("/a/b/c");  // mkdir_parent stats /a/b which we just created

// RIGHT — raw mkdir when parent is known to exist
io_mkdir("/a/b");
if (mkdir("/a/b/c", 0755) != 0) ...
```

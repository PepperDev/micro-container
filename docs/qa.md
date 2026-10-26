# Quality Assurance

Quality Gates (cppcheck, gcov, lizard thresholds) are defined in [CONTRIBUTING.md §Quality Gate](../CONTRIBUTING.md#quality-gate).

## Bug Prevention

When a bug is fixed, evaluate its recurrence likelihood:

| Likelihood | Action |
|---|---|
| Low | No action |
| Medium | Add a prevention entry to §Domain-Specific Pitfalls with bug description, root cause, and prevention plan |
| High | Add a prevention entry to §Domain-Specific Pitfalls AND add a matching test case covering the bug |

Also consult the Memory Safety Checklist and Domain-Specific Pitfalls below as part of the done criteria for every new task — review applicable items before committing.

## Memory Safety Checklist

When modifying any dynamic memory allocation (malloc/calloc/realloc/free, slab pools, or any code that manipulates allocated memory), must audit for ALL of the following hazards:

**Note**: Several hazards below (data race, torn read/write, visibility, deadlock) also apply to **statically allocated fixed-size memory** that is shared among multiple threads. Any shared memory — whether malloc'd or global — must be checked for concurrent access, atomicity of multi-word writes, memory ordering, and lock ordering hazards.

| Hazard | What to look for |
|--------|-----------------|
| **Use-after-free** | Pointer read/written after free; double-free; missing NULL after free |
| **Data race** | Shared allocation accessed by both main thread and worker without ownership handoff or mutex |
| **Out-of-bounds** | Array index from user-controlled data; fd-indexed array with unbounded fd values |
| **Dangling pointer** | realloc/malloc result assigned to global without failure check freeing old data first; pointer to realloc'd array element after growth |
| **Memory corruption** | memcpy without bounds check; missing NULL check after malloc before write |
| **Write-after-read** | Swap-last compaction reading source after potential realloc invalidation |
| **Torn read/write** | Multi-word struct written non-atomically and read from another thread |
| **Visibility** | Missing memory barrier around flag set by one thread and read by another |
| **ABA** | Lock-free freelist reuse with pointer comparison as identity |
| **Deadlock** | Multiple mutexes acquired in different order; mutex held across blocking call |
| **Init-ordering dependency** | Registration/add function called before object fields are fully initialized. Three variants: **(a)** function validates a precondition (e.g. `used` flag) that isn't set yet and silently returns; caller has no way to detect the registration was skipped; failure manifests as a later lookup miss. **(b)** guard condition involves only zero-initialized fields (e.g. `(used+tombs)*2 > cap` on globals: `0 > 0` → **false**) so a required init step (grow, allocate) is skipped, leading to NULL-pointer deref. Generic defense for any new hash table: always add `!ht \|\|` to the grow trigger — `if (!ht \|\| (used+tombs)*2 > cap) grow();` **(c) zero-init ambiguity**: sentinel value is 0 (same as calloc/memset zero state), so a guard like `if (idx >= 0)` thinks a zero-initialized field is legitimately set, causing incorrect side effects (e.g. `pend_free_buf` frees slab index 0 that was never allocated). Defense: use only negative sentinels for "not set" (-1, -2) instead of 0. |
| **Stale buffer data** | Static/reused scratch buffer not cleared between calls; leftover bytes from previous operation contaminate checksums, padding bytes, or length-dependent computations (e.g. odd-length UDP checksum reads padding byte from stale data) |
| **Realloc ordering** | Multiple realloc calls in one success-check — if earlier succeeds and later fails, earlier allocation's old block is already freed, leaving dangling global |

## Domain-Specific Pitfalls

This section consolidates two kinds of bug prevention knowledge: a pre-merge safety checklist of generic C pitfalls (consult before every commit), generic bug patterns with code examples, and project-specific entries (populate as domain knowledge grows).

### Pre-Merge Safety Checklist

Before merging any change, verify these generic C items:

- **Integer overflow**: all arithmetic on untrusted input checked for overflow (buffer sizes, timeout calculations, token arithmetic, etc.)
- **Buffer bounds**: every `memcpy`/`snprintf`/`strncpy` uses the destination buffer size; no `strcpy`, `sprintf`, `gets`
- **Endianness**: network byte-order fields converted with `htonl`/`htons`/`ntohl`/`ntohs` as appropriate; avoid double-conversion (e.g. `s_addr` from `recvfrom` is already NBO)
- **File descriptor leaks**: every `open`/`socket`/`accept` has a paired `close` on all error paths
- **Double-free / NULL-after-free**: on error paths with multiple cleanup sites (early returns, shared cleanup, caller), NULL pointers after `free` and ensure exactly one owner frees each heap pointer
- **Signal safety**: only async-signal-safe functions (see `signal-safety(7)`) called from signal handlers — typically just `write()` to a self-pipe
- **EINTR**: all blocking syscalls (`poll`, `recvfrom`, `sendto`, `accept`) wrapped in a loop that retries on `EINTR`
- **Decode safety**: reject invalid characters in Base32/Base64/Hex input; validate output buffer length before writing
- **Revectored init**: guard conditions on zero-initialized globals may evaluate `0 > 0` as false, skipping required setup — always add `!ptr \|\|` to grow triggers
- **Hash table probe budget**: every open-addressing find/remove/add loop has a bounded probe budget to prevent livelock
- **Dynamic array stale pointers**: pointer to realloc'd array element stored across a potential realloc — store index instead
- **Grow-forgot-cap**: after malloc/realloc for a hash table or dynamic array, set `cap = new_cap` immediately, before any mask/budget computation

### Generic bug patterns with code examples

#### Event-loop fairness

Every event handler that drains an fd in a `for(;;)` loop must yield back to the main event loop after a bounded number of iterations. Without a budget, a flood of events on one fd can starve all other event sources (timers, other fds, signal handlers).

```c
// WRONG — unbounded drain
static void handle_fd(void)
{
    for (;;) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) return;
        process(buf, n);
    }
}

// RIGHT — bounded iteration budget
static void handle_fd(void)
{
    int budget = 64;
    for (;;) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) return;
        process(buf, n);
        if (--budget == 0) return;   // yield to main loop
    }
}
```

Check every `for(;;)` / `while(1)` that reads from a non-blocking fd; budget 32–128.

#### Resource teardown on error paths

In manual-cleanup code, every early `return` after a successful allocation is a potential leak. Free in reverse allocation order and NULL-out after free.

```c
    conn = alloc_conn();
    if (!conn) return -1;
    job = alloc_job();
    if (!job) { free_conn(conn); return -1; }
    if (register(conn, job) < 0) {
        free_job(job);
        free_conn(conn);
        return -1;
    }
```

Check every error path clears all resources allocated so far, in reverse order.

#### Init-ordering gotchas

Zero-initialized hash tables and dynamic arrays are vulnerable to guard conditions that silently skip init:

```c
// WRONG — (used+tombs)*2 > cap → 0 > 0 → false, grow never called
if ((used+tombs)*2 > cap) grow(ht);

// RIGHT — always guard NULL first
if (!ht || (used+tombs)*2 > cap) grow(ht);
```

Also avoid sentinel value 0 when 0 is also a valid index — use -1 or -2 for "not set".

#### Guard condition inversion

When multiple boolean flags drive a conditional action, inverting the logic
(`!a || !b || !c` instead of `a || b || c`) causes the action to be skipped
when it's needed most — all flags are true (all items require action).

```c
// WRONG — enter when ANY is present (false), skip when ALL are missing (true)
int a = missing("A"), b = missing("B"), c = missing("C");
if (!a || !b || !c)        // false when a=b=c=1 → skip
  do_action(a, b, c);

// RIGHT — enter when ANY is missing
if (a || b || c)
  do_action(a, b, c);
```

Check every guard condition that uses `!flag` in a compound OR: is the
intended behavior "enter if any flag is false/missing" or "enter only
if all flags are true/present"? Inverted logic at this point means the
action body is skipped exactly when every flag requires it.

This is a special case of De Morgan's law misapplication, where
`!(A && B && C)` is rewritten as `!A || !B || !C` but the programmer
unintentionally writes `!A || !B || !C` when `A || B || C` was meant.
The fix is to eliminate the double negative and write the positive
condition directly.

### io_exists polarity: 0 means "exists", 1 means "missing"

`io_exists()` returns `0` when the path exists, `1` when it is missing, and
`-1` on error (`src/util/io.c`). Guard code that treats the result as a boolean
(`if (io_exists(p))` or `if (io_exists(p) != 1)`) is inverted: it acts on
missing paths and skips existing ones.

```c
// WRONG — 0 means exists, so this early-returns for an existing dir
if (io_exists(cgdir) != 1)
  return;

// RIGHT — act only when the dir exists
if (io_exists(cgdir) != 0)
  return;
```

Review every `io_exists`/`io_isdir` call site for the three-valued contract;
`!= 1` is almost always a bug, and `-1` must be handled separately. Prefer
naming the comparison (e.g. `int e = io_exists(p); if (e == 1) return;`).

### io_exists cache: invalidate after every successful create

`io_exists()` caches `access()` results per process. A create helper that skips
invalidation leaves the path cached as "missing", so the next `io_mkdir_own()`
on a child path sees its newly created parent as missing and calls `mkdir()`
again, failing with `EEXIST`.

Common mistakes:
- Raw `mkdir()` success followed by `chown()` without invalidating the path
  (`mkdir_chown` in `src/util/io.c` did this)
- `io_mkdtmp()` and `io_symlink()` not invalidating after creating
- Relying on the *caller* to invalidate: callers that create several nested
  dirs then reuse them will still see stale entries

When adding a create/mutate helper, check:
- Does every successful create (dir, file, symlink) call
  `io_cache_invalidate(path)` before returning?
- Does failure after a successful `mkdir()` (e.g. `chown` failing) also
  invalidate the now-existing path?

Correct pattern:
```c
if (mkdir(path, mode) != 0)
  return -1;
io_cache_invalidate(path);   // creation invalidates the cached "missing"
if (owner_must_change && sys_chown(path, uid, gid) != 0)
  return -1;
```

### Cgroup cleanup: parent must stay in the host cgroup namespace

With `-C`, only the container child unshares `CLONE_NEWCGROUP`, folded into its
existing `unshare(CLONE_NEWNS)` call (one unshare, before mounting cgroupfs).
If the **parent** unshares it, the cgroup namespace root becomes `cage-<pid>`;
the parent can no longer see `${path}/cage-<pid>`, and cleanup early-returns,
leaking the cgroup and its `cage-parent` child on every run.

Common mistakes:
- Adding `CLONE_NEWCGROUP` to the parent's `unshare()` in `ns_setup()`
- Writing the parent PID to `cage-<pid>/cgroup.procs` (a no-op) instead of the
  parent cgroup's `${path}/cgroup.procs`, which is what moves the parent out
  before `cgroup_rmdir_recursive()`
- Unsharing the cgroup namespace after mounting cgroupfs in the child

When touching cgroup namespaces or cleanup, check:
- Is `CLONE_NEWCGROUP` folded into the child's `CLONE_NEWNS` unshare, before
  `mount cgroups` (no extra `unshare()` call)?
- Does cleanup write `pid` to `${root}/cgroup.procs` (root = `/sys/fs/cgroup`),
  not to the target sub-cgroup?
- Does the unit test assert the PID was written to the parent `cgroup.procs`
  and that a nested `cage-parent` directory is removed?

### Loop device: hold the setup fd across mount with LO_FLAGS_AUTOCLEAR

A loop device configured with `LO_FLAGS_AUTOCLEAR` is detached as soon as its
last user releases it. The ext4 loopback setup therefore must keep the loop
device fd open until `mount()` has established the filesystem — the mount itself
then holds a reference, so AUTOCLEAR detaches only on unmount.

```c
// WRONG — closing the setup fd detaches the device before mount()
int err = loop_set_autoclear(loop_fd);
close(loop_fd);
mount(dev, mnt, "ext4", 0, NULL);   // EIO: loop device already cleared

// RIGHT — mount while the setup fd is still open, then close it
int loop_fd = io_loop(dev, devsz, file);
mount(dev, mnt, "ext4", 0, NULL);
close(loop_fd);                     // mount keeps the device alive
```

When touching loop setup, check that the fd returned by `io_loop` stays open
through `mount()` and is closed on both the success and mount-failure paths.

### User resolution must happen before chroot

`user_t` getters (`get_home`/`get_name`/`get_shell`) resolve
`${rootfs}/etc/passwd` lazily. `rootfs` is a host-side path, so the passwd scan
must run before the sandbox child chroots and moves root; afterwards the path is
unreachable and the getters silently fall back to defaults.

Numeric `-u` plus a numeric `-g`/colon group is the trap: `get_gid` short-circuits
on the first numeric group token and never triggers the passwd scan, so
name/home/shell stay unresolved until a getter runs post-chroot.

```c
// WRONG — numeric group token short-circuits get_gid; no passwd read pre-chroot
get_uid(u); get_gid(u); get_groups(u);

// RIGHT — force the passwd scan while ${rootfs}/etc/passwd is reachable
user_resolve(u); get_uid(u); get_gid(u); get_groups(u);
```

When adding any new user field resolved from files, resolve it in
`eager_resolve_user` before `move_and_chroot`.

### Init script must exec the guest-relative path

`do_init` resolves the `-i` script on the host side, before the child chroots,
so it checks two locations: the path as given (host-visible when the lowerdir
exposes it) and `${root}` + script (the copy inside the container). Whichever
check succeeds, the path handed to `execve` must be **the value the user gave**,
because `execve` runs after `chroot` and resolves against the guest root.

```c
// WRONG — execs the host-prefixed path, unreachable after chroot
snprintf(buf, size, "%s", mem_path(root, script));

// RIGHT — verify the container copy, but exec the guest-relative path
char *p = mem_path(root, script);
int found = access(script, R_OK) == 0 || access(p, R_OK) == 0;
mem_free(p);
if (!found)
  return -1;
snprintf(buf, size, "%s", script);
```

This only bites with a custom `-l` lowerdir: with the default lowerdir `/` the
host path and the guest path coincide. Any future host/guest path translation
for a post-chroot exec must keep the guest form.

### Never booleanize a child's exit status

`cage` forwards the target command's exit code. Any layer that collapses the
status to `0/1` (`spawn_cage(...) != 0`, `status == 0 ? 0 : 1`) destroys it, so
`cage -- sh -c 'exit 42'` returns 1 instead of 42. Setup failures have their own
sentinel (`-1`); map that to 1 separately rather than folding everything
together.

```c
// WRONG — loses the command's exit code
return spawn_cage(cfg, ruid) != 0;

// RIGHT — propagate the code; map setup failure (-1) to 1
int r = spawn_cage(cfg, ruid);
return r < 0 ? 1 : r;
```

When decoding a raw `waitpid` status, use `exit_code_from_status` (handles
`WIFEXITED` and `WIFSIGNALED` → `128 + N`); never read `WEXITSTATUS` without
first checking `WIFEXITED`.

### ext4 backup superblock is at the group's first block, not `SB_OFF`

The primary superblock sits at byte 1024 (`SB_OFF`) inside block 0, but a backup
superblock is written at the **start** of the backup group's first block (offset
0). The kernel reads a backup with `logical_sb_block = group * blocks_per_group`
and `offset = 0`; e2fsck's `-b <blk>` does the same. Writing a backup at
`base + SB_OFF` leaves the real location zeroed, so the kernel logs `Invalid
checksum for backup superblock <blk>` at mount-time resize (it checksums zeros)
and `e2fsck -b <blk>` reports `Bad magic number`.

```c
// WRONG — primary offset, valid only for group 0
write_at(fd, bs, 1024, base + SB_OFF);
// RIGHT — backup SB at the block start; backup GDT in the next block
write_at(fd, bs, 1024, base);
write_at(fd, gdt, 2 * BLK_SIZE, base + BLK_SIZE);
```

A plain `e2fsck -fn` never reads backup superblocks, so it will not catch this.
Verify each `has_super()` group with `e2fsck -b <group * 32768> -fn <img>`.

### modprobe: unchecked return values

`modprobe_load()` returns -1 on failure (module not found, EOPNOTSUPP, etc.), but callers may ignore the return value. EOPNOTSUPP from finit_module (e.g. inside a container without module loading) causes a silent skip — subsequent code that depends on the loaded module fails cryptically.

When modifying modprobe callers, check:
- The return value of `modprobe_load()` is checked and propagated as a fatal error
- The unchecked call is removed if it was superseded by a checked call in the caller

Correct pattern:
```c
if (modprobe_load("loop") != 0)
  return -1;
```

### Filesystem Syscall Discipline

Rules for `access()` / `stat()` / `mkdir()` to avoid redundant probes.
See [syscall-discipline.md §Syscall Discipline](syscall-discipline.md#syscall-discipline).

### fork cleanup: parent + child both cleaning the same tmpdir

`fork_cleanup` creates a cleanup child that removes the temp root synchronously
via `signal_cleanup`. The parent must NOT also call `io_rmdir(root)` after the
cleanup child is created — they race and the parent gets ENOENT.

Common mistakes:
- Calling `io_rmdir(root)` in parent error paths after `fork_cleanup` succeeded
- Assuming the parent needs to clean up tmpdir when a cleanup child exists

When modifying fork/cleanup code in `cage.c` or `prep.c`, check:
- Is there a cleanup child (`fork_cleanup` / `signal_cleanup`)? If yes, the
  parent must NOT rmdir the tmpdir.
- After `signal_cleanup(*clean)` + `*clean = -1`, verify no subsequent error
  path holds a stale `root` pointer.

### pidfile: reject out-of-range PIDs on read

- **Risk**: `parse_pid()` accepted any `strtol` result below `ERANGE`, and
  `readpid()` narrowed `long` to `pid_t`. A poisoned or truncated pidfile
  containing `0`, a negative value, or a value above `INT_MAX` (which wraps on
  32-bit) could make callers treat it as valid and call `kill(0, ...)`,
  signalling the whole process group.
- **Prevention**: `parse_pid()` rejects `pid <= 0 || pid > INT_MAX`. Covered by
  `readpid_zero_rejected`, `readpid_negative_rejected` and
  `readpid_overflow_rejected` in `tests/test_proc.c`. Any new pidfile reader
  must keep the same range guard before narrowing the type.
- **Recurrence likelihood**: Medium — the pidfile is trusted input in most
  code paths.

### pidfile: open before fork, write after fork

The pidfile is opened with `O_EXCL` **before** `fork init process`, and the
child PID is written **after** the fork succeeds — all before `setup net host side`.
The pidfd is closed right after writing, before the host-side net setup.

Common mistakes:
- Closing pidfd before writing the PID — the write must happen while the fd
  is still open and locked
- Writing `getpid()` instead of the **child** PID — the pidfile must contain
  the PID of the process actually running inside the container from the
  parent namespace perspective, not the parent's own PID
- Writing the PID after `close_pid` — once closed, the fd cannot be written to
- Not unlinking the pidfile on error paths — if `spawn_init` fails after
  `create_pidfile`, the empty pidfile leaks

When modifying pidfile creation, check:
- Is the write ordered before `close_pid` and before bridge setup?
- Is the PID the **child** PID (`st->child`), not `getpid()`?
- On error paths, is the pidfile unlinked? `spawn_cleanup` handles this via
  `unlink_pidfile` (full path, no chdir dependency).
- Is the crash window between O_EXCL open (before fork) and dprintf (after
  fork) handled? `detect_existing` recovers by unlinking empty pidfiles.

Correct pattern:
```c
// pidfile opened BEFORE fork, child PID written AFTER fork
st->pidfd = create_pidfile(st->pidfile);
if (st->pidfd < 0) return -1;
// ... fork happens, st->child is set ...
dprintf(st->pidfd, "%d", st->child);   // child PID, not getpid()
close_pid(st->pidfd);
st->pidfd = -1;
```

### pidfile: unlink only after the process is confirmed gone

- **Risk**: `kill_pidfile` removed the pidfile before sending the signal and
  waiting. Other code (and e2e scripts) use pidfile absence as "session is
  gone", so a concurrent `-N` run could allocate the still-live session's veth
  ID, and `e2e_wait_session_gone` returned while the old netns still held its
  veth (`rp2` skipped the just-freed ID).
- **Prevention**: close the locked fd, call `stop_target(pid)` (which waits for
  `pidexists` to report the process gone) and only then `io_unlink_quiet`.
  Covered by `test_killpid_*` and `e2e/network.sh`'s reuse check, which also
  drains `cage-veth*` links before asserting IDs.
- **Recurrence likelihood**: Medium — "remove bookkeeping first, then kill" is
  a natural cleanup order, and the race only shows with concurrent runs.

### pidfile re-read: passing PID directly instead of re-reading

`detect_existing()` already reads and verifies the PID from the pidfile.
Callers must use this PID directly rather than opening and reading the
pidfile again.

Common mistakes:
- Calling a second function that opens+reads the same pidfile that was
  already read by `detect_existing()`
- Passing the pidfile path instead of the already-known `old_pid`

When modifying re-entry or reuse code, check:
- Did `detect_existing()` already read this pidfile? If yes, pass the
  returned `pid_t` directly rather than re-reading.
- Is there a `read_pidfile`-style wrapper that duplicates `detect_existing()`
  logic? Remove it and refactor callers to accept the PID.

Correct pattern:
```c
pid_t existing = detect_existing(name, pidfile);
if (existing > 0)
  return spawn_existing(cfg, existing);  // pass PID, don't re-read
```

### Helper extraction: preserve return value semantics

When extracting inline code into a helper function, the return value
semantics must remain identical. A helper that returns 0 on success
from a called function may be wrong if the original inline code returned
a non-zero sentinel (e.g. 1 = "needs action").

Common mistakes:
- Returning the result of an I/O function (which returns 0 on success)
  when the original inline code returned 1 to signal a state transition
- Assuming a call to `io_touch()` → 0 is equivalent to calling `io_touch()`
  + returning 1

When modifying setup/sandbox code, check:
- Does the extracted helper return the same values in the same cases as
  the original inline code?
- Are callers checking for a specific non-zero return value (e.g. 1 means
  "needs replacement") that the helper might now return as 0?

Correct pattern:
```c
// WRONG — io_touch returns 0 on success, but caller expects 1
static int create_resolv_file(const char *resolv)
{
  ...
  return io_touch(resolv);  // returns 0, not 1!
}

// RIGHT — preserve the original return value convention
static int create_resolv_file(const char *resolv)
{
  ...
  return io_touch(resolv) != 0 ? -1 : 1;
}
```

### CLONE_NEWPID: unshare before fork, not after

`man 2 unshare` says: "The calling process is not moved into the new
namespace. The first child created by the calling process will have PID 1."

This means: calling `unshare(CLONE_NEWPID)` and THEN forking creates PID 1.
Calling fork, then unshare in the child means the child's next fork becomes
PID 1 — wrong!

Common mistakes:
- Calling `unshare(CLONE_NEWPID)` inside a child process after fork
- Expecting the process that calls unshare to be PID 1

When modifying PID namespace setup, check:
- Is `unshare(CLONE_NEWPID)` called BEFORE or AFTER `pidfork()`?
- After unshare, the next fork becomes PID 1. PID 1 must NOT exit before
  its children, otherwise further forks get ENOMEM.
- PID 1 should continue running as the application, not exit after setup.

Correct pattern:
```c
// parent calls unshare BEFORE fork
if (SYS_UNSHARE(CLONE_NEWNS | CLONE_NEWPID) != 0) return -1;
pid_t child = pidfork();   // child becomes PID 1
if (child == 0) {
    // child does setup and continues as app
    child_run(...);
    _exit(1);
}
// parent waits for child (never enters new PID namespace)
```

### user privilege drop: primary gid missing from supplementary groups

When `ensure_groups()` creates the supplementary groups list, the primary gid
(`u->gid`) must always be included. If supplementary groups are given via
`-g`, the primary gid may not be in that list — it must be prepended.

Common mistakes:
- Not checking if `u->gid` is already in the groups list
- Returning early when `u->groups` is non-NULL without validating contents
- Assuming the primary gid is always present in supplementary groups

When modifying user/group setup in `user.c`, check:
- Does `ensure_groups` check whether `u->gid` is in `u->groups`?
  If not, prepend it before returning.
- After `ensure_groups`, will `setgroups()` in `do_priv_drop` include
  the primary gid?

Correct pattern:
```c
static void ensure_groups(user_t *u)
{
  size_t i;
  if (u->groups) {
    for (i = 0; i < u->groups_count; i++)
      if (u->groups[i] == u->gid) return;
    i = u->groups_count + 1;
  } else {
    i = 1;
  }
  gid_t *ng = malloc(i * sizeof(gid_t));
  if (!ng) return;
  ng[0] = u->gid;
  if (u->groups) {
    memcpy(ng + 1, u->groups, u->groups_count * sizeof(gid_t));
    free(u->groups);
  }
  u->groups = ng;
  u->groups_count = i;
}
```

### mkdir with EEXIST fallback violates access-first discipline

Raw `mkdir()` with EEXIST fallback (e.g. `mkdir(path) != 0 && errno != EEXIST`)
violates [syscall-discipline.md §Syscall Discipline](syscall-discipline.md#syscall-discipline) Rule 4: "`mkdir()` over existing
directories (handling EEXIST) is a bug. Always `access(F_OK)` first."

Common mistakes:
- Using `mkdir()` + EEXIST fallback when the directory may already exist —
  wastes a syscall and is a rule violation
- Calling `io_mkdir()` (which uses mkdir-first internally) when the parent is
  known to exist — `io_mkdir`'s `mkdir_parent` recursion is redundant
- Not propagating `io_exists()` errors correctly — must check `< 0` before
  interpreting the result

When modifying code that creates directories, check:
- Is there a raw `mkdir()` with EEXIST fallback? Replace with `io_exists()` + raw `mkdir()`
- Is the directory expected to exist? Use `io_exists()` first
- Does the code call `io_mkdir()` when the parent is known to exist? Use raw `mkdir()`
- Does `io_exists()` return -1 get handled before interpreting the boolean result?

Correct pattern:
```c
int e = io_exists(path);
if (e < 0) return -1;
if (e == 1 && mkdir(path, 0755) != 0) return -1;
```

### Config struct initialization: string pointer defaults must match runtime behavior

When a config struct field is a string pointer (e.g. `cfg->group`), setting
it to a non-NULL default like `"0"` can bypass downstream "is this set?"
checks that use `== NULL`. The `fixup_primary_group` function checks
`!prim_group_str` to decide whether to apply the gid=uid fallback — a
non-NULL default defeats this guard.

Common mistakes:
- Initializing a string-typed config field to `"0"` or `""` instead of `NULL`
  to represent "not specified"
- Not tracing how downstream code uses the value — a non-NULL sentinel
  that carries semantic meaning ("default group is 0") may interfere with
  `!= NULL` guards designed to detect "user didn't set this"

When modifying config structs in `cage_config_init` or adding new config
fields, check:
- Is the field's default `NULL` when "not set" is the intended state?
- Does any downstream code branch on `== NULL` to detect absence?
  If so, a non-NULL default will silently bypass that branch.

Correct pattern:
```c
// WRONG — "0" is non-NULL, bypasses !prim_group_str guard
cfg->group = "0";
cfg->user  = "0";

// RIGHT — NULL means "not specified", let downstream handle defaults
cfg->group = NULL;
cfg->user  = "0";     // user still defaults to "0" (numeric root)
```

### resolve_pidfile: computed default not written back to cfg

`resolve_pidfile` computes a default pidfile path when `cfg->pidfile` is
NULL. Callers that pass `cfg->pidfile` directly (`killpid`, `detect_existing`)
handle NULL internally by computing their own default — so `cfg->pidfile`
*can* remain NULL without breaking those paths. However, setting `cfg->pidfile`
in `resolve_pidfile` simplifies debugging and future callers.

Common mistakes:
- Computing a default without writing it back to `cfg`, then passing
  `cfg->pidfile` (still NULL) to a function that does NOT handle NULL

When modifying pidfile resolution, check:
- Does the downstream function accept NULL pidfile? If not, pass the
  resolved `st->pidfile` instead of `cfg->pidfile`.

The current code sets `cfg->pidfile = pf` after computing the default,
which is correct and recommended.

### Mock bypass: existing tests silently become mock tests when bypass is added

When a function gains a mock bypass (return early with mock value), any existing
test that calls that function will now receive the mock default instead of
exercising the real code path — the test silently becomes a mock test.

Common mistakes:
- Adding `_mock_*_ret` bypass to a function without auditing existing callers
- Assuming existing tests will still exercise real code paths after the bypass
  is added
- Not resetting the mock control to `-1` in tests that need the real code path

When modifying a function to add a mock bypass, check:
- Are all existing callers of this function (in tests) updated to set the mock
  control to `-1` before calling if they need real code execution?
- Is the mock control initialized to the bypass-default (e.g., 0) in
  `mock_sys_reset()`?

Correct pattern:
```c
// WRONG — test expects real code but gets mock value 0
void test_some_error_path(void)
{
  mock_sys_reset();         // sets _mock_same_fs_ret = 0 (bypass)
  // ... setup error condition ...
  ASSERT_EQ(function_under_test(...), -1);  // gets 0 instead!
}

// RIGHT — disable bypass before calling real code
void test_some_error_path(void)
{
  mock_sys_reset();
  _mock_same_fs_ret = -1;   // disable bypass, exercise real code
  // ... setup error condition ...
  ASSERT_EQ(function_under_test(...), -1);  // gets expected -1
  _mock_same_fs_ret = 0;    // restore default for subsequent tests
}
```

Note: when wrappers are moved from `SYS_*` macros to weak symbols (see next
section), bypass pattern remains the same — the mock control variables and
`mock_sys_reset()` stay, only the interception mechanism changes.

### Test mock infrastructure: weak symbols

All syscall and stdlib wrappers are defined as weak symbols in
`src/util/syscall.c`. Tests provide strong overrides in `tests/mock_syscall.c`.
At link time, the linker picks the strong definition — no `#ifdef` guards,
no separate test compilation, no `TEST_STATIC`.

Mock control variables (`_mock_mount_ret`, `_mock_mount_count`,
`_mock_mount_fail_after`, etc.) live in `tests/mock_syscall.c`. The mock
function checks them and returns early if a mock value is set, otherwise
calls the real implementation via the weak symbol's default.

```c
// tests/mock_syscall.c — strong override
int _mock_mount_ret = 0;
int _mock_mount_count = 0;
int _mock_mount_fail_after = 0;

int mock_mount(...)
{
  _mock_mount_count++;
  if (_mock_mount_fail_after && _mock_mount_count >= _mock_mount_fail_after)
    return _mock_mount_ret;
  if (_mock_mount_ret) return _mock_mount_ret;
  return mount(...);  // call real
}
```

Reset via `mock_sys_reset()` before each test:

```c
void mock_sys_reset(void)
{
  _mock_mount_ret = 0;
  _mock_mount_count = 0;
  _mock_mount_fail_after = 0;
  // ... all mock controls ...
}
```

Common mistakes:
- Using `_mock_*_ret` without calling `mock_sys_reset()` first — stale values
  from the previous test bleed over
- Using `_mock_*_ret = 0` to mean "disable mock" when 0 is a valid success
  return — use `-1` as the sentinel for "no mock" (see Mock bypass pitfall)
- Forgetting to restore mock defaults after a test that changes them —
  `mock_sys_reset()` at the start of each test prevents this

When modifying or adding mock infrastructure, check:
- Does the new mock need `_mock_*_fail_after` for Nth-call positioning?
- Does the new mock record call count for ordering assertions?
- Is `mock_sys_reset()` called at the start of each test that uses mocks?

Correct pattern:
```c
void test_some_error_path(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;   // mock mount to fail
  ASSERT_EQ(function_under_test(...), -1);
  ASSERT_EQ(_mock_mount_count, 1);   // verified exactly one attempt
}
```

### killpid: silently ignoring kill() errors hides process death failures

`kill(pid, 0)` (pidexists), `kill(pid, SIGTERM)`, and `kill(pid, SIGKILL)` can
all fail with errors other than ESRCH (e.g. EPERM, EINVAL). Ignoring these
errors means `killpid` may return success even though the process was never
signalled — the caller assumes the target is dead when it is still alive.

Common mistakes:
- Checking only for ESRCH and treating all other kill() failures as success
- Calling `waitpid_graceful` unconditionally after a failed `kill(SIGTERM)`
- Looping through `pidexists` without handling its error return (-1), which
  keeps spinning for 50 iterations on a persistent error

When modifying `killpid` / `waitpid_graceful` / `kill_gracefully`, check:
- Does every `kill()` call check `errno == ESRCH` before silently returning 0?
- Does `pidexists()` return -1 get handled (stop + return error) rather than
  ignored?
- Is `waitpid()` called unconditionally after `kill(SIGKILL)` even when the
  process may have already died (ESRCH)?

Correct pattern:
```c
if (kill(pid, SIGTERM) != 0) {
  if (errno == ESRCH) return 0;   // already dead
  return -1;                      // real error
}
```

### cgroup v2: write_enable_controllers reads from parent path after PID moved to child

`cgroup_pre_unshare_at` moves the PID into `cage-{pid}` before writing
`cgroup.subtree_control`. A cgroup with running processes cannot have
its subtree_control modified (EBUSY). The fix reads `cgroup.controllers`
from the **child** cgroup (it inherits available controllers from the
parent) and writes to the child's `subtree_control`.

Common mistakes:
- Reading `cgroup.controllers` from the parent (`root`) instead of the
  child (`cgdir`) when writing to the child's `subtree_control`
- Keeping the `enable_cgroup_controllers` call in `cgroup_v2_enter`
  (child path) when the parent already set up subtree_control for the
  child cgroup — the child just needs to create `cage-parent` dir and
  write PID 1 to `cgroup.procs`

When modifying cgroup delegation code, check:
- Is `write_enable_controllers` reading from the same cgroup directory
  whose `subtree_control` it's writing to?
- After `cgroup_pre_unshare_at` runs, does `cgroup_v2_enter` try to
  re-enable controllers that the parent already set up?
- Does the test simulate both parent's and child's `cgroup.controllers`
  files when testing `write_enable_controllers`?

Correct pattern:
```c
// cgroup_pre_unshare_at: read from child, write to child
static int write_enable_controllers(const char *cgdir)
{
  char *ctrl_path = mem_path(cgdir, "cgroup.controllers");
  // ... read ctrls, write to cgdir/cgroup.subtree_control
}
```

### GUI directory ownership: parent dirs created as root must be chowned at creation time

When `mount_file_acl` → `mount_file` → `mkparent` creates parent directories for
bind-mount targets inside the container root (before `move_and_chroot`), those
directories are created by `io_mkdir` which runs as root. They must be chowned
to the guest uid:gid at creation time, using root-prefixed paths.

Common mistakes:
- Creating bind-mount parent directories via `mkparent` → `io_mkdir` without
  immediately chowning them to the guest uid:gid
- Constructing paths without the `root` prefix in any post-hoc chown function
  (`fixup_home_files` originally built `/home/user/.config` instead of
  `<root>/home/user/.config`, making `io_exists`/`io_chown` operate on host
  paths — the chown silently did nothing)
- Relying on a post-hoc chown pass that ignores failures (`void` return)
- Gating the post-hoc pass on a prior operation succeeding, so a mount failure
  skips the chown entirely

When modifying GUI mount code, check:
- Does `mkparent` or `io_mkdir` create a directory that will be accessed by
  a non-root process? If yes, chown immediately after creation using the
  absolute path (which already includes the `root` prefix).
- Does any post-hoc chown function construct paths with `mem_path(ctx->root, ...)`
  and propagate failures via `int` return?
- Does `mount_pulse` create `/run/user/%u/pulse/` as root without chowning?
  (Covered by `mkparent` chown since `mount_file` creates the parent of the
  bind target.)
- If a directory is created by any code path inside the container root before
  `move_and_chroot`, are the paths prefixed with `root`?

Correct pattern (current implementation in `gui.c`):
```c
/* intermediates stay 0:0; only the final directory belongs to the guest */
return io_mkdir_parents_at(rootfd, parent_rel, uid, gid, 0755);
```

### pidfd close must release flock via close_pid, not close

`close_pid(pidfd)` does `flock(pidfd, LOCK_UN)` then `close(pidfd)`. Calling `close()` directly
on the pidfd skips the unlock — the kernel eventually reclaims the lock on fd close, but the
explicit unlock is required for correctness. The flock must be released before `unlink_pidfile`.

Common mistakes:
- Using `close(st->pidfd)` instead of `close_pid(st->pidfd)` in any code path that closes the pidfd
- Adding a new error path that closes the pidfd without checking which close variant is used by
  the other paths (all must use `close_pid`)
- Forgetting to gate the `close_pid` call with `if (st->pidfd >= 0)` like the other sites do

When modifying any code that closes the pidfile fd, check:
- Does it call `close_pid(...)` (not `close(...)`)?
- Is the `close_pid` call placed after all pidwaits and just before `unlink_pidfile`, so the
  flock is held during child/worker wait?
- Is there a `>= 0` guard on the pidfd before calling `close_pid`?

Correct pattern:
```c
if (st->pidfd >= 0)
  close_pid(st->pidfd);
unlink_pidfile(st->pidfile);
```

Areas that commonly produce project-specific entries:
- **State machines**: protocol handshakes, sequence tracking, ordering constraints — desync is silent
- **Variable-length encoding**: TLV or dispatch-by-type parsers — always validate remaining buffer length against declared length before reading
- **Cross-resource lifecycle**: paired acquire-use-release across subsystems — verify cleanup on ALL error paths

### Host home resolution: SUDO_HOME takes precedence over HOME

When running under sudo, `$HOME` points to `/root`, not the original user's home.
The `SUDO_HOME` environment variable (set by sudo) carries the original user's home.

Common mistakes:
- Using `getenv("HOME")` directly when resolving the host user's home directory
- Not checking `SUDO_HOME` first before falling back to HOME
- Passing the wrong home path to pulse cookie lookup or Xauthority resolution

When modifying code that resolves the host user's home, check:
- Does the code use `getenv("SUDO_HOME")` first, then fallback to `getenv("HOME")`?
- Is the resolved home used for host-side paths (bind mounts, cookie lookup) vs guest-side paths?
- Is the value validated before use? It feeds a bind-mount source, so it must be
  an absolute path with no `..` component that names an existing directory;
  otherwise treat it as absent (`resolve_host_home()` → `host_home = NULL`).

Correct pattern:
```c
const char *host_home = getenv("SUDO_HOME");
if (!host_home)
  host_home = getenv("HOME");
/* then validate: absolute, no "..", existing directory, else NULL */
```

### Guest home resolution: check -e HOME before user->home

When constructing paths inside the container that depend on the guest's home
directory (pulse cookie, .Xauthority), the code must use the guest's HOME as
resolved by the Container Lifecycle spec, not `user->home` directly.

Common mistakes:
- Passing `user->home` to `setup_gui_mounts` / `mount_user_home` without
  checking if `-e HOME` was provided
- Calling `fixup_home` or similar functions that create `user->home`
  unconditionally — the spec doesn't create home unless truly needed

When modifying GUI mount or home-path code, check:
- Is there a `-e HOME` override in the env map? Use `env_map_get(&env->map, "HOME")`
  which returns the override value if present.
- Is `user->home` used as a fallback, not as the sole source?
- Is the resolved `guest_HOME` passed through to `setup_gui_mounts` rather than
  the raw `user->home`?

Correct pattern:
```c
// WRONG — ignores -e HOME
setup_gui_mounts(&gc, user->home);

// RIGHT — resolve per spec formula
const char *gh = env_map_get(&env->map, "HOME");
if (!gh) gh = user->home ? user->home : "/home/.user";
setup_gui_mounts(&gc, gh);
```

### Host XDG_RUNTIME_DIR resolution: fallback must be shared

When running as root via setuid or sudo, `$XDG_RUNTIME_DIR` may be absent from
the environment. Both the env-detection path (`set_gui_env` in `env.c`) and the
mount-src path (`setup_gui_mounts` in `gui.c`) need to resolve the same host
path, but with consistent fallback logic.

Common mistakes:
- Reading `getenv("XDG_RUNTIME_DIR")` without fallback in one path but using
  a fallback in another
- Using `geteuid()` instead of `getuid()` in the fallback (setuid binary issue)
- Not factoring in `SUDO_UID` for sudo-invoked scenarios

When modifying code that resolves the host XDG_RUNTIME_DIR, check:
- Is `get_host_xdg_dir()` used, rather than ad-hoc local logic?
- Does the fallback chain cover: env var → `SUDO_UID` → `ruid`?
- Is the `ruid` parameter passed from the initial capture in `main()`, rather than calling `getresuid()`/`getuid()` internally?
- Is a set-but-empty `$XDG_RUNTIME_DIR` treated as unset (test `x && x[0]`, not `x`)? An empty value is not a usable directory.

Correct pattern:
```c
const char *get_host_xdg_dir(char *buf, size_t size, uid_t ruid)
{
  const char *xrd = getenv("XDG_RUNTIME_DIR");
  if (xrd && xrd[0]) return xrd;
  const char *su = getenv("SUDO_UID");
  if (su) { snprintf(buf, size, "/run/user/%s", su); return buf; }
  snprintf(buf, size, "/run/user/%u", ruid);
  return buf;
}
```

### Guest-side path construction: guest UID must replace host UID in container targets

When constructing paths inside the container (mount targets, env vars), the guest
UID (from `-u`) must be used, not the host UID (from the resolved host path).
Mount functions and env-var helpers that use the host-sourced path as a template
for the guest-side path will produce wrong paths when guest UID ≠ host UID.

Common mistakes:
- Using `mem_path(xrd, wl_name)` as the mount target inside the container —
  `xrd` is the host path and contains the host UID
- Setting `XDG_RUNTIME_DIR=${xrd}` in `gui_envs` — `xrd` is the host XDG path,
  not the guest's
- Deriving the Pulse mount target from `resolve_pulse_src(xrd)` which returns
  a host-side path

When modifying GUI mount or env-var code, check:
- Is the path being constructed intended for the host side (source of bind mount)
  or the guest side (mount target / env var value)?
- For guest-side paths, is `ctx->uid` (or the container user's uid) used instead
  of reusing the host path?
- After calling `fixup_guest_xdg_dir()`, is the `XDG_RUNTIME_DIR` entry in
  `gui_envs` updated to `/run/user/<guest_uid>`?

Correct pattern:
```c
// WRONG — uses host path as both source and target
char *src = mem_path(xrd, wl_name);
int r = mount_file(ctx->root, src, src);  // target has host UID!

// RIGHT — construct target with guest UID
char *src = mem_path(xrd, wl_name);
char target[64];
snprintf(target, sizeof(target), "/run/user/%u/%s", ctx->uid, wl_name);
int r = mount_file(ctx->root, target, src);   // target has guest UID
```

### GUI operations must run in child, not parent

When `-G` (GUI) is given, all socket probing (`access()` for X11/Wayland/Pulse),
env var setting (DISPLAY, WAYLAND_DISPLAY, XDG_SESSION_TYPE, etc.), and bind
mounting must happen in the child process after `ns_setup` and `mount_all`, as
a single step before `move_and_chroot` ([lifecycle.md §GUI Setup](lifecycle.md#gui-setup)).

Common mistakes:
- Probing sockets or setting GUI env vars in `parse_envs()` or any parent-side
  function called before fork/ns_setup
- Adding new GUI env vars to `set_gui_env()` or `capture_host_paths()` — these
  are parent-side and must only capture host paths for the child to use
- Assuming `access()`/`readdir()` in the parent produces the same results as in
  the child (the child has a different mount namespace)

When modifying any GUI-related code, check:
- Is the code running in the parent's `parse_envs`/`do_early_setup` path? If yes,
  it must NOT probe sockets or set GUI env vars — only capture host paths.
- Is the code running in the child's `child_setup_gui` path? If yes, it runs
  after `setup_gui_mounts` and can safely set GUI env vars via `env_set_gui_envs`.
- Does any new env var depend on container-local state (mounted sockets, overlay
  paths)? It must be set in the child, not the parent.

Correct pattern:
```c
// WRONG — parent-side socket probing in parse_envs
if (gui) {
    capture_host_paths(env, ruid);   // OK — only host paths, no probing
    add_x11(&env->map, disp);        // WRONG — socket probing in parent
}

// RIGHT — parent only captures paths, child probes + sets env vars
// parent: parse_envs with gui=1 calls capture_host_paths(env, ruid)
// child:  child_setup_gui → setup_gui_mounts → env_set_gui_envs(env)
```

### access() and real uid: resolved by setreuid in super_escalate

When a process has different real and effective UIDs (e.g. setuid binary, or
`-G` flag in cage), `access(path, F_OK)` checks the **real** uid, not the
effective — spurious EACCES for paths owned by the real uid.

**Resolution**: `super_escalate()` in `super.c:89-98` calls `setreuid(0, 0)`
BEFORE any GUI code runs (including the child's `child_setup_gui`). This
makes real uid == effective uid == 0, so `access()` works correctly for all
paths reachable by a root process. No `faccessat(AT_EACCESS)` is needed.

`io_exists()` in `io.c:20-29` uses `access(path, F_OK)` — intentionally,
not as a bug. After `super_escalate`, real uid is 0, matching effective uid.

Common mistakes:
- Adding GUI code that assumes `access()` might fail due to real uid mismatch
  (it won't — `setreuid` already equalised them)
- Reintroducing a `getresuid()` call in the child path (see §ruid after
  privilege change) or anywhere downstream of `super_escalate`
- Changing `io_exists` to `faccessat(AT_EACCESS)` thinking it's a fix —
  would actually change semantics from real-uid-based to effective-uid-based
  access, potentially bypassing DAC restrictions the child's root should obey

Common mistakes:
- Using `mem_path(xrd, wl_name)` as the mount target inside the container —
  `xrd` is the host path and contains the host UID
- Setting `XDG_RUNTIME_DIR=${xrd}` in `gui_envs` — `xrd` is the host XDG path,
  not the guest's
- Deriving the Pulse mount target from `resolve_pulse_src(xrd)` which returns
  a host-side path

When modifying GUI mount or env-var code, check:
- Is the path being constructed intended for the host side (source of bind mount)
  or the guest side (mount target / env var value)?
- For guest-side paths, is `ctx->uid` (or the container user's uid) used instead
  of reusing the host path?
- After calling `fixup_guest_xdg_dir()`, is the `XDG_RUNTIME_DIR` entry in
  `gui_envs` updated to `/run/user/<guest_uid>`?

Correct pattern:
```c
// WRONG — uses host path as both source and target
char *src = mem_path(xrd, wl_name);
int r = mount_file(ctx->root, src, src);  // target has host UID!

// RIGHT — construct target with guest UID
char *src = mem_path(xrd, wl_name);
char target[64];
snprintf(target, sizeof(target), "/run/user/%u/%s", ctx->uid, wl_name);
int r = mount_file(ctx->root, target, src);   // target has guest UID
```

### mkdir_parents_own: loop creates intermediate prefixes only, missing final component

`mkdir_parents_own` iterates through each `/` in the path and creates the prefix
directory at each boundary. When the path itself is a directory (no trailing `/`),
the final component is never created — only its ancestors are.

Common mistakes:
- Calling `mkdir_parents_own` and assuming the entire path is created (e.g.,
  passing `/tmp/root/home/user/.config/pulse` expecting `/pulse` to exist)
- Adding `io_touch` after `mkdir_parents_own` in a caller that assumes the
  parent directory already exists — `io_touch` gets ENOENT
- Writing a similar loop in a new function and forgetting to handle the
  trailing non-slash component

When modifying any path-creation loop that processes only `/` delimiters, check:
- Is there a post-loop call to handle the final path component (the part
  after the last `/`)?
- Does the function name describe what it creates? "mkdir_parents_own"
  implies it creates parents; a caller that also needs the final directory
  must create it separately.
- If the function is changed to also create the final component, are all
  existing callers still correct? Some callers may pass a path where the
  final component is a filename (e.g., `cookie`), not a directory — creating
  the full path as a directory would be wrong.

Correct pattern:
```c
static int ensure_all_dirs(const char *path, uid_t uid, gid_t gid)
{
  char buf[4096];
  size_t len = strlen(path);
  if (len >= sizeof(buf)) return -1;
  memcpy(buf, path, len + 1);
  for (char *p = buf + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (try_create_own(buf, uid, gid) != 0)
        return -1;
      *p = '/';
    }
  }
  // Also create the final path component — it's a directory, not a file
  if (try_create_own(buf, uid, gid) != 0)
    return -1;
  return 0;
}
```

### ruid after privilege change: second getresuid() may return wrong real uid

`getresuid()` (or `getuid()`) called after `setreuid()` has changed the real uid
returns the new (privileged) value, not the original caller's uid. If the
original ruid is needed for user-identity lookups (e.g., XDG_RUNTIME_DIR
fallback), it must be captured before any privilege changes and threaded
through the call chain as a parameter.

Common mistakes:
- Calling `getresuid()` in a helper function that may run after privilege
  escalation (setuid, sudo `-G`, explicit `setreuid`)
- Not passing `ruid` as a parameter from the initial capture site to all
  downstream consumers that need the original user identity
- Adding a new `getuid()`/`getresuid()` call in code that runs between
  `super_escalate` and privilege drop

When modifying any code path that touches real uid, check:
- Is `getresuid()` called after any `setreuid()`? If yes, capture ruid before
  the privilege change and pass it as parameter.
- Does any function query the user's real uid independently, or does it
  receive it from the caller?
- If a new code path needs the original user's uid (e.g., for path
  construction, file access, or identity lookup), is it receiving `ruid`
  from the initial capture, or calling `getresuid()` itself?

Correct pattern:
```c
// WRONG — second getresuid() may return wrong value
const char *get_host_xdg_dir(char *buf, size_t size)
{
  uid_t ruid;
  getresuid(&ruid, NULL, NULL);
  // ... use ruid ...
}

// RIGHT — ruid passed as parameter from initial capture
const char *get_host_xdg_dir(char *buf, size_t size, uid_t ruid)
{
  // ... use ruid (pass through from main's initial getresuid)
}
```

### Cgroup lifecycle: detection must be separated from usage

`cgroup_pre_unshare_at()` and `cgroup_cleanup()` used to call `cgroup_detect()`
and `getpid()` internally, conflating detection with usage. [lifecycle.md §Cgroup Lifecycle](lifecycle.md#cgroup-lifecycle) specifies
4 distinct lifecycle points: (1) detect once in the caller, (2) pre-unshare guarded
by type==2, (3) mount cgroupfs guarded by `!cfg->unshare_cgroup`, (4) cleanup
guarded by type==2 + dir exists.

Common mistakes:
- Embedding `cgroup_detect()` inside functions that only need to use the detected value
- Calling `getpid()` inside a cleanup function instead of accepting PID as a parameter
- Not passing the cached `cg_fs_type` through structs like `prep_result`, `spawn_st`, `run_ctx`

When modifying cgroup code, check:
- Does the function receive `fs_type` and `pid` as parameters, or does it call
  `cgroup_detect()` / `getpid()` internally?
- Is `cgroup_detect()` called only once and the result cached for the process lifetime?
- Does `root_private` flow correctly from detection → ns_setup?

Correct pattern (from the refactored code):
```c
// setup_cgroup in cage.c — detects once
st->cg_fs_type = cgroup_detect();
st->cg_pid = getpid();

// cgroup_pre_unshare_at receives values, doesn't detect internally
cgroup_pre_unshare_at(root, fs_type, pid, &root_private);

// cgroup_cleanup receives values, doesn't detect internally
cgroup_cleanup(st->cg_pid, st->cg_fs_type);
```


### GUI env: DISPLAY must be the host value as-is, not re-formatted

- **Risk**: `env_set_gui_envs` synthesised `:<n>.0` from the resolved socket
  number even when the host `$DISPLAY` was non-empty, silently changing the
  user's display (e.g. `:0` became `:0.0`). The spec ([§Environment
  Variables](lifecycle.md#environment-variable-precedence), [§Re-entry
  Flow](lifecycle.md#re-entry-flow-joining-existing-instance) rule 5) requires
  the host value to be passed through unchanged; `:N.0` is only the fallback
  when host `$DISPLAY` is empty/unparseable and the socket was auto-discovered.
- **Prevention**: GUI env construction must keep the display string (host or
  `-e` override) verbatim; only format `:N.0` in the empty/unparseable
  fallback branch. Cover with a test asserting a non-empty host `DISPLAY`
  reaches the guest map unchanged (`test_gui_x11_host_display`).
- **Recurrence likelihood**: Medium.

### Re-entry must not default XDG_RUNTIME_DIR without a socket

- **Risk**: `fixup_xdg_runtime_dir` (src/core/reuse.c) set
  `XDG_RUNTIME_DIR=/run/user/<uid>` unconditionally on every re-entry that
  lacked the variable in its `-e` map — i.e. on all of them, since only `-e`
  options populate it — so an X11-only `-G` re-entry yielded
  `{DISPLAY, XDG_RUNTIME_DIR}` while the first entry yielded `{DISPLAY}`,
  violating [§Re-entry Flow](lifecycle.md#re-entry-flow-joining-existing-instance)
  rule 5 ("the same GUI environment variables as a first-entry `-G` run") and
  [§Environment Variables](lifecycle.md#environment-variable-precedence)
  (`XDG_RUNTIME_DIR` is set only when a Wayland or pulse socket is detected).
- **Prevention**: every GUI env var on both first entry and re-entry must
  come from the socket-gated flow (`child_setup_gui` → `apply_gui_env` →
  `maybe_set_xdg_runtime`, gated on `xrd_created`); the re-entry path must
  not add convenience defaults of its own. The reuse env build is isolated in
  `reuse_setup_env()` and covered by `test_reuse_env_no_socket_no_xdg` (no
  socket → no `XDG_RUNTIME_DIR`), `test_reuse_env_wayland_sets_xdg` (socket →
  `/run/user/<uid>`), and `test_reuse_env_e_xdg_wins` (`-e` wins).
- **Recurrence likelihood**: Medium.

### io_rmtree follows symlinks and can remove host paths

- **Risk**: `io_rmtree` and `test_rmtree` recursively follow symlinks. If a symlink points outside (e.g. `/tmp/cage-*/run/shm` → `/dev/shm`), recursing can follow it and attempt to delete host filesystem content.
- **Prevention**: Treat symlink entries as leafs — do not `opendir` them; unlink the symlink itself (using `lstat` to detect them). The current implementation already checks the path argument with `lstat` and unlinks if symlink; any entry that is a symlink must be unlinked, not opened. Cover with tests that assert `/dev/shm` contents are untouched when removing a tree containing a symlink to `/dev/shm`.
- **Recurrence likelihood**: High.

### Re-entry must not re-create GUI mounts or re-apply socket ACLs

- **Risk**: the re-entry env path looks like a fresh GUI setup; calling `child_setup_gui()`/`setup_gui_mounts()` there stacks bind mounts on every re-entry (mountinfo lines grow) and re-runs `set_x11_acls()`, mutating the live instance's joined mount namespace instead of leaving it alone.
- **Prevention**: re-entry builds env via `child_gui_env_only()` (`begin_gui_ctx` + detection-only `x11_resolve_display()`/`xdg_detect_socks()` + `apply_gui_env`) — never `setup_gui_mounts()`. The detection helpers mirror the mount-time conditions (including the `guest_ps` pulse nuance) so the resulting env is identical to a first entry. Covered by `test_reuse_env_wayland_sets_xdg` and `test_reuse_env_x11_fallback_display`, which assert `_mock_mount_count`/`_mock_setxattr_count` stay 0 while env parity holds.
- **Recurrence likelihood**: Medium — re-entry still needs a full GUI env, so wiring the complete first-entry setup "for convenience" is easy to reintroduce; the zero-mount assertions in the reuse env tests catch regressions.

### Group enumeration guard must use parse-time intent, not resolution progress

- **Risk**: `resolve_groups` Path A (name-based `/etc/group` enumeration) is guarded by `should_enumerate_groups()`. Keying that guard on `uid_filled` ("no uid resolved yet") makes the outcome depend on getter order: `uid_filled` flips to 1 the moment passwd is parsed, so any earlier getter (`user_resolve` in `eager_resolve_user`, `get_gid`, the GUI context builder) silently disables enumeration and a named `-u` keeps only its primary gid.
- **Prevention**: decide Path A from immutable parse-time state: `name_given` (set when `-u` supplied a name) plus "no group tokens, no gid yet". Numeric `-u` never enumerates, even after passwd resolution fills `name` from a passwd entry. Covered by `test_name_groups_after_early_resolve` (named user, resolve-first order → full supplementary list) and `test_numeric_uid_no_enum_after_resolve` (numeric stays lazy).
- **Recurrence likelihood**: Medium — resolution-progress flags are tempting guards for "not yet done" conditions; any new lazy-resolution path must snapshot parse-time intent the same way.

### Every object group's depfile must be wired into `-include`

- **Risk**: the Makefile compiles with `-MMD -MP` in every object rule, so depfiles exist for all groups, but only some are `-include`d. When `obj/tests/*.d` was not included, a header-only change (`src/cli/args.h`) left the test objects compiled against the previous struct layout while the edited `.c` files were rebuilt — the resulting mixed-ABI `obj/test_runner` segfaulted with no output and `make -n` afterwards claimed everything was up to date. The quality gate silently ran against stale tests for any change that only touched a header.
- **Prevention**: every object group has a `*_DEPS := $(<group>_OBJS:.o=.d)` variable and its own `-include` next to `$(DEPS)`/`$(COV_DEPS)`/`$(TEST_DEPS)` (Makefile:148-150). When adding a new object directory or rule, wire its depfiles in the same commit; verify by `touch`ing a shared header and checking the expected objects appear in `make -n`.
- **Recurrence likelihood**: Medium — the failure is silent (only `.c` edits trigger rebuilds, `make -n` looks idle), so a new object group can repeat it unnoticed.

### Credential reads must be checked before the privilege decision

- **Risk**: `getresuid()`/`getresgid()` return values were discarded, leaving
  uninitialized locals to feed `super_escalate()`'s escalation decision. A
  failed read silently becomes "not root" or, worse, a stale value drives the
  setuid path.
- **Prevention**: check every credential read and abort with a diagnostic
  (`get_ids()` in `src/main.c`). No test seam exists for `main.c`, so this is
  verified by inspection; the rule applies to any future `getresuid`/`getreuid`
  call before a privilege change.
- **Recurrence likelihood**: Medium — these calls "cannot fail" in practice, so
  their returns are habitually dropped.

### setreuid/setregid results are checked before the self-heal continues

- **Risk**: `super_escalate()` called `setreuid(0, 0)` / `setregid(0, 0)` and
  ignored the results, so under `no_new_privs` or a capability-dropped
  container it continued to `super_fix()` as though it were fully root.
- **Prevention**: `drop_saved_ids()` checks both returns and aborts with a
  diagnostic. Both calls go through the `sys_setreuid`/`sys_setregid` wrappers
  so the failure paths are unit-tested (`tests/test_super.c`).
- **Recurrence likelihood**: Medium — same dropped-return habit as the
  credential reads.

### `argv[optind - 1]` does not identify a `--` separator

- **Risk**: `cfg->has_dashdash = (optind > 1 && strcmp(argv[optind - 1],
  "--") == 0)` also fires when `--` was consumed as an option's argument
  (`cage -a --`), so a run with no command launched the default shell instead
  of reporting "no command specified" — a silent behavior change.
- **Prevention**: derive the separator from getopt's progression, not from the
  token after the parse. Track `prev = optind` after each successful option;
  a real separator is the case where the loop stopped one step later at the
  same token (`optind == prev + 1 && argv[prev] == "--"`). Covered by
  `test_dash_dash_as_option_arg`, `test_dash_dash_as_option_arg_then_separator`,
  `test_dash_dash_after_flag`, `test_dash_dash_bare`.
- **Recurrence likelihood**: Medium — post-hoc `argv[optind - 1]` inspection is
  a common shortcut that collides with option arguments.

### Never interpolate a path into a shell command

- **Risk**: `popen("%s \"%s\"", prog, path)` ran the decompressor through
  `/bin/sh`, so a `"`, `$` or backtick in the module path (which comes from
  `modules.dep`, a file cage reads as root) became command execution — the
  double quotes rejected the intended value only until the first quote.
- **Prevention**: run the filter with `fork` + `execp` and an explicit `argv`
  (`spawn_filter()`/`exec_filter()` in `src/pkg/modload.c`), never a shell.
  Validate or constrain the path where possible, but argv construction is the
  load-bearing fix. Covered by `test_load_module_path_xz_no_shell`, which puts a
  `"; touch …` sequence in the module name and asserts the sentinel is absent.
- **Recurrence likelihood**: Medium — shell-out sites look convenient and the
  quoting mistake is invisible without a hostile filename.

### Numeric ids parsed from CLI/user input must be range-checked

- **Risk**: `(uid_t) strtoul(s, NULL, 10)` narrows silently: `"4294967296"`
  wraps to `0`, so a request for an out-of-range uid quietly becomes **root**
  (`-u 4294967296` ran as uid 0). `ERANGE` is also discarded.
- **Prevention**: parse through a checked helper that rejects `ERANGE`, trailing
  garbage, and any value above `UINT32_MAX` (`parse_id()` /
  `store_identity()` / `parse_one_token()` in `src/user/user_parse.c`), and
  propagate the error so `user_new()` returns `NULL`. Covered by
  `test_uid_overflow_rejected`, `test_uid_way_overflow_rejected`,
  `test_gid_overflow_rejected`, `test_uid_max_accepted`.
- **Recurrence likelihood**: Medium — every `strtoul`/`atoi` that feeds an id
  repeats the truncation.

### Privileged mounts must refuse a symlinked mount point

- **Risk**: the ext4 loopback mount point (`lp->loopdir`) is derived from the
  appdir and mounted as root before any `CLONE_NEWNS`. A symlink swapped in at
  that path redirects the privileged `mount` to its target, giving an
  unprivileged caller control over what the kernel mounts onto.
- **Prevention**: immediately before mounting, `lstat` the path and refuse it
  unless it is a real directory (`require_real_dir()` in
  `src/sandbox/ext4fix.c`, backed by `sys_isdir_nofollow()`), so a symlink or
  non-directory aborts the run. Covered by
  `test_handle_same_fs_symlink_loopdir_fails`.
- **Recurrence likelihood**: Medium — any future `mount`/create on a
  caller-influenced path needs the same check.

### Path-based xattr writes on attacker-writable paths must be fd-based

- **Risk**: `acl_set()` runs on socket paths under world-writable
  `/tmp/.X11-unix/` and on bind-mounted guest paths. A `stat(path)` followed by
  `setxattr(path, …)` follows symlinks, so a local attacker can swap a symlink
  in between the two calls (TOCTOU) and have root write an ACL to an arbitrary
  file. `-e PULSE_COOKIE=../../root/x` is the same escape through the guest path.
- **Prevention**: open the target once with `O_PATH` and use `fsetxattr` on that
  fd. When the path must not be a symlink, open it with `SYS_OPEN_NOFOLLOW`
  (`sys_open_rdonly()`), which uses `O_PATH|O_NOFOLLOW` to refuse following the
  final component and rejects the path if it is itself a symlink. Covered by
  `test_acl_set_symlink_rejected` and `test_acl_set_fd_success`.
- **Recurrence likelihood**: Medium — any new path-based metadata write on a
  shared directory repeats the race.

### Existence checks on possibly-symlinked paths must use lstat

- **Risk**: `io_exists()` uses `access()` and `stat()` follows symlinks,
  so a *dangling* symlink is reported "missing" and a *valid* one exposes
  the target's type/size. Code that then opens/creates the path with
  `O_CREAT` operates through the link (creating or failing on the target)
  instead of handling the link itself — this aborted runs with
  `cage: unable to create …/etc/resolv.conf` for a symlinked guest
  `/etc/resolv.conf`.
- **Prevention**: dispatch on one `lstat()` (`ENOENT` → missing;
  `!S_ISREG` → unlink the link + create; regular → content/size logic) so
  links are never followed, as `check_or_create_resolv()` does
  (src/sandbox/setup.c). Covered by
  `test_setup_resolv_dangling_symlink` / `test_setup_resolv_valid_symlink_replaced`
  and the `e2e/resolv.sh` symlink cases.
- **Recurrence likelihood**: Medium — the follow-symlink behavior of
  `access()`/`stat()` is silent and any new probe of a user-influenced
  path can repeat it.

### Pre-chroot appdir creation must walk from an O_NOFOLLOW root fd

- **Risk**: the GUI pre-chroot helpers created guest directories and touched
  files by absolute path (`io_mkdir_own`/`io_touch` + `mkparent`). The appdir is
  root-writable and its ancestors are caller-influenced, so a symlink planted at
  any component made the privileged `mkdir`/`chown`/`open` land outside the
  sandbox root.
- **Prevention**: `io_mkdir_own_at()`/`io_touch_at()`
  (`src/util/io_at.c`) take an `O_NOFOLLOW|O_DIRECTORY` fd on the sandbox root
  and walk the relative path component by component: each intermediate is
  `fstatat(AT_SYMLINK_NOFOLLOW)`-checked and must be a real directory before it
  is descended with `openat(O_NOFOLLOW|O_DIRECTORY)`. `src/sandbox/gui.c`
  (`mount_file`, `ensure_guest_home`, `prepare_cookie_dirs`) is rewired to these.
  Covered by `setup_gui_home_symlink_rejected`, `mkdir_own_at_rejects_symlink`,
  `touch_at_rejects_symlink` and `touch_at_rejects_symlink_leaf`.
- **Recurrence likelihood**: Medium — every new privileged create under a
  caller-influenced path must use the fd-relative helper, not a path string.

### Liveness probes must not trust kill(pid, 0) alone — zombies answer it

- **Risk**: `kill(pid, 0)` returns success for an unreaped zombie, so
  pidfile-based liveness (`pidexists`) reports a session as alive after its
  guest has exited and before the instance parent reaps it. Re-entry then
  joins a dying instance (skipping GUI mounts and socket ACLs by design) and
  can `setns` into a PID namespace about to vanish; a same-`-n` run during
  `-k`'s graceful-kill window re-enters a session being terminated.
- **Prevention**: after `kill(pid, 0)` succeeds, read the state field from
  `/proc/<pid>/stat` (the character after the **last** `)` — `comm` may
  contain spaces and parens); state `Z` means stale. This is what
  `pid_zombie()` does for `detect_existing()`/`killpid()` (src/core/proc.c).
  Any new liveness check that gates joining or skipping teardown must apply
  the same zombie filter, not `kill(pid, 0)` alone. Covered by
  `test_detect_existing_zombie` / `test_killpid_zombie_target`.
- **Recurrence likelihood**: Medium — `kill(pid, 0)` is the idiomatic
  "is it alive" probe and its zombie success is non-obvious; new supervisor
  or re-entry paths can reintroduce it.

### nf_tables netlink: batch protocol plus nested/be32 attributes

- **Risk**: the kernel's nfnetlink handler rejects a bare
  `NFT_MSG_NEWTABLE` with `EINVAL` — tables/chains/rules must be wrapped in
  `NFNL_MSG_BATCH_BEGIN` … `NFNL_MSG_BATCH_END` (`res_id =
  htons(NFNL_SUBSYS_NFTABLES)`). Inside a chain, `NFTA_CHAIN_HOOK` must carry
  `NLA_F_NESTED` and `NFTA_HOOK_HOOKNUM`/`NFTA_HOOK_PRIORITY` must be **be32**,
  or the kernel answers `EOPNOTSUPP`; rule expressions need `NLA_F_NESTED` on
  `NFTA_RULE_EXPRESSIONS`/`NFTA_LIST_ELEM` and an (empty) nested
  `NFTA_EXPR_DATA`. String attributes include the trailing NUL.
- **Prevention**: build all nftables changes through the single batch
  transport in src/net/net.c (`nft_batch_open`/`nft_batch_msg`/`nft_batch_flush`,
  with `nft_put_nest`/`nft_put_be32`) rather than emitting standalone
  netlink messages. Verify host state (`nft list ruleset`) after any change.
- **Recurrence likelihood**: Medium — the encoding rules are undocumented in
  the headers and a future "simple" NEW* message will silently regress to
  EINVAL/EOPNOTSUPP with no build-time warning.

### Netlink requests need NLM_F_ACK, and drain every reply

- **Risk**: a netlink write whose header lacks `NLM_F_ACK` produces no reply
  on success, so a blocking `recvmsg()` on the (single) reply hangs forever —
  `netlink_fill_addr`/`netlink_fill_up` omitted the flag. Conversely, one
  send can yield several replies (batch begin/end plus a per-message
  `NLMSG_ERROR`), so reading a single ack hides a later failure.
- **Prevention**: set `NLM_F_ACK` on every request whose success is awaited
  and drain until `EAGAIN` (`netlink_recv_nb` + `nft_drain_rest`), failing if
  any drained `NLMSG_ERROR` carries a negative errno. Covered by the
  `NLM_F_ACK` assertions in `test_fill_addr`/`test_fill_up`.
- **Recurrence likelihood**: Medium — "no ack expected" is the default mental
  model and the hang only manifests at runtime.

### Network byte order and hostmask at the rtnetlink attribute boundary

- **Risk**: `IFA_LOCAL`/`IFA_BROADCAST` payloads are 4-byte addresses in
  **network** order. Passing a host-order `uint32_t` yields addresses like
  `9.0.24.172`, and computing the broadcast from the **netmask** (`addr |
  netmask`) instead of the **hostmask** (`addr | ~netmask`) yields
  `255.255.255.1`-style values. Both were latent in `fill_addr_attrs`
  (src/net/rtnl.c).
- **Prevention**: keep `NET_IP4` macros host-order and convert with `htonl`
  exactly once at the boundary (`addr = htonl(local)`); derive the broadcast
  from the hostmask `(1u << (32 - prefix)) - 1`. Covered by
  `test_fill_addr_with_broadcast` (asserts `htonl(0xC0A80001)` and
  `htonl(0xC0A800FF)`).
- **Recurrence likelihood**: Medium — byte-order bugs are silent to the
  compiler and only visible in `ip addr` output.

### Repeated one-shot setup must be idempotent, not append

- **Risk**: `net_setup_nat()` (src/net/net.c) builds a `cage-nat` table, a
  `postrouting` chain and a masquerade rule on every `-N` run. The batch used
  `NFT_MSG_NEWRULE` with `NLM_F_APPEND` and no existence check, so each run
  appended another masquerade rule; the shared table is never torn down (it
  must outlive sessions for concurrent cages), so rules accumulated without
  bound.
- **Prevention**: create the whole thing as one atomic batch with `NLM_F_EXCL`
  on the table. When the table already exists the kernel aborts the batch with
  `EEXIST`, which `net_setup_nat()` treats as success (`r == 0 || r == -EEXIST`);
  neither the chain nor the rule is re-created. Covered by
  `test_net_setup_nat_eexist_is_ok` / `test_net_setup_nat_other_error_fails`
  and `e2e/network.sh`'s masquerade-count case.
- **Recurrence likelihood**: Medium — "run setup again" naturally reads as
  "append/refresh", and shared, long-lived resources make duplicates silent.

### "localhost" as a resolver name is a loopback resolver

- **Risk**: `line_is_localhost_ns()` (src/sandbox/setup.c) only matched
  `127.*`, `::1` and empty, so a `nameserver localhost` line looked like a
  *real* resolver. Under `-N` cage then kept the host bind mount
  (`nlink=1`) instead of the `8.8.8.8` tmpfs replacement (`nlink=0`),
  leaving the sandbox pointed at an unreachable loopback resolver.
- **Prevention**: classify the value with a single `val_is_localhost()` helper
  covering `127.*`, `::1`, the literal name `localhost` and empty/whitespace;
  lifecycle.md §Resolv.conf Strategy records the enumeration. Covered by
  `test_needs_default_dns` and `e2e/resolv.sh` case `localhost`.
- **Recurrence likelihood**: Medium — the name `localhost` is spelled
  differently from its loopback addresses and is easy to omit from the set.

### A new network namespace has no routes until you add them

- **Risk**: a fresh netns starts with only the loopback route. Assigning an
  address to `eth0` creates the *connected* route for its own `/31`, so the
  host peer became reachable, but nothing else was: there was no default route
  (`RTM_NEWROUTE`), so out-of-subnet traffic — and therefore NAT masquerade and
  the `8.8.8.8` resolv fallback — was unusable (`Network unreachable`). The
  spec described veth, addresses and NAT but no route, and no code created one.
- **Prevention**: after the interface is up, install a default route via the
  peer (`netlink_fill_default_route()` + `default_route_add()` in
  src/net/{rtnl,net}.c, called from `net_setup_child`). Covered by
  `test_fill_default_route`/`test_fill_default_route_buffer_too_small` and
  `e2e/network.sh` default-route and out-of-subnet probe cases.
- **Recurrence likelihood**: Medium — address + interface-up looks complete;
  the missing route only shows up as an out-of-subnet reachability failure.

### veth peers must sit in the same /31 block

- **Risk**: a `/31` link has exactly two usable addresses, `{2k, 2k+1}` per
  block `k`. `NET_HOST_IP(N)=2N+1` and `NET_CAGE_IP(N)=2N+2` straddled two
  blocks, so the host and sandbox ends were in different subnets and the peer
  was unreachable (`Network unreachable` from `ip route get`). The link came up
  but nothing crossed it.
- **Prevention**: derive both ends from the same block index — `NET_CAGE_IP(N)
  = 2N`, `NET_HOST_IP(N) = 2N+1` (src/net/net.h), leaving the next id at
  `2(N+1)`. Covered by `e2e/network.sh` (address assertions plus the sandbox →
  host-peer reachability case).
- **Recurrence likelihood**: Medium — the arithmetic looks innocuous and the
  failure only appears as a runtime reachability error.

### Veth IDs must reuse the lowest freed slot, not extend past the maximum

- **Risk**: `netlink_alloc_veth_id` (src/net/veth.c) scanned existing
  `cage-vethN` links and returned `max + 1`. When a middle session exited its
  veth disappeared, leaving a hole, but the next allocation still grew the
  maximum. With hosts `/31`-addressed per ID, freed addresses were never
  reused: after rp0 (id 0) and rp1 (id 1), killing rp0 made rp2 take id 2
  instead of id 0.
- **Prevention**: scan every existing ID into a `used[]` bitmap indexed by
  `CAGE_VETH_MAX_ID` and return the lowest zero bit
  (`netlink_lowest_free_veth_id`), failing only when all slots are used.
  Covered by `test_lowest_free_veth_id_crafted`,
  `test_lowest_free_veth_id_empty`, `test_lowest_free_veth_id_exhausted` and
  `e2e/network.sh`'s "reuse freed ID after parallel" case.
- **Recurrence likelihood**: Medium — "next after the maximum" is the natural
  shortcut for unique-ID allocation; holes only surface under churn.

### IPv6 forwarding: use force_forwarding, never all/forwarding on the host

`-N` needs IPv6 forwarding, but `/proc/sys/net/ipv6/conf/all/forwarding=1`
makes the kernel purge every RA-learned default route whose interface has
`accept_ra != 2` (`rt6_purge_dflt_routers`), so the host loses its SLAAC
default routes and IPv6 connectivity.

Use the per-interface `force_forwarding` knob instead: write
`conf/all/force_forwarding=1` for existing interfaces and
`conf/default/force_forwarding=1` for interfaces created later (the veth).
`force_forwarding` makes `ip6_forward` accept packets on that input interface
without flipping `cnf.forwarding`, so RA processing keeps working.

Common mistakes:
- Writing `conf/all/accept_ra=2` "to fix RA": `all/accept_ra` does not
  propagate to existing interfaces; RA routes are still purged.
- Writing `all/forwarding=1` and then trying to restore routes: the purge is
  immediate and the next unsolicited RA may be minutes away.
- Forgetting `default/force_forwarding`: the host side works, but the newly
  created `cage-vethN` has no forwarding and container traffic is dropped.

On kernels without `force_forwarding` (< 6.17), raise `accept_ra` to 2 on
every existing interface (individually; `all/accept_ra` does not propagate)
*before* writing `all/forwarding=1`. The purge skips `accept_ra == 2`, so RA
default routes survive. Abort the forwarding write if any per-interface raise
fails — a partial raise can still purge routes on the missed interface.

`e2e/network.sh` asserts `all/forwarding` stays 0 and both force_forwarding
knobs become 1 after a `-N` session (6.17+ path).

### Set-but-empty environment variables must be treated as unset

- **Risk**: `getenv()` returns `""` (non-NULL) for a variable that is set to
  the empty string, so `if (getenv("X"))` treats it as authoritative. For
  `XDG_RUNTIME_DIR=""` this resolved to an unusable host dir (no Wayland/Pulse
  socket mapped) instead of falling back to `/run/user/${SUDO_UID:-<uid>}`.
  The same class of bug applies to guest `-e` overrides, where `-e
  PULSE_SERVER=` / `-e XDG_RUNTIME_DIR=` must behave as if omitted.
- **Prevention**: guard with `x && x[0]` (or a shared helper) wherever an
  environment value can be empty but the intent is "empty means unset". This
  is what `get_host_xdg_dir()` (src/util/env.c) does, and what `find_envvar()`
  (src/core/prep.c, used for guest `-e` overrides) does. Covered by
  `test_gui_envs_xdg_empty_is_unset` and `test_find_envvar_empty_is_unset`.
- **Recurrence likelihood**: Medium — `if (getenv("X"))` is idiomatic and the
  empty-string case is easy to overlook.

### GUI setup runs before chroot: guest paths need the ctx->root prefix

- **Risk**: `-G` GUI setup runs *before* `move_and_chroot`, so a guest-absolute
  path (e.g. a custom `-e PULSE_SERVER=/run/…/gsock`) is **not** rooted yet.
  Operating on the raw guest path writes to the container-host filesystem
  outside the guest root (a root escape) and leaves the in-root `touch`/mount
  target missing, aborting with "GUI setup failed". This is what
  `prepare_custom_pulse_target()` did.
- **Prevention**: prefix `ctx->root` via `mem_path(ctx->root, guest_path)`
  before any pre-chroot `mkdir`/`open`/`mount` on a guest path — as
  `touch_target()` and `ensure_xdg_parent()` do. The same rule applies to any
  new pre-chroot filesystem operation on a user-supplied guest path. Covered
  by `test_setup_gui_custom_pulse_parent_under_root` and `e2e/pulse.sh` ps8/ps11.
- **Recurrence likelihood**: Medium — the pre-chroot stage silently accepts
  guest-absolute paths that resolve to a valid (but wrong) host location.

### Samefs loopback: the loopdir is policy, not a fixed constant

- **Risk**: the ext4 loopback used to hardcode `EXT4_MNT`/`EXT4_IMG`
  (`/var/lib/cage-loop[.img]`), so every samefs relocation landed there — a
  custom `-a` silently changed path (and its computed `-w`/upper followed the
  wrong parent). Reintroducing any fixed path for the loopback repeats the
  contract violation; likewise, overwriting a raw `-w` inside
  `handle_same_fs()` breaks the "custom paths are contracts" rule.
- **Prevention**: derive the loop root from `parent(computed_appdir)` via
  `derive_loopdir()` (append `-loop` unless the parent already ends with it;
  a single-element appdir is its own parent), keep the raw config `-U`/`-w`
  reachable (cfg passed into `handle_same_fs`) so `-U` is fatal on samefs and
  `-w` is fatal unless `path_under(loopdir)`. Covered by
  `test_derive_loopdir_*`/`test_handle_same_fs_*` and `e2e/appdir.sh`.
- **Recurrence likelihood**: High — the hardcoded constant is compact and
  "works" for the default appdir; the mis-relocation only shows up for
  custom `-a`/`-w`.

### A non-reaping PID1 turns every orphan into a permanent zombie

- **Risk**: processes observed by name (`pgrep`, `ps`) in a test namespace
  keep matching after death while unreaped. The e2e container used
  `sleep infinity` as PID1, which never calls `waitpid()`, so each suite's
  `e2e_sanitize` SIGKILLs and every exiting detached `setsid cage` left
  Z-state `cage`/`sleep` children of PID1 — later suites that count
  `pgrep -x sleep` saw inflated numbers (`expected [2], got [4]`) or false
  liveness (`missing [GONE] in [ALIVE]`).
- **Prevention**: PID1 must reap (run the container with `--init` so
  docker-init is PID1), and the supervisor process must not share a name
  with anything the tests count or sanitize kills (supervisor is
  `tail -f /dev/null`, never `sleep infinity`). When a suite counts
  processes by name, verify the count excludes the supervisor by *cmdline*,
  not by PID number. Covered by the full `e2e/_all.sh` run (24 suites,
  0 failures, 0 zombies) — see Past Incidents §e2e zombie pollution.
- **Recurrence likelihood**: Medium — a supervisor named after a counted
  process and a non-reaping PID1 both look harmless in isolation.

### indent is not idempotent for `, (cast)(expr)` argument lists

- **Risk**: GNU indent 2.2.13 flips a comma immediately followed by a cast of
  a parenthesized expression between `, (type)` and `,(type)` on every pass
  (`f(x, a, (size_t)(end - a))`). Any such construct makes `make format`
  non-idempotent, so `make format && git diff --exit-code` can never pass for
  either committed form — it fails, and re-running the formatter flips the
  line back. A cast of a simple identifier (`(size_t)len`) is stable; only
  `, (type)(expr)` oscillates.
- **Prevention**: do not pass `, (type)(expr)` as an argument — bind the cast
  to a local first (`size_t len = (size_t)(end - a);` then `f(..., len)`), as
  `append_tokens()` now does. `.github/scripts/build` additionally formats a
  copy of every file twice and fails with the offending file name if the two
  passes disagree, instead of reporting a phantom diff. See Past Incidents
  §non-idempotent indent formatting.
- **Recurrence likelihood**: Medium — the construct is natural to write and the
  resulting failure looks like an unrelated flaky format check.

### Samefs loopback must key on ancestry, not just the filesystem device

- **Risk**: the ext4-loopback relocation is needed only when the appdir lands
  *inside the lowerdir tree* and on the *same filesystem*. Comparing only
  `st_dev` (the old logic compared the lowerdir device with the appdir's
  grandparent) forces a loopback for two harmless layouts: (a) the appdir is a
  **sibling** of the lowerdir on the same filesystem (`-l /srv/lower -a
  /srv/app`), and (b) the appdir already sits on its **own mount** under the
  lower tree (a tmpfs mounted at `/srv/lower/app`). In both cases OverlayFS
  works without a loopback, so the relocation silently moves the user's appdir.
  The ancestry test alone is not enough either — an appdir that is *not* inside
  the lower tree but is itself on an overlayfs still needs a loopback, because
  OverlayFS cannot nest.
- **Prevention**: detection needs both an ancestry test
  (`path_is_parent(lowerdir, appdir_closest_existing)`) and the device
  comparison, plus the overlayfs check for the non-ancestor case; only
  same-fs *and* inside-the-tree, or an ordinary-fs sibling on overlayfs,
  triggers the loopback. See lifecycle.md §Samefs / Ext4 Loopback Flow rule 1.
  Covered by `test_same_fs_detect_lower_not_parent`,
  `test_same_fs_detect_lower_parent_same_fs` and the `e2e/appdir.sh`
  sibling/own-mount/empty-lower/full-lower cases.
- **Recurrence likelihood**: Medium — "same filesystem" reads as sufficient
  on its own, and the extra loopback still runs successfully, so the regression
  is a silent relocation rather than a failure.

### A green host `gcc` build does not cover the release toolchains

- **Risk**: the build CI compiles with the distro `gcc` (newer kernel UAPI
  headers and a newer GCC), while `release` cross-compiles with the bundled
  `<arch>-linux-musl-gcc` toolchains, which ship Linux 5.15 UAPI headers and a
  different GCC. Code that compiles cleanly on the host can therefore fail only
  in the release build: `MODULE_INIT_COMPRESSED_FILE` (Linux 5.17) was missing
  from the 5.15 `<linux/module.h>`, and `snprintf("/tmp/.X11-unix/%s", name)`
  into `char[64]` tripped the toolchains' `-Wformat-truncation`, which the host
  GCC did not raise at the default flags.
- **Prevention**: treat `make CC=<arch>-linux-musl-gcc` as the real compile
  check before a release. Version-gate newer UAPI constants with an `#ifndef`
  fallback (`#ifndef MODULE_INIT_COMPRESSED_FILE`), and size a `snprintf`
  destination for its worst-case output (`char path[PATH_MAX]`) plus a
  truncation check rather than a guessed small buffer. See Past Incidents
  §release cross-build.
- **Recurrence likelihood**: Medium — a new UAPI constant or a tighter
  `snprintf` buffer is easy to add and stays invisible to the host `gcc`.

### rpmbuild refuses a foreign `BuildArch`; cross-build with `--target`

- **Risk**: the release packaging step repackages a prebuilt binary into an RPM
  for every target arch on a single x86_64 runner. Setting `BuildArch:` to the
  *target* arch makes `rpmbuild` reject the build with `error: No compatible
  architectures found for build` whenever that arch differs from the host, which
  aborts the whole release. The compiled binary is fine, so the message sits at
  the tail of the build log and reads like a compiler or linker error (the i686
  case looked like the `-flto` link had failed).
- **Prevention**: do not pin the target with `BuildArch`. Pass the target to
  `rpmbuild --target "$RPM_ARCH"` so it cross-builds, and take the result from
  `RPMS/$RPM_ARCH/` — the package is named after `%_target_cpu`, not the host
  arch. Verified by building `x86_64`, `i686`, `armv7hl` and `aarch64` RPMs on an
  x86_64 host. See Past Incidents §release rpm cross-build.
- **Recurrence likelihood**: Medium — the spec looks correct and the failure
  only appears for a foreign arch, masquerading as a toolchain error.

### A path-interpolated CLI value must be a single path element

- **Risk**: an option value spliced into a filesystem path — `-n` into
  `/run/cage/${name}.pid` and `/var/lib/cage/app-${name}`, or `-v`'s container
  target joined onto the sandbox root by `mem_path` — lets `/` or `..` escape
  the base. Those paths are created and unlinked as root, so the escape happens
  with privilege.
- **Prevention**: reject a relative path with a `..` component at parse time —
  the shared rule lives in `mem_path_escapes()` (`src/util/mem.h`) so every
  caller enforces it identically. `-n` is validated even more strictly (empty
  or `/` rejected) because it is one path element, not a path. Document the
  constraint next to the option. Apply the rule to every future option whose
  value is concatenated into a path, and to any value read from a file that
  ends up in one.
- **Recurrence likelihood**: Medium — each consumer looks correct on its own
  and the defect only appears with a hostile argument.

### Guest paths derived from the environment are attacker input

- **Risk**: pre-chroot, as root, cage creates and chowns directories under the
  sandbox root from values the caller controls (`HOME`, `XDG_RUNTIME_DIR`,
  `PULSE_COOKIE`, `SUDO_HOME`). `-e HOME=../../root/.ssh` turned into a host
  directory outside the root, owned by the requested uid.
- **Prevention**: treat every guest-relative path as untrusted input: reject a
  `..` component with `mem_path_escapes()` before joining it onto the root —
  both where the path is consumed (`prepare_cookie_dirs`, `ensure_guest_home`)
  and at the entry point that takes the env-derived home (`setup_gui_mounts`).
  A lexical check is necessary but not sufficient on its own: a symlink inside
  a root-writable directory still escapes, so the `O_NOFOLLOW` root-fd work in
  the `src/util/io.c` TODO is the complementary half.
- **Recurrence likelihood**: Medium — the join looked safe because `/` is
  stripped, which does nothing about `..`.

### A transformation that grows the output needs a capacity, checked worst-case

- **Risk**: `build_subtree_value` padded each controller name with a space and a
  `+`, writing two bytes per input byte, but guarded with `len + 2 >= 256` —
  the size of the input, not the output. A 253-byte `cgroup.controllers` line
  wrote up to 508 bytes into the callers' `char val[256]`
  (`src/sandbox/cgroup.c`, `src/sandbox/cgroup_setup.c`), a stack overflow.
- **Prevention**: take the output capacity as a parameter and reject against the
  worst-case expanded length *before* writing: `2 * len + 2 > cap` (`1` leading
  `+`, up to `2 * len` body, `1` NUL). Pass `sizeof(val)` at each call site.
  Covered by `build_subtree_all_spaces_fits` (boundary accepted) and
  `build_subtree_overflow_rejected`.
- **Recurrence likelihood**: Medium — any expand-and-write helper must bound on
  the expanded size, and the guard is easy to write against the wrong length.

### Buffers returned to string consumers must be NUL-terminated

- **Risk**: `io_read` returned a length-delimited buffer with no terminator, yet
  callers scanned it as a C string — `has_real_ns_in_content(b.data)` and the
  `strchr`/`strstr` in `src/sandbox/setup.c`, `src/sandbox/cgroup.c`,
  `src/sandbox/cgroup_setup.c`. A file whose content filled the allocation (or
  any file read without a trailing NUL) made those scans read past `b->len` into
  uninitialized heap.
- **Prevention**: terminate in `read_file` — grow one byte and set
  `b->data[b->len] = '\0'`, so every `io_read` result is a valid C string.
  Callers that defensively grew-and-terminated themselves (`pid_zombie`,
  `env_read_pid_environ`) drop the now-redundant step. Covered by
  `read_small` (`strlen(b.data) == b.len`) and `read_empty`
  (`b.data[0] == '\0'`).
- **Recurrence likelihood**: Medium — a length-delimited buffer handed to string
  code is a silent out-of-bounds read.

### Never read errno after a successful call

- **Risk**: `scan_loop_devices` tested `sys_loop_get_stat(fd) == 0 || errno ==
  ENXIO` and then re-read `errno` to decide. `errno` is only meaningful after a
  call that failed, so a stale `ENXIO` left by an unrelated earlier call made a
  *busy* loop device look free — cage would then claim it and clobber another
  user's loop.
- **Prevention**: branch on the return value first and only then consult `errno`:
  `if (sys_loop_get_stat(fd) < 0 && errno == ENXIO)`. Covered by
  `scan_loop_devices_stale_errno`.
- **Recurrence likelihood**: Medium — `errno` is silently sticky and reads
  naturally after a call, successful or not.

### Decode a netlink reply only after validating its length

- **Risk**: `netlink_send_ack` cast the received buffer to `struct nlmsghdr`
  and read `nlmsg_type`/`nlmsgerr.error` with no length check, so a short or
  empty `recv` (a truncated or hostile reply) made it read uninitialized stack.
  The `nft` ACK drain decoded the same way (`nft_ack_errno` had no length).
- **Prevention**: reject `alen < NLMSG_HDRLEN` and require `NLMSG_OK`, and
  before reading the error require `nlmsg_len >= NLMSG_LENGTH(sizeof(struct
  nlmsgerr))`. `netlink_ack_result()` centralises this; `nft_ack_errno()` takes
  the received length. Covered by `ack_result_short`,
  `ack_result_truncated_error`, `ack_result_ok`, `ack_result_err`,
  `ack_result_non_error`.
- **Recurrence likelihood**: Medium — netlink replies are length-delimited and
  the header cast looks harmless.

### A header walker must bounds-check every optional field

- **Risk**: the gzip header walker advanced the offset past optional fields
  without confirming they fit: `skip_fextra` added `2 + xlen` unchecked, so a
  large `XLEN` pushed the offset past the buffer and the next header read ran out
  of bounds; `skip_string` did `(*o)++` even when the loop stopped at the end
  with no NUL, ending one past the buffer; and the FHCRC skip (`*o += 2`) had no
  check at all.
- **Prevention**: bound every advance against `in_len` through one `advance()`
  helper; `skip_string` only consumes the NUL when it actually found one
  (returns -1 otherwise). Covered by `decompress_gzip_extra_truncated`,
  `decompress_gzip_name_unterminated`, `decompress_gzip_hcrc_truncated`.
- **Recurrence likelihood**: Medium — every new optional field is another
  unchecked offset step.

### Shift counts from parsed input must be range-checked

- **Risk**: `fill_addr_attrs` computed the broadcast mask as `1u << (32 -
  prefix)`. Its guard only excluded `prefix >= 31`, so `prefix == 0` shifted by
  32 (undefined behaviour) and a negative prefix shifted by more than the width,
  both UB that a caller could reach through `-a`/`-A` parsing.
- **Prevention**: reject the whole unusable range up front — `prefix <= 0 ||
  prefix >= 31` skips the broadcast attribute. Covered by
  `fill_addr_zero_prefix` and `fill_addr_negative_prefix`.
- **Recurrence likelihood**: Medium — a mask or shift derived from a parsed
  prefix is easy to widen out of range.

### A wait that reports "no child" must not leave status unread

- **Risk**: `pidwait` returns `0` on `ECHILD` without writing its `status`
  out-parameter, yet callers declared `int status;` and decoded it
  (`exit_code_from_status(status)` in `src/core/cage.c`, `st == 0` in
  `src/core/prep.c`) or read it after a bare `waitpid` (`src/core/super.c`).
  An uninitialized stack read decided the exit code.
- **Prevention**: initialise the status to `0`, check the wait's return before
  decoding, and retry on `EINTR` (`pidwait` loops on `EINTR`; `super.c`'s
  `wait_exit_ok` does the same for the raw `waitpid`). Covered by
  `pidwait_echild` asserting `pidwait` leaves a sentinel status untouched.
- **Recurrence likelihood**: Medium — a wait helper that succeeds without an
  event is easy to decode blindly.

### A copy into a fixed buffer must reject truncation

- **Risk**: `resolve_script` validated `script` with `access()` but then copied
  it with `snprintf(buf, size, "%s", script)` and ignored the result. When
  `script` was longer than `size` the buffer held a different, truncated prefix
  while the function still reported success — callers then ran the wrong path.
- **Prevention**: check `snprintf` returns and reject `n < 0 || (size_t) n >=
  size`. Covered by `resolve_script_truncated`.
- **Recurrence likelihood**: Medium — `snprintf` truncation is silent and its
  return value is easy to discard.

### A builder that can fail must report the failure to its caller

- **Risk**: `appdir.c`'s `join()` returned silently when the joined path did not
  fit, and `set_dir()` had no way to observe it, so `resolve_appdir` returned 0
  with `upperdir`/`workdir` left as empty strings — a privileged overlay mount
  would then run with an empty upper/work dir.
- **Prevention**: `join()` returns -1 on overflow, `set_dir()` propagates it, and
  `resolve_appdir()` checks each call. Covered by `join_overflow_rejected`.
- **Recurrence likelihood**: Medium — a `void` builder that fails silently is
  easy to miss when the caller assumes a populated buffer.

### Guest paths copied with snprintf must reject truncation

- **Risk**: the XDG helper formatted `guest_xrd` and the socket name with
  `snprintf` into fixed buffers and ignored the result. A long `XDG_RUNTIME_DIR`
  (or Wayland name) was silently truncated and the prefix was then used as an
  mkdir/bind target — creating or mounting at the wrong guest path.
- **Prevention**: every copy is checked through a `fits(n, sz)` helper;
  `fill_xdg`, `ensure_wayland_target` and `pulse_target` fail (NULL/-1) when the
  formatted string would not fit. Covered by `setup_gui_xdg_truncated`.
- **Recurrence likelihood**: Medium — user-supplied env values copied into fixed
  buffers silently truncate.

### Do not append into a fixed buffer without a capacity check

- **Risk**: `nft.c`'s batch builder did `memcpy(b->data + b->len, msg, mlen)`
  into a fixed 768-byte `struct nft_batch` with no bound, so an oversized batch
  would smash the stack. The local request structs were also fixed-size but had
  no guard.
- **Prevention**: `nft_batch_msg` refuses a message that would not fit
  (`b->len + NLMSG_ALIGN(mlen) > sizeof(b->data)`), `nft_batch_end` refuses the
  trailer likewise, and the builders return `int` so `nft_build`/`nft_setup_family`
  fail the batch. The append bound is defensive: current static content is well
  under the buffer, so no unit test reaches the branch.
- **Recurrence likelihood**: Medium — appending to a fixed buffer without a bound
  is a recurring class.

### Attribute writers that take a buffer must take a capacity

- **Risk**: `netlink_nest_start` wrote an `nlattr` header at `buf + *off` with no
  capacity argument, unlike its sibling `netlink_put_string` which enforces
  `cap`. A nested veth message on a small buffer wrote past the end.
- **Prevention**: `netlink_nest_start` now takes `cap`, returns `-1` when the
  header does not fit and reports the start offset through an out-parameter;
  callers in `rtnl.c` propagate the error, and the raw `ifinfomsg` memset that
  followed it is now behind `put_ifinfomsg_zero`, which also checks `cap`.
  Covered by `fill_create_veth_nest_no_room`.
- **Recurrence likelihood**: Medium — asymmetric helpers where one sibling checks
  `cap` and another does not.

### Kernel-provided files must be opened, not created

- **Risk**: `write_pid` opened `cgroup.procs` with `O_WRONLY | O_CREAT, 0644`,
  so a missing file — wrong path, controller without a procs file, mock root in
  a test — was created as an ordinary regular file and the write "succeeded"
  without the process ever entering a cgroup. The write itself was an unchecked
  `dprintf`, so a short or failed write also reported success.
- **Prevention**: open kernel-managed files (`cgroup.procs`,
  `cgroup.subtree_control`, pidfiles locked elsewhere) with `O_WRONLY` only and
  let the open fail when the object is absent; format into a stack buffer and
  check both `snprintf` and `write` before returning 0. Covered by
  `write_pid_missing_rejected` and `write_pid_success` in `tests/test_cgroup.c`,
  whose fixtures seed `cgroup.procs` instead of relying on creation.
- **Recurrence likelihood**: Medium — "open for writing" naturally reaches for
  `O_CREAT`, and the unchecked-write habit recurs with every new file writer.

### `fsetxattr` fails on `O_PATH` fds; sockets can only be opened `O_PATH`

- **Risk**: `acl_set_fd` opened the target with `O_PATH|O_NOFOLLOW` (race-free
  identity, symlinks refused) and then called `fsetxattr`. The kernel rejects
  `fsetxattr` on an `O_PATH` fd with `EBADF`, and a unix socket cannot be
  opened any other way (`open(..., O_RDONLY)` returns `ENXIO`), so the ACL was
  never applied whenever it was actually needed. X11 sockets owned by root or
  another user made GUI setup fail with `cage: GUI setup failed`.
- **Prevention**: keep the `O_PATH` open and set the xattr through the magic
  link `/proc/self/fd/<fd>` (`sys_setxattr`), which resolves to the pinned
  inode and cannot be redirected by swapping the path. Verified by
  `e2e/gui_x11.sh` g4 (other-owner socket) and g6 (root-owned socket
  discovered from `/tmp/.X11-unix`).
- **Recurrence likelihood**: Medium — fd-based xattr hooks look usable but
  `O_PATH` has a narrow syscall allowlist.

### A command user created by `--initscript` must be re-resolved afterwards

- **Risk**: the container child resolved the guest `user_t` before running the
  init script, then used the same instance for the command. A user or group
  that the init script created (`adduser`/`addgroup`) was invisible, so `-u
  carol` silently fell back to `nobody` and `-g gonly`/`<gid>:<name>` fell
  back to root.
- **Prevention**: after `do_init`, create a fresh guest `user_t` rooted at `/`
  and eagerly resolve it (`reread_guest_user`) before `child_run`; the GUI
  context still uses the pre-init user because it is built before the chroot.
  Covered by `e2e/identity.sh` n4 (user + complementary groups) and n5/n6
  (named primary/complementary groups).
- **Recurrence likelihood**: Medium — any reordering of setup steps silently
  reintroduces the stale cache.

### Guest HOME parents are root-owned; only the leaf belongs to the guest

- **Risk**: creating a nested guest HOME (`-e HOME=/a/b/c`) through
  `io_mkdir_own_at` chowned every created component to the guest uid, so the
  guest could rename or replace its own ancestors. The fd-relative refactor
  replaced the old `mkparent` (parents `0:0`, leaf `uid:gid`) and lost this.
- **Prevention**: `io_mkdir_parents_at` creates intermediate components `0:0
  0755` and only the leaf `uid:gid 0755`; `ensure_guest_home` and
  `ensure_mount_target` use it, while `prepare_cookie_dirs` keeps
  `io_mkdir_own_at` because `.config`/`.config/pulse` genuinely belong to the
  guest. Covered by `mkdir_parents_at_chowns_only_leaf` and `e2e/pulse.sh`
  pc12.
- **Recurrence likelihood**: Medium — two mkdir helpers with different
  ownership rules invite a swap.

### Discarded teardown return reports a failed kill as success

- **Risk**: `killpid` called `kill_gracefully(pid)` but discarded its return, so
  a failed `SIGTERM` (`EPERM`, `EINVAL`) or a failed wait after an already
  removed pidfile still reported success to the CLI — the caller believed the
  sandbox was gone while its process survived.
- **Prevention**: propagate the result and treat only `ESRCH` as success; send
  the signal through `sys_kill` so the failure path can be mocked. Covered by
  `test_killpid_kill_fails` in `tests/test_proc.c`.
- **Recurrence likelihood**: Medium — teardown helpers whose errors look
  harmless are the ones most often called without checking the return.

### Shared-memory handshakes must not wait forever on a dead child

- **Risk**: the `-N` parent waited on the child's "net-ready" futex with a
  plain `sys_futex_wait`, which has no timeout. A child that exited before
  `do_child_net` (unshare, mount, or init-script failure) never woke it, so the
  parent hung permanently instead of reporting the failure. The temp-root
  handshake had already been converted to a timed loop; the net handshake had
  not.
- **Prevention**: `wait_flag` is the single timed wait (20 ms) used by both
  handshakes; each iteration peeks at the child with
  `waitid(WEXITED | WNOHANG | WNOWAIT)` and returns on child death. See
  docs/lifecycle.md §Network Namespace Setup rule 8.
- **Recurrence likelihood**: Medium — newly added cross-process futex
  handshakes tend to start as a bare wait.

### Free everything allocated before an early-return setup path

- **Risk**: `spawn_init` returned straight to the caller when
  `create_pidfile` failed, after `do_early_setup` had already allocated
  `free_pidfile`; `spawn_cage` had the same leak on its failure return. The
  failure path is rare, so the leak accumulates only across repeated failed
  launches, making it easy to miss.
- **Prevention**: `spawn_st_init` zeroes every owned pointer, `spawn_init`
  frees `free_pidfile` (and NULLs it) before its bare `return -1`, and
  `spawn_cage` frees it again defensively (NULL-safe). Do not route these
  paths through `spawn_cleanup`, which would unlink a pidfile that may belong
  to another instance.
- **Recurrence likelihood**: Medium — each new early return between an
  allocation and the normal cleanup block reopens the leak.

### Check `snprintf` when building a child path for recursive deletion

- **Risk**: `io_rmtree` formatted `"%s/%s"` into a fixed 4096-byte buffer
  without checking the result. A truncated child path points at a different
  entry (or a prefix that happens to exist), so recursive deletion could
  remove the wrong subtree instead of failing on an over-long name.
- **Prevention**: `rmtree_dir` checks `snprintf` and reports `path too long`,
  skips that entry, and returns -1; `io_rmtree` propagates the failure without
  attempting `rmdir` on a directory that still has children. Recursion also
  propagates per-child failures instead of silently continuing.
- **Recurrence likelihood**: Medium — every path-joining `snprintf` in a
  recursive walk is a candidate for the same unchecked pattern.

### A failed ACL grant on a bind-mounted socket must fail the GUI setup

- **Risk**: `mount_file_acl` discarded the `acl_guest_set` result, so a failed
  POSIX ACL grant on a bind-mounted X11/Pulse socket left the guest unable to
  connect while the run continued as if GUI setup had succeeded — the failure
  surfaced later as a confusing connection error inside the sandbox.
- **Prevention**: check and return the ACL result (the caller prints
  `cage: GUI setup failed`), and log the offending target. Covered by
  `mount_file_acl_propagates_failure` in `tests/test_gui.c`, which mocks the
  socket open/stat and forces `sys_setxattr` to fail.
- **Recurrence likelihood**: Medium — ACL and chown calls are easy to treat as
  best-effort even when the guest needs their effect.

### Build persistent artifacts aside and rename; validate before reuse

- **Risk**: `setup_ext4_img` ignored `EEXIST`, so an image left behind by an
  interrupted run (created with `O_CREAT|O_EXCL` and written in place) was
  silently reused even though it was truncated or half-initialised. Mounting
  it fails, and the failure is attributed to the loopback machinery rather
  than to the stale artifact.
- **Prevention**: build into `${img}.tmp.<pid>` and `rename()` over the target
  so the final name only ever refers to a complete image; validate an existing
  image with `ext4_image_valid()` (exact size plus `0xEF53` superblock magic)
  before reusing it, and rebuild when validation fails. Covered by
  `test_ext4_image_valid`.
- **Recurrence likelihood**: Medium — in-place artifact creation plus
  `EEXIST`-as-success is a common resume-from-crash pattern.

### E2E: wait for observable state, not a fixed sleep

- **Risk**: `e2e/same_fs.sh` slept a fixed 2s before asserting the ext4
  loopback log line, the loop mount and the relocated appdir. Under load cage
  had not reached the same-fs path yet, so all three checks failed with an
  empty log even though cage was healthy and completed moments later.
- **Prevention**: poll for a state the sandbox produces
  (`test -d /var/lib/cage-loop/app-d1`) with a bounded 10s timeout before
  asserting; the log and mount checks follow and report the real state. When a
  test unmounts a fixture tmpfs, `umount` in a loop until the path is gone and
  fail fast if it cannot be unmounted, so a leaked stacked mount cannot
  silently disable the case. See Past Incidents §same_fs flake.
- **Recurrence likelihood**: Medium — a fixed `sleep` is the default way to
  wait for asynchronous setup and looks correct on an idle machine.

### Bound reads of container-rootfs-controlled files

- **Risk**: `io_read()` grows its buffer until EOF, so a huge
  `/etc/resolv.conf` inside a container rootfs drives `buf_grow()` until
  `mem_alloc()` aborts the child, giving the rootfs owner a memory-exhaustion
  lever during setup.
- **Prevention**: `io_read_cap(path, buf, cap)` reads at most `cap + 1` bytes
  and fails once the limit is crossed. Resolv reads use a 64 KiB cap: an
  oversized host file is treated as having no real DNS (default DNS is used),
  while an oversized guest file fails setup closed. Covered by
  `read_cap_exact`/`read_cap_exceeded` and
  `setup_resolv_oversized_rejected`.
- **Recurrence likelihood**: Medium — `io_read()` is the convenient default,
  and container-rootfs files are easily mistaken for trusted configuration.

### Do not use vfork for the sudo re-exec child

- **Risk**: `try_sudo_reexec()` used `vfork()` to run `sudo --fix`. When sudo
  was absent, the child fell back to `_exit(127)` after `execlp()`; on resume
  the parent segfaulted in `_start_c` instead of reporting the expected
  `cage: sudo --fix failed` error. `e2e/unprivileged.sh` caught rc=139 where
  the test expects rc=1 and the word `sudo` on stderr.
- **Prevention**: use `fork()` (`sys_fork`) for this repair-only child. The
  path runs at most once after a broken install, so copying page tables is
  irrelevant; correctness and a clean error path matter more. Covered by
  `e2e/unprivileged.sh` cases "no setuid and no sudo" and "with passwordless
  sudo".
- **Recurrence likelihood**: Medium — `vfork` is the reflexive choice before
  `exec`, and unit mocks skip the real child-exec failure path.

## Code Quality Exceptions

*All functions meet lizard thresholds. No exceptions.*

## Past Incidents

Recorded incidents, their root causes, and prevention actions are split
across three files to keep each document under 60 KB:

- [past-incidents.md](past-incidents.md)
- [past-incidents-2.md](past-incidents-2.md)
- [past-incidents-3.md](past-incidents-3.md)

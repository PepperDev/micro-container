# Past Incidents

### close() on pidfd skips flock(LOCK_UN), losing lock for concurrent readers

- **Bug**: `spawn_exec_fail` in `cage.c` called `close(st->pidfd)` instead of `close_pid(st->pidfd)`. `close_pid` releases the pidfile's `flock(LOCK_UN)` before closing the fd. Using bare `close()` skipped the unlock — the kernel reclaims the lock on fd close, but the explicit unlock is required for correctness (documents intent, avoids races with lock reacquisition).
- **Root cause**: Only one of three close-pidfd sites used `close()` — the other two (`finish_child`, `spawn_cleanup`) used `close_pid()` correctly. The `spawn_exec_fail` path was likely overlooked when `close_pid` was introduced.
- **Prevention**: Changed `close(st->pidfd)` to `close_pid(st->pidfd)` in `spawn_exec_fail`. Added §pidfd close must release flock via close_pid, not close to Domain-Specific Pitfalls.

### Primary gid missing from supplementary groups in privilege drop

- **Bug**: When supplementary groups were given via `-g` (`-u 1000 -g 500:999`),
  `ensure_groups` returned early because `u->groups` was non-NULL, without
  checking whether the primary gid (500) was present. The `setgroups()` call
  in `do_priv_drop` received `{999}` — the primary group 500 was missing.
- **Root cause**: `ensure_groups()` used `if (u->groups && u->groups_count != 0) return;`
  as a guard, which returned immediately for any non-empty groups list without
  validating that `u->gid` was included.
- **Prevention**: Rewrote `ensure_groups` to always include `u->gid` in the
  groups list — it checks for the gid and prepends it if missing. Tests updated
  to expect the primary gid in supplementary groups. Added §user privilege drop:
  primary gid missing from supplementary groups to Domain-Specific Pitfalls.

### modprobe_load return value ignored

- **Bug**: `finit_module` fails with EOPNOTSUPP inside a container; `modprobe_load` returns -1 but callers ignore it. The `loop` module failure then cascades into "max devices exhausted", `overlay` failure causes `io_samefs` crash on missing `/var/lib/cage`, and `veth` failure is silently skipped.
- **Root cause**: `modprobe_load` return value unchecked in `find_free_loop()`, `do_root_prep()`, and `setup_net_links()`.
- **Prevention**: Added §modprobe: unchecked return values to Domain-Specific Pitfalls. Added return-value checks to all `modprobe_load` callers.

### Double rmdir: parent + child both clean the same tmpdir

- **Bug**: After `fork_cleanup` created a cleanup child, 3 parent error paths
  (`spawn_cleanup`, `spawn_exec` fail path, `finish_child` fail path) called
  `io_rmdir(root)`. The cleanup child had already removed the tmpdir, so the
  parent got ENOENT on every second call. Traced via strace double-rmdir ENOENT.
- **Root cause**: Redundant cleanup — the parent didn't know the cleanup child
  had already handled tmpdir removal.
- **Prevention**: Added §fork cleanup: parent + child both cleaning the same
  tmpdir to Domain-Specific Pitfalls. Removed all 3 redundant `io_rmdir` calls.

### Empty pidfile: open before fork creates waste window

- **Bug**: `create_pidfile` opened and locked `/run/cage/pid` before `pidfork()`.
  The PID was written only later in `finish_child` → `writepid`. If fork failed,
  the empty pidfile was unlinked without ever being written. An empty-pidfile
  window existed between open and the subsequent unlink.
- **Root cause**: Creating an externally-visible resource before knowing the
  operation that needs it will succeed.
- **Prevention**: Added §pidfile / lockfile: creating a resource before knowing
  the operation succeeds to Domain-Specific Pitfalls. Moved `create_pidfile` to
  `spawn_init`, after `do_root_prep` has already forked the child. The PID is
  written immediately after `open`+`flock` — no empty-pidfile window.

### Pidfile contains parent PID instead of child PID

- **Bug**: `create_pidfile` wrote `getpid()` (parent PID) to the pidfile. The
  child process runs in a new PID namespace (after `unshare(CLONE_NEWPID)`)
  and is the actual container application. The pidfile should contain the
  child PID.
- **Root cause**: `create_pidfile` didn't know the child PID (it was called
  before `pidfork()`). The PID write used `getpid()` as a placeholder.
- **Prevention**: `create_pidfile` now only opens+locks; the PID is written
  in `spawn_init` after `do_root_prep` has forked the child. Added
  §pidfile / lockfile: PID must be the child PID, not getpid().

### Root tmpdir cleanup happens at command exit instead of after overlay mount

- **Bug**: `fork_cleanup` waited for a signal that was only sent in
  `finish_child` (at command exit). The root tmpdir remained alive in the
  original mount namespace the entire time, though it was empty from that
  namespace's perspective.
- **Root cause**: Cleanup signal was deferred to `finish_child` instead of
  being sent immediately after overlay mounts completed.
- **Prevention**: Added a pipe-based signaling mechanism in `do_root_prep`.
  The mount child writes to the pipe after mounts succeed; the parent reads
  the pipe and immediately signals the cleanup child. The cleanup child uses
  `io_rmtree` instead of `io_rmdir` for safety.

### Pidfile opened twice on re-entry path

- **Bug**: `spawn_existing` called `read_pidfile()` which opened and read
  `/run/cage/pid` a second time, even though `detect_existing()` had already
  read and verified the PID in `do_spawn()`.
- **Root cause**: Code duplication — `spawn_existing` didn't use the PID
  already verified by `detect_existing`.
- **Prevention**: Removed `read_pidfile()`. Changed `spawn_existing` to
  accept `old_pid` as a parameter, passed from `do_spawn()`. No more
  duplicate open+read.

### io_mkdir(etc) + stat(resolv.conf) syscall discipline violations + create_resolv_file return value

- **Bug**: Three violations of §Filesystem Syscall Discipline plus one
  refactoring bug:
  - `setup_resolv` called `io_mkdir(root/etc)` unconditionally (Rule 4).
  - `check_or_create_resolv` called `stat()` on a path that may not exist
    (Rule 2).
  - Refactoring: `create_resolv_file` returned `io_touch(resolv)` directly
    (0 on success) instead of 1, causing `setup_resolv` to skip
    `do_resolv_replace` — all SYS_MOUNT calls were silently elided.
  - A separate attempt to replace `io_mkdir(dev/shm)` with
    `ensure_dir_mkdir` (access(F_OK)+raw mkdir) was reverted because
    `io_mkdir`'s `mkdir_parent` fallback is needed when `root/dev/`
    doesn't exist (mount_misc called without prior mount_core).
- **Root cause**: Extracting inline code into a helper changed return value
  semantics (1 → 0) without updating callers. The `ensure_dir_mkdir`
  approach lacked `mkdir_parent` fallback.
- **Prevention**: Fixed all applicable issues:
  - Removed `io_mkdir(etc)`, delegate to `check_or_create_resolv`.
  - Added `io_exists()` (access F_OK) before `stat()` in
    `check_or_create_resolv`.
  - Fixed `create_resolv_file` to return `io_touch(resolv) != 0 ? -1 : 1`.
  - Reverted `ensure_dir_mkdir` to `io_mkdir` (parent creation needed).
  - Rules are documented in §Filesystem Syscall Discipline and added §Helper
    extraction: preserve return value semantics to Domain-Specific Pitfalls.

### chown("/root", 0, 0) without checking if already correct

- **Bug**: `ensure_dir` in `launch.c` called `io_chown(path, uid, gid)`
  unconditionally after ensuring the path exists. For the common case
  (`/root` owned by root), this performed a metadata write when a
  metadata read would suffice.
- **Root cause**: Optimization opportunity missed — the cost of `chown`
  (metadata write) is higher than `stat` (metadata read).
- **Prevention**: Added a `stat()` before `chown`; skip the `chown` when
  uid/gid already match.

### fork ENOMEM after unshare(CLONE_NEWPID)

- **Bug**: `unshare(CLONE_NEWNS|CLONE_NEWPID)` was called in `prepare_mounts`
  (parent process). Then `pidfork()` created a mount child that became PID 1
  in the new namespace. When the mount child exited, a subsequent `pidfork()`
  for the app child returned ENOMEM (man fork: "init process terminated").
- **Root cause** (v1): The mount child (PID 1 in new namespace) exited, which
  prevents further forks in that namespace.
- **Wrong fix** (v1): Moved `ns_setup` (unshare) into the child after fork.
  This broke PID 1 semantics: `man 2 unshare` says the calling process is
  NOT moved into the new namespace — the **first child** becomes PID 1.
  After fork + unshare, the next fork (e.g. `ls`) became PID 1 instead of
  the shell. When `ls` (init) exited, further forks got ENOMEM.
- **Correct fix** (v2): `ns_setup` stays in `prepare_mounts` (parent side,
  BEFORE the fork). Parent calls `unshare(CLONE_NEWNS|CLONE_NEWPID)`, stays
  in original namespace, then `run_mount_child` forks — child becomes PID 1
  in the new namespace. Child does mounts and continues as the application
  (never exits early). Single-fork pattern is preserved.

### create_resolv_file returns 0 instead of 1, silently skipping mount calls

- **Bug**: `create_resolv_file` returned `io_touch(resolv)` directly, which
  returns 0 on success. The caller (`check_or_create_resolv` → `setup_resolv`)
  interpreted 0 as "no DNS replacement needed", skipping `do_resolv_replace`
  entirely. All 6 mount-related tests in `test_setup.c` failed because
  `setup_resolv` returned early without calling SYS_MOUNT.
- **Root cause**: When extracting inline resolv-creation code into
  `create_resolv_file()`, the return value changed from 1 (original inline
  code: `io_touch(...); return 1;`) to 0 (`return io_touch(resolv)`).
- **Prevention**: Added §Helper extraction: preserve return value semantics
  to Domain-Specific Pitfalls. Fixed the return to
  `io_touch(resolv) != 0 ? -1 : 1`.

### Loop mount message prints on every same-fs detection

- **Bug**: `same_fs_detect` printed "using ext4 loopback" every time
  `handle_same_fs` detected same-fs, even when the loop was already
  provisioned. The message appeared on every container start, not just
  during initial loop setup.
- **Root cause**: The `fprintf` was in `same_fs_detect`, which runs
  before `loopback_is_provisioned` in `handle_same_fs`.
- **Prevention**: Moved the `fprintf` from `same_fs_detect` to
  `setup_ext4_fix`, which is only called during initial setup, not
  when the loop is already provisioned.

### Mount ordering: cleanup child rmdir removes overlay dentry before child chroots

The cleanup child (forked before `unshare`, in the original mount namespace) does
`rmdir` of the temp root. This removes the dentry from the shared VFS dcache.
Even with chroot before signal, the unlinked mount point dentry causes the
kernel's `mount(2)` `path_mountpoint()` traversal for proc/sysfs to return
ENOENT — the dentry is detached from its parent in the dcache and cannot be
used as a mount target path.

The fix uses `MS_MOVE` to detach the overlay mount from the tmpdir BEFORE
signaling the cleanup child: `chdir(root)` → `mount(root, "/", NULL, MS_MOVE,
NULL)` → `chroot(".")`. After MS_MOVE, the overlay is at `/` and the tmpdir is
a regular empty directory. The cleanup child's `rmdir` just removes an empty
dir — no mount point dentry involved.

Common mistakes:
- Removing the tmpdir while the overlay mount is still attached there —
  `mount(2)` path resolution fails even with `chroot` pinning
- Signaling the cleanup child before `MS_MOVE` — rmdir unlinks the mount point
  dentry, causing ENOENT in subsequent `mount()` calls
- Mounting proc/sysfs before chroot — absolute paths resolve to host mounts,
  causing EBUSY on mount over existing sysfs
- Having the parent and child each do partial chroot logic

When modifying prepare_mounts or run_mount_inner, check:
- Is MS_MOVE called between chdir and chroot? (Detaches overlay from tmpdir)
- Is signal_cleanup called AFTER MS_MOVE? (Cleanup child rmdir must see only
  an empty dir, not a mount point)
- Are do_init and child_setup_gui called BEFORE MS_MOVE? (They need absolute
  tmpdir paths for bind mount targets)
- Does the child call chroot again? (It shouldn't — inherits from parent)
- **Is `ns_setup` called BEFORE `setup_overlay`?** If the overlay is mounted
  in the old namespace, the cleanup worker (forked before unshare) sees the
  overlay and `io_rmtree` recursively traverses the entire overlay tree
  including all bind-mounted user data — thousands of wasted syscalls.

Correct pattern:
```c
// parent: ns_setup → overlay → host volumes + init + gui → chdir → MS_MOVE → chroot → signal → fork
// child:  mount_remaining → net → child_run (no chroot, inherited from parent)
```

### cfg->group default "0" overrides fixup_primary_group

- **Bug**: `cage_config_init` set `cfg->group = "0"`. This caused
  `get_primary_group` to return `prim_str = "0"` (non-NULL), which
  bypassed `fixup_primary_group`'s gid=uid logic — the process
  retained gid=0(root) despite `-u 1000`. Additionally, group "0"
  was parsed as a supplementary group, leaking root into the
  supplementary groups list.
- **Root cause**: Config struct string pointer initialized to `"0"`
  instead of `NULL`, making `parse_user` believe a group was
  explicitly provided.
- **Prevention**: Changed default to `NULL`. Added §Config struct
  initialization: string pointer defaults to Domain-Specific Pitfalls.

### detect_cgroup2 reads /proc/filesystems after chroot but before mount_virtual

- **Bug**: `detect_cgroup2()` at `mount.c:273` ran unconditionally after
  `move_and_chroot()` but before `mount_remaining()` → `mount_virtual()` mounted
  `/proc`. Since `/proc/filesystems` didn't exist in the new root, `io_read` failed
  with ENOENT, printing "cage: unable to open /proc/filesystems" on every cage run
  regardless of `-C` flag. When `-C` was false, the result was never used. When `-C`
  was true, `has_fstype("cgroup2")` in `mount_cgroups()` duplicated the read.
- **Root cause**: cgroup version was probed three separate times via `/proc/filesystems`:
  (a) host-side in `ensure_cgroup2_root` (correct), (b) after chroot in `detect_cgroup2`
  (broken — no /proc yet), (c) after mount_virtual in `has_fstype` (redundant).
  Only (a) was needed.
- **Prevention**: Added `cgroup_version_t` enum cache (`cgroup_version.c:254-262`), set
  during the first probe in `ensure_cgroup2_root`, read via `cgroup_version()` getter.
  Removed `detect_cgroup2()`, replaced with cache lookup guarded by `cfg->unshare_cgroup`.
  Replaced `has_fstype` calls in `mount_cgroups` with cache lookup. `/proc/filesystems`
  is now read at most once, always on the host side before any namespace operations.
- **Recurrence likelihood**: Low — no more direct `/proc/filesystems` reads for cgroup
  detection; all consumers use the cache.

### get_primary_group takes everything after first colon

- **Bug**: `get_primary_group` used `strlen` from the first colon
  onward, capturing the entire remainder as the primary group string.
  For `-u 1001:1001:1002`, it set `prim_str = "1001:1002"` which
  `resolve_primary_group` tried to look up as a literal group name
  and failed.
- **Root cause**: The function didn't handle multiple colons — it
  assumed everything after the first colon was the primary group.
- **Prevention**: `get_primary_group` now finds the next colon to
  limit `prim_len` and returns remaining text as `user_supp` for
  supplementary group parsing. Added test `user_colon_with_supplementary`
  covering the multi-colon format.

### setns() in re-entry always passes nstype=0

- **Bug**: `join_ns` called `SYS_SETNS(fd, 0)` unconditionally instead of
  passing the matching `CLONE_NEW*` flag (e.g. `CLONE_NEWPID` for "pid").
  Per `man 2 setns`, passing 0 is valid but may cause ambiguity in corner
  cases; the recommended practice is to always pass the explicit flag.
- **Root cause**: The namespace type string ("pid", "cgroup", etc.) was
  used only for the path but ignored in the `SYS_SETNS` call.
- **Prevention**: Added `ns_flag()` helper mapping namespace name to
  `CLONE_NEW*` constant. Updated mock to track the nstype argument.

### mock bypass in same_fs_detect causes existing test to fail

- **Bug**: `test_same_fs_detect_io_exists_fails` expected `same_fs_detect` to
  return -1 (path was `chmod 0000`, causing `io_samefs` to fail), but got 0
  because `_mock_same_fs_ret` defaulted to 0 (bypass) and the function returned
  immediately without executing real code.
- **Root cause**: Adding `_mock_same_fs_ret` mock bypass to `same_fs_detect`
  without updating the existing test to disable the bypass via
  `_mock_same_fs_ret = -1`. The test silently became a mock test.
- **Prevention**: Added `_mock_same_fs_ret = -1` before the call and
  `_mock_same_fs_ret = 0` after in `test_same_fs_detect_io_exists_fails`.
  Added §Mock bypass: existing tests silently become mock tests to
  Domain-Specific Pitfalls.

### Re-entry child skips chroot, runs in host root

- **Bug**: `reuse_child` in `spawn_existing` joined namespaces via `setns`
  but did not `chdir`/`chroot` into the container root. After
  `setns(CLONE_NEWNS)`, the process inherits the container's mount table
  but retains the host path as "/" (because `chroot` is per-process, not
  namespace-scoped). The fresh spawn path (`child_run` in `prep.c`) did
  `chdir(root) + chroot(".")`, but `reuse_child` skipped it entirely.
  The command ran in the host root instead of the container root.
- **Root cause**: `reuse_child` was modeled after the reference code's
  `changeroot_pid` which also did not chroot, but the current code's fresh
  spawn path always chroots. The re-entry path was missing this step.
- **Prevention**: `spawn_existing` now passes `"/proc/<old_pid>/root"`
  to `reuse_child`, which performs `chdir(path) + chroot(".") +
  chdir("/")` — the same sequence as `child_run`. The kernel's magic
  symlink `/proc/pid/root` is followed directly by `chdir()`, landing
  in the target process's root without needing `readlink`.
- **Recurrence likelihood**: Low — both fresh and re-entry paths now
  follow the same explicit chroot pattern.

### Pidfile opened after chroot, resolves in overlay instead of host filesystem

- **Bug**: `spawn_init` called `create_pidfile(st->pidfile)` AFTER `do_setup` which
  performed `chdir`+`MS_MOVE`+`chroot`. The pidfile path `/run/cage/pid` resolved
  within the overlay, creating the pidfile in the overlay's upper layer instead of
  the host filesystem. `detect_existing` (called before chroot) always looked on the
  host, so re-entry detection was silently broken — no existing-instance check ever
  found the pidfile.
- **Root cause**: Ordering — `create_pidfile` was called after chroot instead of
  before. The fd survives chroot, so opening the fd before chroot and writing via
  the fd afterward is safe.
- **Prevention**: Swapped the order: `create_pidfile` now runs before `do_setup`
  (before chroot). The fp survives all namespace operations. The PID is written
  via `dprintf(fd, ...)` after `do_setup` returns with the child PID.
- **Recurrence likelihood**: Low — the fix is a simple ordering correction.

### create_guest_xdg_dir raw mkdir violates access-first + missing recursive chown in parents

- **Bug**: `create_guest_xdg_dir` in `gui.c:265-282` used raw `mkdir(g, 0700)` with EEXIST fallback, violating Syscall Discipline Rule 4 (access before mkdir). When `-e XDG_RUNTIME_DIR=/custom/path` was passed and `/custom/` didn't exist, `mkdir` failed with ENOENT ("cage: unable to mkdir"). Additionally, `mkparent()` at `gui.c:22-37` called `io_mkdir(parent)` (which creates grandparents recursively) but only chowned the immediate parent, leaving grandparent directories owned by root instead of the guest uid:gid.
- **Root cause**: No recursive-mkdir-with-chown helper existed. Code used raw mkdir + EEXIST fallback for `create_guest_xdg_dir`, and `mkparent` relied on `io_mkdir`'s recursive parent creation without propagating chown to grandparents.
- **Prevention**: Added `io_mkdir_own(path, uid, gid, mode)` in `io.c`: walks path components, `access(F_OK)`-checks each (Rule 4), `mkdir`s missing components, and `chown`s each newly-created component to the given uid:gid. Replaced raw `mkdir` in `create_guest_xdg_dir` with `io_mkdir_own`. Simplified `mkparent` to delegate to `io_mkdir_own` (also chowns grandparents). Updated §GUI directory ownership pitfall in Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — other directory-creation code may still use raw mkdir or fail to chown parent dirs.

### Re-entry: chdir(/proc/old_pid/root) after setns fails in container PID namespace

- **Bug**: `spawn_existing` called `join_namespaces(old_pid)` (which includes
  `setns(CLONE_NEWPID)`) before building and passing the chroot path
  `/proc/<old_pid>/root` to `reuse_child`. After fork, the child was in the
  container's PID namespace where host PID `old_pid` didn't exist —
  `chdir("/proc/482145/root")` returned ENOENT.
- **Root cause**: `chdir` happened after `setns(CLONE_NEWPID)` and in the forked
  child — the child couldn't resolve the host PID in the container's PID namespace.
- **Prevention**: Moved `chdir` to before `join_namespaces`, while still in the
  host PID namespace where the host PID resolves. The child inherits the cwd and
  only needs `SYS_CHROOT(".")`. No path construction needed in the child.
- **Recurrence likelihood**: Low — the fix is a simple ordering correction.

### Cleanup child rmdir removes overlay dentry before mount child chroots (regression cycle)

- **Bug**: Three regressions over four fix iterations:
  1. **Original** (fix db01004): Cleanup child `io_rmtree(root)` → `rmdir("/tmp/cage-XXXXXX")` removes the overlay mount point dentry from the shared VFS. The mount child (forked earlier, before `unshare`) cannot resolve any path under that dentry — `mount("/etc/resolv.conf", "/tmp/cage-XXXXXX/etc/resolv.conf", ...)` returns ENOENT.
  2. **Fix v1** (db01004): Parent `chdir(root)` pins dentry via cwd before signaling cleanup child. Mount child inherits pinned cwd. Child `chroot(".")` happens in `child_run` (after `mount_remaining`), causing sysfs EBUSY — `"/sys"` resolves to the host's existing sysfs mount, cannot mount over it.
  3. **Fix v2** (820f7b8): Parent does `chdir(root) + chroot(".")` BEFORE `signal_cleanup` + `fork`. Child inherits chroot, no separate chroot in child. No EBUSY — all metadata mounts resolve within the already-established chroot. However, the cleanup child's rmdir still causes `mount("proc", "/proc", ...)` to fail with ENOENT — even with chroot before signal, the unlinked mount point dentry disrupts the kernel's `path_mountpoint` traversal for `mount(2)`.
  4. **Fix v3** (bc5bd04): Insert `MS_MOVE` between `chdir` and `chroot`. After `mount(root, "/", NULL, MS_MOVE, NULL)`, the overlay is attached to `/` instead of the tmpdir. The tmpdir becomes a regular empty directory. The cleanup child's `rmdir` removes an empty dir — no mount point dentry unlinking. `mount("proc", "/proc", ...)` resolves normally.
- **Root cause**: The cleanup child's `rmdir` of the mount point dentry (while the overlay mount is still attached there) unlinks the dentry from the VFS dcache. The kernel's `path_mountpoint()` traversal encounters the unlinked dentry and returns ENOENT, even though `access()`/`stat()` work from within a chroot.
- **Prevention**: Detach the overlay mount from the tmpdir via `MS_MOVE` before signaling the cleanup child. The tmpdir becomes an empty directory — rmdir has no effect on mount point dentries. Added §Mount ordering: cleanup child rmdir removes overlay dentry before child chroots to Domain-Specific Pitfalls.

### Overlay mount before unshare causes cleanup worker to traverse entire overlay tree

- **Bug**: `setup_overlay` (mounts overlay on tmpdir) was called before `ns_setup`
  (`unshare(CLONE_NEWNS)`). The overlay mount existed in the original mount
  namespace, shared with the cleanup worker (forked even earlier). When the
  cleanup worker woke up and ran `io_rmtree(root)`, it recursively traversed
  the entire overlay tree — including all bind-mounted user data — issuing
  thousands of `open`+`unlink`+`getdents64` syscalls before being killed by
  SIGHUP. Trace showed hundreds of unique paths being walked.
- **Root cause**: Wrong ordering in `prepare_mounts`: `setup_overlay` was called
  before `ns_setup`. The cleanup worker (in the old namespace) had no business
  seeing the overlay — it only needed to `rmdir` the empty tmpdir.
- **Prevention**: Swapped to `ns_setup || setup_overlay`. The overlay is now
  mounted only in the new (unshared) namespace. The cleanup worker, still in the
  old namespace, sees a plain empty directory. Also replaced `io_rmtree(root)`
  with `rmdir(root)` in the cleanup worker to prevent any future recursive
  traversal. Added note to §Mount ordering Domain-Specific Pitfall.

### mount_cgroup2_root: mkdir with EEXIST fallback violates Rule 4

- **Bug**: `mount_cgroup2_root` in `cgroup.c` called `access()` + `mkdir()` with
  EEXIST fallback in a single expression: `access(root, F_OK) != 0 && mkdir(root, 0755) != 0 && errno != EEXIST`. While the `access()` check prevented the EEXIST
  for the common case, the pattern still relied on EEXIST handling instead of
  cleanly separating existence check from creation.
- **Root cause**: The access-first discipline ([syscall-discipline.md Rule 4](syscall-discipline.md#syscall-discipline)) was not applied
  rigorously — the expression mixed existence probing, creation, and error
  handling in a single line with short-circuit evaluation.
- **Prevention**: Replaced with explicit `io_exists()` + raw `mkdir()`:
  `int e = io_exists(root); if (e < 0) return -1; if (e == 1 && mkdir(root, 0755) != 0) return -1;`. Added §mkdir with EEXIST fallback violates access-first
  discipline to Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — same pattern exists in `make_cgroup_dir`
  in the same file (`mkdir(cgdir, 0755) != 0 && errno != EEXIST`).

### cgroup v2: EBUSY on subtree_control write after PID moved to child

- **Bug**: `cgroup_pre_unshare_at` moved the container PID into `cage-{pid}`,
  then called `write_enable_controllers(root, cgdir)` which read the parent's
  (`root`) `cgroup.controllers` but tried to write to the child's (`cgdir`)
  `cgroup.subtree_control`. Since the child cgroup already contained a process,
  the write returned EBUSY. Additionally, `cgroup_v2_enter` (child path)
  called `enable_cgroup_controllers` again on the parent cgroup, redundantly
  re-enabling controllers that the parent had already set up.
- **Root cause**: Ordering — PID was written to cgroup.procs before
  subtree_control was configured. Reading from the wrong cgroup directory
  (parent instead of child) obscured the issue.
- **Prevention**: Changed `write_enable_controllers` to read from the child
  cgroup's `cgroup.controllers` (which inherits available controllers from
  the parent). Removed the redundant `enable_cgroup_controllers` call from
  `cgroup_v2_enter` — the parent already configured subtree_control before
  moving the PID. Updated `test_pre_unshare_subtree_control` to create child's
  `cgroup.controllers` file. Removed `test_v2_enter_controller_enable_fails`
  test since controller enable is no longer done in `cgroup_v2_enter`.
- **Recurrence likelihood**: Medium — passing the wrong cgroup directory
  path to a function that reads/writes controller files is easy to miss.

### chdir_pidfile_parent removed in pidfile lifecycle restructure

- **Bug**: The `chdir_pidfile_parent` function created a fragile dependency:
  the pidfile unlink used basename only, relying on a prior chdir to the
  pidfile directory. If the chdir was missed (or the process was not in the
  expected cwd), `unlink_pidfile` would either delete the wrong file or fail
  silently.
- **Root cause**: The old lifecycle required the parent to chdir to the
  pidfile parent directory so that basename-only unlink would resolve
  correctly — an indirect coupling between namespace setup and pidfile cleanup.
- **Fix**: Removed `chdir_pidfile_parent` entirely. The pidfile is now opened
  before fork, written and closed before bridge setup, and unlinked by full
  path (no chdir needed). See [lifecycle.md §Container Lifecycle](lifecycle.md#container-lifecycle).

### Host home points to /root under sudo instead of real user's home

- **Bug**: Under sudo, `getenv("HOME")` returns /root. This caused pulse
  cookie and .Xauthority to look in /root instead of the real home directory.
  Pulse audio would not connect inside the container.
- **Root cause**: `setup_gui_mounts` and `set_gui_env` used `getenv("HOME")`
  directly. Under sudo, HOME is always /root.
- **Prevention**: Both call sites now check `getenv("SUDO_HOME")` first,
  falling back to `getenv("HOME")`. Added §Host home resolution: SUDO_HOME
  takes precedence over HOME to Domain-Specific Pitfalls.

### Host XDG_RUNTIME_DIR: env-detection path lacks fallback, no GUI env vars emitted

- **Bug**: When running as root via setuid or sudo without `-E`, `$XDG_RUNTIME_DIR`
  was absent from the environment. `set_gui_env()` in `env.c` read `getenv("XDG_RUNTIME_DIR")`
  with no fallback, so `host_xdg_runtime_dir` was NULL, `detect_xdg_gui()` was skipped,
  and no Wayland/Pulse env vars were added to `gui_envs[]`. Meanwhile `get_xdg_dir()` in
  `gui.c` had its own fallback via `SUDO_UID` → `geteuid()`, so mounts worked but env
  vars were missing. The two code paths were inconsistent.
- **Root cause**: Two code paths computed the same value (host XDG_RUNTIME_DIR path)
  with different fallback logic. The env-detection path had no fallback at all.
  Additionally, `geteuid()` was used instead of `getuid()` — wrong for setuid binaries
  where `geteuid()` returns 0 (root).
- **Prevention**: Extracted `get_host_xdg_dir()` into `env.c`/`env.h` with consistent
  fallback chain: env var → `SUDO_UID` → `getuid()`. Both `set_gui_env()` and
  `setup_gui_mounts()` use the same helper. Added §Host XDG_RUNTIME_DIR resolution:
  fallback must be shared to Domain-Specific Pitfalls. Added tests for fallback and
  SUDO_UID paths.
- **Recurrence likelihood**: Medium — any future code path needing the host
  XDG_RUNTIME_DIR could introduce its own ad-hoc resolution logic.

### Guest XDG_RUNTIME_DIR uses host UID for mount target and env var

- **Bug**: `cage -G -u 1001:1001 sh -i` mounted Wayland/Pulse sockets at
  `${root}/run/user/1000/` (host uid) instead of `${root}/run/user/1001/`
  (guest uid). The `XDG_RUNTIME_DIR` env var was set to `/run/user/1000`
  (host path). Guest dir `/run/user/1001` was created by `create_guest_xdg_dir`
  but left empty. Traced via strace.
- **Root cause**: `mount_wayland()` and `mount_pulse()` used the host `xrd`
  path (e.g. `/run/user/1000/wayland-0`) as the mount target inside the
  container — `xrd` was the resolved host path, always using the host UID.
  `detect_xdg_gui()` in `env.c` set `XDG_RUNTIME_DIR=${xrd}` (host path)
  without access to the guest uid. The env var and mount targets were both
  correct for the case where guest uid == host uid, but silently wrong when
  they differed.
- **Prevention**: `mount_wayland()` and `mount_pulse()` now construct mount
  targets under `/run/user/<guest_uid>` using `ctx->uid` from `struct gui_ctx`.
  `fixup_guest_xdg_dir()` in `env.c` replaces the host-path entry in
  `gui_envs` with the guest-path `/run/user/<guest_uid>` after mounts are
  set up. Both code paths (nssetup child, reuse child) call the fixup before
  `launch_cage` builds the envp array. Added `fixup_guest_xdg_dir()` tests.
- **Recurrence likelihood**: Medium — any new code that constructs paths
  inside the container based on host-resolved values may repeat the same
  mistake, particularly if the function receiving the path doesn't distinguish
  "host-side source" from "guest-side target".

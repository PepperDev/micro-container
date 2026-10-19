# Past Incidents (continued)

### USER=user when no passwd entry: fallback name should be NULL, not "user"

- **Bug**: `user_fallbacks` in `user.c:149` unconditionally set `u->name` to
  `"user"` when no passwd entry was found for a numeric UID. Spec says `USER`
  should be **omitted** from the environment in this case.
- **Root cause**: Hardcoded `"user"` string literal as fallback name for
  numeric UIDs without a passwd entry, instead of leaving `u->name = NULL`.
  `append_env` in `put_6` already handled NULL correctly (skipped `USER=`
  entry), but the fallback never produced a NULL.
- **Prevention**: Changed `user_fallbacks` to only set `u->name` when the
  argument contains non-digit characters (i.e. a name was provided). Numeric
  UIDs without passwd entries now leave `u->name = NULL`, and `put_6` skips
  `USER=` accordingly. Tests updated.
- **Recurrence likelihood**: Low — specific to a single fallback function.

### io_mkdir mkdir-first strategy hits EEXIST on existing directories

- **Bug**: `io_mkdir` used a mkdir-first strategy (`mkdir()` → handle EEXIST →
  `stat` to validate). Trace showed three EEXIST events
  (`/home/.user`, `/run/user/1001`, `/home/.user/.config/pulse`), each followed
  by an unnecessary `stat()` in `mkdir_existing`. This violated Syscall
  Discipline Rule 4 (mkdir over existing directories) and Rule 3 (stat on
  directory just created).
- **Root cause**: `io_mkdir` prioritized minimizing syscalls for the
  common case (new directory) while handling EEXIST as a fallback with a
  heavyweight `stat()` validation. The mkdir+EEXIST+stat chain was slower
  than `access(F_OK)` + optional `stat()`.
- **Prevention**: Switched `io_mkdir` to access-first: `io_exists(path)` →
  if exists, `io_isdir(path)` for validation → if not exists, `mkdir()`.
  Removed `mkdir_existing` dead code. Per Syscall Discipline, `access()` is
  lighter than `mkdir()`, and the `stat()` is only done on paths confirmed
  to exist by `access()`. Added §mkdir with EEXIST fallback violates
  access-first discipline to Domain-Specific Pitfalls (already present).
- **Recurrence likelihood**: Medium — the mkdir-first pattern is
  counter-intuitive (mkdir"feels" faster than access+mkdir). Any new
  code creating directories could repeat this mistake.

### chmod after mkdir in create_guest_xdg_dir violates Rule 8

- **Bug**: `create_guest_xdg_dir` in `gui.c:247-249` created
  `/run/user/<uid>` with `mkdir(..., 0755)` then immediately called
  `chmod(path, 0700)`. This violated Syscall Discipline Rule 8
  (no chmod after mkdir).
- **Root cause**: The mkdir mode (0755) was chosen as a safe default,
  then corrected to 0700 via chmod. The mode should have been 0700
  from the start.
- **Prevention**: Changed `mkdir(g, 0755)` to `mkdir(g, 0700)` and
  removed the `chmod()` call.
- **Recurrence likelihood**: Low — specific to one function.

### Duplicate access() on host pulse cookie violates Rule 1

- **Bug**: `mount_user_files` called `find_pulse_cookie(host_home)` which
  called `access("/home/<user>/.config/pulse/cookie", F_OK)` to verify
  existence, then called `mount_hfile` which called the same `access()` again.
  Trace showed two identical `access()` calls 31 µs apart.
- **Root cause**: `find_pulse_cookie` confirmed cookie existence but
  `mount_hfile` had no way to accept a pre-verified path. The existence
  check was duplicated because the two functions were independent.
- **Prevention**: Inlined the mount logic for the known-existent cookie
  path: after `find_pulse_cookie` returns a confirmed path, call
  `mount_file_acl` directly instead of going through `mount_hfile` (which
  would re-check existence). The `io_exists` call in `mount_hfile` is
  skipped, eliminating the duplicate.
- **Recurrence likelihood**: Low — specific to the interaction between
  `find_pulse_cookie` and `mount_hfile` in `mount_user_files`.

### create_guest_xdg_dir ignores EEXIST, skips chown on existing dir

- **Bug**: `create_guest_xdg_dir` called `mkdir(g, 0700)` which returned -1 with
  `errno == EEXIST` when the guest XDG_RUNTIME_DIR already existed (from a prior
  run or base image). The function printed an error message and returned -1 without
  chowning the existing directory, leaving it root-owned. The caller
  (`mount_xdg_socks`) ignored the return value, so the error was silent.
- **Root cause**: The EEXIST case was handled as a generic failure — `mkdir`
  returned -1, the else branch printed an error, but no chown was attempted.
  The caller not checking the return value masked the bug.
- **Prevention**: Added `errno == EEXIST` check: on EEXIST, chown the existing
  directory instead of returning failure. Error message now only prints on
  non-EEXIST failures.
- **Recurrence likelihood**: Low — specific to one function's EEXIST handling.

### Pulse socket mount missing ACL (inconsistent with Wayland ACL)

- **Bug**: `mount_pulse` used `mount_file` which only performs a bind mount without
  setting POSIX ACL on the socket. `mount_wayland` used `mount_file_acl` which calls
  `acl_guest_set` after the bind mount. The spec ([lifecycle.md §GUI Setup](lifecycle.md#gui-setup))
  says `acl_set_if_needed` applies to Pulse sockets, same as Wayland.
- **Root cause**: When `mount_pulse` was originally written, it called `mount_file`
  instead of `mount_file_acl`. The Wayland path used `mount_file_acl` but the Pulse
  path was not updated to match.
- **Prevention**: Changed `mount_file` to `mount_file_acl` in `mount_pulse`. Both
  Wayland and Pulse socket mounts now consistently set ACL via `mount_file_acl`.
- **Recurrence likelihood**: Low — specific to a single function call in `mount_pulse`.
  No other socket mount uses the bare `mount_file` variant.

### Pulse home subdirs created as root despite previous fix — properly fixed

- **Bug**: `$home/.config/pulse` (and `$home/.config`) are created as root by
  `mount_file_acl` → `mount_file` → `mkparent` → `io_mkdir`. Previous fix
  `a9bc762` added `fixup_home_files` to post-hoc chown these dirs, but the
  issue persisted after two attempts.
- **Root cause** (v1): Post-hoc chown unreliable — `fixup_home_files` was
  `void`, ignored failures. It was gated on `mount_user_files` success, so a
  mount failure skipped the chown entirely.
- **Root cause** (v2 — true root cause): `fixup_home_files` constructed paths
  WITHOUT the `root` prefix (e.g. `/home/user/.config` instead of
  `<root>/home/user/.config`), so `io_exists` always checked host paths and
  never found the container dirs. The chown was never actually executed on
  the container paths — the fix was completely non-functional.
- **Prevention**:
  1. `mkparent` now accepts `uid_t`/`gid_t` and calls `sys_chown` on the
     created parent dir immediately after `io_mkdir`.
  2. `mount_file` forwards uid/gid to `mkparent`; `mount_file_acl` passes
     `ctx->uid`/`ctx->gid`.
  3. `fixup_home_files` uses `mem_path(ctx->root, buf)` to construct
     root-prefixed paths, returning `int` and propagating failures.
  4. `mount_user_home` calls `fixup_home_files` unconditionally (not gated
     on `mount_user_files` success).
  5. Added `sys_chown` weak wrapper (mocked in tests) — consistent with
     existing syscall mock infrastructure.
  6. Added test `test_setup_gui_pulse_cookie_chown` verifying `sys_chown`
     is called when pulse cookie is found.
  7. Added test `test_setup_gui_pulse_cookie_chown_fails` covering the
     failure path when `fixup_home_files` chown fails.
  8. Refactored `create_guest_xdg_dir` and `pulse_target` to reduce
     `gui.c` NLOC from 303 to 300 (file LOC threshold).
- **Recurrence likelihood**: Medium — any new code creating container dirs
  without `root` prefix could repeat the same path-construction mistake.
   Added §GUI directory ownership to Domain-Specific Pitfalls.

### init_child setenv calls replaced with env_map

- **Bug**: `init_child` in `prep.c` used `setenv(3)` via `setenv_ef`/`setenv_host`/`set_root_env`
  to set PATH, TERM, LANG, HOME, USER, SHELL before `execve`. This was the last
  remaining `setenv` in the child setup path — inconsistent with the hash-map
  environment mechanism used everywhere else (`launch_cage`/`fill_env`).
- **Root cause**: Legacy code predating the env_map infrastructure. The init
  script path (`--initscript`) was never migrated.
- **Prevention**: Threaded `env_t *` through `do_init` → `run_init` → `init_child`.
  Replaced setenv calls with `env_map_put`/`env_map_build`. Removed `setenv_ef`,
  `setenv_host`, `set_root_env` and `resolve_sh` functions.
- **Recurrence likelihood**: Low — the last setenv usage has been eliminated;
  any new code must explicitly opt into the env_map pattern.

### fill_env guard condition inversion: HOME/USER/SHELL skipped when all missing

- **Bug**: `fill_env` had `if (!nh || !nu || !ns)` as the guard to enter
  `grow_envp`. `nh=1` meant HOME was missing. When all three were missing
  (nh=nu=ns=1), `!nh || !nu || !ns` evaluated to `0 || 0 || 0` = 0 (false).
  The `grow_envp` call was skipped entirely — HOME, USER, and SHELL were never
  appended to the execve array when all three were absent from the env map.
- **Root cause**: Guard condition inversion — the `!` operators negated each
  flag, and the `||` combined them as "enter if any is present" instead of
  the intended "enter if any is missing". The condition `nh || nu || ns`
  (without `!`) was the correct form.
- **Prevention**: Rewrote `fill_env` to use `env_map_ensure` which inserts
  missing variables directly into the map before `env_map_build`. This
  eliminates the guard condition entirely — no condition, no inversion risk.
  Added §Guard condition inversion to Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — similar inversion bugs can appear in
  any compound guard condition with negated flags.

### setup_devshm branch condition inverted

- **Bug**: `setup_devshm` in `setup.c` had the `io_islink` branches swapped.
  `io_islink` returns 0 for symlink, ≠0 for not-symlink. When `/dev/shm` was a
  symlink (==0), the code called `setup_devshm_symlink` (symlink + tmpfs mount)
  instead of `setup_devshm_dir` (tmpdir at run/shm). When `/dev/shm` was not a
  symlink (≠0), it called `setup_devshm_dir` instead of `setup_devshm_symlink`.
- **Root cause**: Branches were inadvertently swapped — the `if (r == 0)` body
  contained `setup_devshm_symlink` instead of `setup_devshm_dir`.
- **Prevention**: Swapped the branches to match the documented reference
  behavior. Tests updated for the corrected mount count and
  `test_setup_devshm_run_shm_blocked_by_file` expectation.
- **Recurrence likelihood**: Low — the fix is a literal branch swap.

### DISPLAY default missing `.0` screen suffix

- **Bug**: When host DISPLAY was unset and no `-e DISPLAY` given, `add_x11()` in
  `env.c` set `DISPLAY=:0` instead of `DISPLAY=:0.0` per spec ([lifecycle.md §GUI Setup](lifecycle.md#gui-setup)).
  Both the code and the existing test assertions expected the wrong format.
- **Root cause**: Format string `":%d"` instead of `":%d.0"`. The spec was likely
  refined after the code was written, and neither was updated to match.
- **Prevention**: Changed format string to `":%d.0"`. Updated all 3 test
  assertions (socket scan, socket lowest, x11 dir scan) to expect `:N.0`.
- **Recurrence likelihood**: Low — single format string in one function.

### Duplicate passwd reads: load_root_user redundant with read_passwd

- **Bug**: `load_root_user()` in `user.c` opened and scanned `$root/etc/passwd` for
  uid==0, duplicating `read_passwd()`'s `copy_root_entry()` call — both populated
  the same `user->root_*` fields. Additionally, `load_root_user()` was called in
  `do_init()` (prep.c) but `parse_user()` (which internally calls `read_passwd()`)
  was called AFTER `do_init()`, so `do_init()`'s root info came from the redundant
  scan rather than the main passwd read.
- **Root cause**: Code duplication — two functions scanning the same file for the
  same data. Wrong call order — `parse_user()` ran after `do_init()` instead of
  before, forcing `do_init()` to use `load_root_user()` as a workaround.
- **Prevention**: Removed `load_root_user()` entirely. `parse_user()` now runs
  before `do_init()`, so `do_init()` reads `root_*` fields from the already-
  populated user struct. Added skip-reparse optimization: when all of `-e HOME`,
  `-e USER`, `-e SHELL` are explicitly given, `-u` is numeric, and supplementary
  groups are all numeric GIDs, the post-init re-parse of passwd is skipped.
- **Recurrence likelihood**: Low — the duplicate function is removed; no code left
  to recur.

### super_escalate fails to set real uid/gid to 0

- **Bug**: `super_escalate()` only checked `egid != 0` for `setregid(0, 0)` but never set real uid to 0. When `-G` flag kept real uid=1000 (only effective was 0), the process still had elevated effective IDs but real IDs at the unprivileged values — operations that check real IDs (like `access()`) would fail.
- **Root cause**: `super_escalate()` only handled effective IDs, not real IDs.
- **Prevention**: `super_escalate()` calls `setreuid(0, 0)` when `ruid != 0`, and `setregid(0, 0)` when either `egid != 0` or `rgid != 0`. The real and effective IDs are captured once in `main()` and passed as parameters.
- **Recurrence likelihood**: Medium — similar oversights can occur in privilege-escalation code that only considers effective IDs.

### -e HOME not honoured for GUI mounts

- **Bug**: `child_setup_gui()` in `prep.c:115` passed `user->home` directly to `setup_gui_mounts()`, ignoring any `-e HOME` override. Additionally, `fixup_home()` in `nssetup.c:114` unconditionally created `user->home` before GUI setup, even though the spec doesn't create home unconditionally.
- **Root cause**: `child_setup_gui` had no awareness of `-e HOME`. `fixup_home()` was legacy code predating the spec's Container Lifecycle definition.
- **Prevention**: Removed `fixup_home()`. `child_setup_gui` now resolves `guest_HOME` per the spec formula: `-e HOME` from env_map → `user->home` → `/home/.user`. The resolved value is passed to `setup_gui_mounts` instead of raw `user->home`.
- **Recurrence likelihood**: Medium — any new code that needs the guest home path (e.g., for dir creation, chown, or bind-mount construction) may use `user->home` directly instead of consulting `-e HOME`.

### get_host_xdg_dir second getresuid() returns wrong ruid after setreuid

- **Bug**: When running via setuid binary, `super_escalate()` called `setreuid(0, 0)`, changing real uid to 0. Later, `get_host_xdg_dir()` in `env.c` called `getresuid()` to get the real uid for the XDG_RUNTIME_DIR fallback path. The second `getresuid(&ruid, ...)` returned `ruid=0` instead of the original caller's uid (e.g., 1000), resulting in `/run/user/0` instead of `/run/user/1000`. This caused Wayland and Pulse socket detection (`detect_xdg_gui`) to fail — no GUI env vars were emitted despite host sockets being available.
- **Root cause**: Two independent `getresuid()` calls in the same process lifecycle — one in `parse_envs`/`set_gui_env` (via `super_escalate`) and one in `get_host_xdg_dir` — with the second one reading a ruid that had been modified by `setreuid` in between. The fix eliminates the second call by threading the original `ruid` through the entire call chain: `main` → `do_spawn` → `spawn_cage`/`spawn_existing` → `parse_envs` → `get_host_xdg_dir`.
- **Prevention**: `get_host_xdg_dir()` now accepts `uid_t ruid` as a parameter instead of calling `getresuid()` internally. All callers pass the ruid obtained from the initial `getresuid()` in `main()`. A related dangling-pointer bug was also fixed: `set_gui_env` stored a pointer to a stack-local `xdg_buf` in `env->host_xdg_runtime_dir` — changed to `mem_strdup()`.
- **Recurrence likelihood**: Medium — any function that independently calls `getresuid()`/`getuid()` after a privilege change (e.g., after `setreuid`, `setregid`, or `setgroups`) may read stale or incorrect identity values. Added §ruid after privilege change to Domain-Specific Pitfalls.

### GUI socket probing in parent before fork violates the docs spec

- **Bug**: `set_gui_env()` in `env.c` was called from `parse_envs()` in the parent's `do_early_setup()`, before fork and overlay mount. It probed sockets via `access()` and set GUI env vars (DISPLAY, WAYLAND_DISPLAY, etc.) in the parent's `env_map_t`. [lifecycle.md §GUI Setup](lifecycle.md#gui-setup) specifies all GUI work (socket detection + mounts + env var setting) as a single step in the child at "setup container filesystem" → "setup GUI mounts", before `move_and_chroot`. The two-phase approach (parent probes, child mounts) was inconsistent with the spec and produced wrong results when parent and child had different filesystem views.
- **Root cause**: `set_gui_env()` was a natural place to add GUI env var logic during the original implementation, but it ran in the parent process — before any filesystem namespace setup. The sideline code didn't check the spec's lifecycle for the correct phase.
- **Prevention**: Split `set_gui_env()` into `capture_host_paths()` (only captures `host_xdg_runtime_dir` and `host_home`, runs in parent's `parse_envs`) and `env_set_gui_envs()` (socket probing + GUI env var setting, runs in `child_setup_gui` after `setup_gui_mounts`). The child has access to the same `env_map_t` via `env_t` in `spawn_st`. No socket probing or env var setting happens in the parent. Added §GUI operations: must run in child, not parent to Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — any future developer adding a new env var that depends on container-local state (mounted sockets, overlay paths) may instinctively add it to `parse_envs` or a similar parent-side function.

### create_guest_xdg_root early return leaves buf uninitialized on second call

- **Bug**: `create_guest_xdg_root` in `gui_xdg.c` returned 0 immediately when
  `xrd_created` was already set, without writing to the `buf` output parameter.
  On the second call (from `ensure_guest_xdg_pulse` after `mount_wayland` had set
  `xrd_created = 1`), the buffer contained uninitialized stack data. This caused
  `mem_path(ctx->root, xdg)` to construct a path like `<root>/run/user/1001/wayland-0/pulse`
  instead of `<root>/run/user/1001/pulse`, because leftover stack data from the
  previous `mount_wayland` call's `p` buffer (`run/user/1001/wayland-0`) contaminated
  the uninitialized `xdg` buffer. Traced via strace showing
  `access("/tmp/cage-.../run/user/1001/wayland-0/pulse", F_OK) = -1 ENOTDIR`.
- **Root cause**: Early return in `create_guest_xdg_root` skipped buffer population
  when `xrd_created` was already set. The function's contract (populate `buf` with
  the guest XDG dir path) was violated on the fast path.
- **Prevention**: Moved the `snprintf` before the `xrd_created` check, so `buf` is
  always populated regardless of whether the directory was already created.
- **Recurrence likelihood**: Low — the fix moves the buffer write before the early
  return; the function now always writes `buf` regardless of `xrd_created` state.

### cgroup_detect cascade mismatch: detection + usage conflated in single function

- **Bug**: `cgroup_pre_unshare_at()` and `cgroup_cleanup()` both called `cgroup_detect()`
  internally, conflating detection with usage. [lifecycle.md §Cgroup Lifecycle](lifecycle.md#cgroup-lifecycle) specifies 4
  distinct lifecycle points with guards: (1) detect once in the caller, (2) pre-unshare
  guarded by type==2, (3) mount cgroupfs guarded by `!cfg->unshare_cgroup`, (4) cleanup
  guarded by type==2 + dir exists. The code called `cgroup_pre_unshare_at` with NULL
  fs_type (triggering internal detect) and `cgroup_cleanup` with no PID/type params
  (triggering internal detect via getpid and /proc/filesystems).
- **Root cause**: Detection was embedded inside functions that should only _use_ the
  detected value. The caller already had `cfg->unshare_cgroup` and could detect once,
  but had no way to pass the result through — the struct/spawn pipeline lacked the
  cgroup fields.
- **Prevention**: Externalized detection to `setup_cgroup(cfg, st)` in cage.c (detects
  once, stores in `spawn_st`). Changed `cgroup_pre_unshare_at(root, fs_type, pid, ...)`,
  `cgroup_pre_unshare(fs_type, pid, root_private)`, and `cgroup_cleanup(pid, fs_type)`
  to accept cascaded parameters instead of calling detect/getpid internally. Added
  `cg_fs_type`, `cg_pid` fields to `prep_result` and `spawn_st`. Added
  `cg_fs_type` field to `run_ctx`. Added §Cgroup lifecycle: detection must be
  separated from usage to Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — the pattern of a function both detecting a
  resource property and using it is a common lifecycle anti-pattern. Any function
  that calls `cgroup_detect()`, `getpid()`, or similar status queries internally
  should instead receive the value as a parameter.

### Pidfile lifecycle restructured (open before fork, close before bridge)

- **Bug**: The old pidfile lifecycle had a fragile chdir dependency: the parent
  chdir'd to the pidfile parent directory inside `prepare_mounts` (after
  `unshare(CLONE_NEWNS)`), enabling basename-only unlink. This broke if the
  chdir was missed or the process was not in the expected cwd. Additionally,
  `close_pid` happened only after container exit (in `finish_child`), keeping
  the pidfile locked for the entire container lifetime.
- **Root cause**: The lifecycle was designed around "open after fork, write
  immediately" to minimize the empty-file window, but the chdir+basename-unlink
  pattern created an indirect coupling that was fragile.
- **Fix**: Opened pidfile before fork, moved write+close to before bridge setup
  (early in `spawn_exec`), removed `chdir_pidfile_parent` entirely, and changed
  `unlink_pidfile` to use full path. See [lifecycle.md §Container Lifecycle](lifecycle.md#container-lifecycle).
- **Recurrence likelihood**: Medium — any future change that moves pidfile
  operations relative to fork or bridge setup could reintroduce ordering bugs.

### cgroup_cleanup inverted io_exists check leaks cgroup directories

- **Bug**: `cgroup_cleanup` in `cgroup_setup.c` guarded removal with
  `if (io_exists(cgdir) != 1) return;`. `io_exists` returns `0` when the path
  exists, so the guard always early-returned for the directory that needed
  removal. Every `cage -C` run leaked `/sys/fs/cgroup/cage-{pid}`; because the
  leaked directories contain virtual cgroup files, a plain `rm -rf` failed with
  EPERM and only recursive `rmdir` could clear them.
- **Root cause**: The guard treated `io_exists`'s three-valued return
  (`0` = exists, `1` = missing, `-1` = error) as a boolean and inverted the
  condition, making the removal path unreachable.
- **Prevention**: Changed the check to `!= 0`. Extracted
  `cgroup_cleanup_at(root, pid, fs_type)` — mirroring the existing
  `cgroup_pre_unshare_at` — so cleanup is testable against a temp root without a
  real cgroupfs. Added tests `cgroup_cleanup_removes_dir`,
  `cgroup_cleanup_at_v1_noop`, and `cgroup_cleanup_at_missing_noop`. Added
  §io_exists polarity: 0 means "exists", 1 means "missing" to
  Domain-Specific Pitfalls.
- **Recurrence likelihood**: High — `io_exists`/`io_isdir` are used throughout
  the codebase and their polarity is easy to invert when read as booleans.

### ext4 loopback: AUTOCLEAR detaches device when setup fd closed before mount

- **Bug**: `io_loop` configured the loop device with `LO_FLAGS_AUTOCLEAR` and
  immediately closed the loop fd before returning. The default (same-fs) ext4
  loopback path then called `mount()` on the already-detached device, which
  failed with `cage: failed to mount ext4 loopback` (dmesg:
  `EXT4-fs (loop0): unable to read superblock`).
- **Root cause**: AUTOCLEAR detaches the loop device as soon as its last user
  releases it. Closing the setup fd before `mount()` left the device with no
  holder, so the kernel cleared it before the filesystem could be mounted.
- **Prevention**: `io_loop` now returns the open loop device fd and
  `setup_ext4_loopback` mounts while holding it, closing it after a successful
  mount and on the mount-failure path. The autoclear ioctls were wrapped as
  `sys_loop_set_status64`/`sys_loop_clr_fd` so the success path is mockable.
  Added `test_loop_returns_open_fd`, documented §Loop device: hold the setup fd
  across mount with LO_FLAGS_AUTOCLEAR, and added rule 9 to
  [lifecycle.md §Samefs / Ext4 Loopback Flow](lifecycle.md#samefs--ext4-loopback-flow).
- **Recurrence likelihood**: Medium — AUTOCLEAR's last-release semantics are
  easy to overlook when refactoring loop setup.

### Numeric -u with explicit group token skips eager passwd resolution

- **Bug**: `cage -u 1000 -g 1000` (and `-u 1000:1000`, `-u 1000 -g 100`)
  produced `HOME=/home/.user` and an empty `USER`/`SHELL` instead of the passwd
  entry's values, while `-u 1000` alone worked correctly.
- **Root cause**: `get_gid` short-circuits when the first group token is numeric
  — it sets `gid` from the token and returns without calling `resolve_passwd`.
  `eager_resolve_user` only called `get_uid`/`get_gid`/`get_groups`, so with a
  numeric group token the passwd file was never parsed before `move_and_chroot`.
  The later `get_home`/`get_name`/`get_shell` calls triggered `resolve_passwd`
  post-chroot, where `open_db("${rootfs}/etc/passwd")` failed, falling back to
  `/home/.user`, a NULL name, and `/bin/sh`.
- **Prevention**: Added `user_resolve()` (calls `resolve_passwd`) and made
  `eager_resolve_user` call it before chroot. Added test
  `numeric_uid_with_group_token_forces_passwd`, which eagerly resolves, then
  removes `${rootfs}/etc/passwd` to simulate the post-chroot view before reading
  the getters. Added §User resolution must happen before chroot to
  Domain-Specific Pitfalls and a Chroot ordering note to
  [lifecycle.md §UID/GID Resolution](lifecycle.md#uidgid-resolution-lazy-getters).
- **Recurrence likelihood**: Medium — any new lazily-resolved user field must be
  forced in `eager_resolve_user`, and the numeric-group short-circuit makes the
  bug invisible for the common `-u <uid>` (no `-g`) case.

### Init script execs host-prefixed path with custom lowerdir

- **Bug**: With a custom lowerdir (`cage -l /altroot -i /init.sh ...`), the init
  script failed to run (`sh: can't open /tmp/cage-XXXX/init.sh`). The same
  command worked with the default lowerdir.
- **Root cause**: `resolve_script` returned the `${root}`-prefixed path (or the
  host path) as the exec path. `init_child` chroots into `${root}` before
  `execve`, so a host-prefixed absolute path pointed outside the new root and
  could not be opened. The default lowerdir `/` masked the bug because the host
  path and the guest path are the same file there.
- **Prevention**: `resolve_script` now always writes the user-supplied
  (guest-relative) path to the output buffer; the pre-chroot existence checks
  only decide whether the script exists. Made `resolve_script` public and added
  `tests/test_prep.c` (guest-absolute, relative, nested, missing, host-path
  as-is). Updated [lifecycle.md §Container Lifecycle](lifecycle.md#container-lifecycle)
  pseudo-code and added §Init script must exec the guest-relative path.
- **Recurrence likelihood**: Medium — any host/guest path translation performed
  before chroot can leak the host form into the post-chroot `execve`.

### Exit status booleanized; signals not mapped

- **Bug**: `cage -u 1000 -g 1000 -- /bin/sh -c 'exit 42'` returned 1 instead of
  42. The re-entry path (`spawn_existing`) collapsed any non-zero status to 1,
  and a command killed by a signal reported `WEXITSTATUS` (often 0) rather than
  `128 + N`.
- **Root cause**: `main.c do_spawn` reduced `spawn_cage`/`spawn_existing` with
  `!= 0`; `finish_child` called `WEXITSTATUS(status)` without checking
  `WIFEXITED`; `wait_status` (re-entry) computed `status == 0 ? 0 : 1`,
  discarding the code.
- **Prevention**: Added `exit_code_from_status` (`src/core/proc.c`) — `WIFEXITED`
  → `WEXITSTATUS`, `WIFSIGNALED` → `128 + WTERMSIG`, else 1 — and routed both
  `finish_child` and `wait_status` through it. `do_spawn` now propagates the
  integer code, mapping negative setup failures to 1. Added tests
  `exit_code_from_status_exited`/`_signaled`/`_real_signal` and
  `wait_status_forwards_code`. Documented lifecycle rule 1 §Exit status
  forwarding and the `128+N` case in the man page.
- **Recurrence likelihood**: Medium — collapsing a status to a boolean at any
  new return path silently loses the forwarded code.

### ext4 backup superblocks written at the primary superblock offset

- **Bug**: The generated ext4 image produced `EXT4-fs (...): Invalid checksum for
  backup superblock 819200/884736/1605632` in `dmesg` (kernel mount-time resize)
  even though `e2fsck -fn` passed.
- **Root cause**: `write_one_backup` (`src/ext4/sb.c`) wrote each backup at
  `base + SB_OFF` — 1024 bytes into the backup group's first block. In ext4 a
  backup superblock lives at the **start** of that block (offset 0); the primary's
  1024-byte offset applies only to group 0. The real backup location therefore
  held zeros, the kernel checksummed zeros and reported a mismatch, and
  `e2fsck -b <blk>` failed with `Bad magic number`.
- **Prevention**: `write_one_backup` now writes at `base` (offset 0); the backup
  GDT already follows at `base + BLK_SIZE`. Added `test_ext4_backup_superblock`
  (magic, `s_block_group_nr`, CRC32C, and GDT-copy checks at the corrected
  location) and §ext4 backup superblock is at the group's first block to
  Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — the primary/backup offset asymmetry is easy
  to regress, but the new unit test pins the location.

### Early temp-root removal and the root-ready futex

- **Bug**: The temporary root `/tmp/cage-XXXXXX` remained visible in the host filesystem for the full lifetime of the container child (until `waitpid` returned), instead of disappearing as soon as the child had entered the new root (`chdir` + `mount --move` + `chroot`). This was a regression introduced by 9647080 ("refactor: merge container setup into a single fork"), which removed the `fork_cleanup` child and the `signal_cleanup`/futex mechanism that had previously allowed the parent to remove the temp root early.
- **Root cause**: The single-fork refactor lost the synchronization point that told the parent "the child no longer has open references to the temp root and has moved itself into the overlayed root". The code in `spawn_exec` returned `finish_child()` immediately after the network handshake and never removed the temp root while the command ran.
- **Prevention**:
  - Added a `MAP_SHARED` "root-ready" futex (`root_futex`) to `struct run_ctx`/`struct spawn_st`. The futex is created in the parent (in `do_setup_prep`), passed through `run_ctx` to the child, and freed on both sides.
  - Child wakes the futex immediately after `move_and_chroot(pr->root)` in `run_mount_inner` and unmapped the shared page (`child_root_ready`).
  - Parent waits on the futex in `spawn_exec` after `do_parent_net`; if the child signals readiness, the parent removes the temp root with `rmdir` (falling back to `io_rmtree` if non-empty) while the command continues. The wait is robust: it polls with `waitid(P_PID, child, ..., WEXITED|WNOHANG|WNOWAIT)` to detect child death before wake and uses short timeouts on `sys_futex_wait_to` to avoid infinite blocking; a failing child causes the wait to complete and cleanup proceeds.
  - Extended syscall API with `sys_futex_wait_to` (and mock support), generalized futex creation/freeing, and added tests (`test_spawn_cage_early_temp_root_removal`, `test_spawn_cage_child_dies_before_root_ready`).
  - Hardened cleanup: `io_rmtree`/`test_rmtree` now detect symlinks with `lstat` and unlink them (don't follow); fallback removal prevents parent-from-the-future mounts/symlinked dirs left in test views from blocking rmdir. Updated docs: [lifecycle.md §Temp Root Cleanup](lifecycle.md#temp-root-cleanup); added §io_rmtree follows symlinks... to Domain-Specific Pitfalls.
- **Recurrence likelihood**: High — any future refactor of fork/cleanup paths can drop the "root-ready" handshake; the two unit tests explicitly cover early removal while running and child-death safety.

### Guest DISPLAY re-formatted to :N.0 instead of host value passed through

- **Bug**: With host `DISPLAY=:0` and `-G`, the guest got `DISPLAY=:0.0`
  instead of `:0`, diverging from §Environment Variables / §Re-entry Flow rule 5
  ("same GUI env as a first-entry -G run" implies the host value verbatim).
- **Root cause**: `env_set_gui_envs` always derived `DISPLAY` from the resolved
  socket number via `add_x11()` (`snprintf(":%d.0", n)`), discarding the host
  `$DISPLAY` string even when it was non-empty and parseable. The `:N.0`
  synthesis is only specified for the empty/unparseable fallback (socket scan).
- **Prevention**: `env_set_gui_envs(env, display, guest_x11num, ...)` now takes
  the resolved display string: `-e DISPLAY` wins, else host `$DISPLAY`
  verbatim; `:N.0` is formatted only when the string is empty/unparseable and
  `guest_x11num >= 0`. `apply_gui_env()` (src/core/prep.c) resolves
  guest-override vs host precedence. Added `test_gui_x11_host_display`
  asserting the non-empty host value passes through unchanged; added §GUI env:
  DISPLAY must be the host value as-is... to Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — GUI env code is small but the
  normalize-vs-passthrough distinction is easy to re-break when adding display
  handling (e.g. screen numbers, remote displays).

### Re-entry set XDG_RUNTIME_DIR unconditionally (no Wayland/Pulse socket)

- **Bug**: an X11-only `-G` re-entry yielded `{DISPLAY, XDG_RUNTIME_DIR}`
  while the first entry yielded `{DISPLAY}` — re-entry did not produce "the
  same GUI environment variables as a first-entry `-G` run"
  ([§Re-entry Flow](lifecycle.md#re-entry-flow-joining-existing-instance)
  rule 5; [§Environment Variables](lifecycle.md#environment-variables):
  `XDG_RUNTIME_DIR` is set only when a Wayland or pulse socket is detected).
- **Root cause**: `fixup_xdg_runtime_dir()` in `src/core/reuse.c` (added in
  27a593d when the older `env_fixup_xdg_runtime()` calls were removed)
  defaulted `XDG_RUNTIME_DIR=/run/user/<uid>` whenever the fresh env map
  lacked the variable — i.e. always, since only `-e` options populate it —
  running before and independently of `child_setup_gui()`, bypassing the
  socket gate (`xrd_created`) used on every other GUI env path. The
  then-orphaned `env_fixup_xdg_runtime()`/`env_map_fixup_xdg()` helpers were
  left behind as dead code.
- **Prevention**: the unconditional default and the dead helpers were
  deleted; the reuse env build was extracted into `reuse_setup_env()`
  (`parse_envs` + `child_setup_gui`) in `src/core/reuse.c`, covered by
  `test_reuse_env_no_socket_no_xdg` (no socket → no `XDG_RUNTIME_DIR`),
  `test_reuse_env_wayland_sets_xdg` (socket → `/run/user/<uid>`), and
  `test_reuse_env_e_xdg_wins` (`-e` value wins over auto-detection). Added
  §Re-entry must not default XDG_RUNTIME_DIR without a socket to
  Domain-Specific Pitfalls. Manual verification: X11-only and Wayland
  first-entry/re-entry pairs each `diff` as identical.
- **Recurrence likelihood**: Medium — re-entry is a separate code path that
  looks like it "needs" a complete environment, so convenience defaults are
  easy to reintroduce when adding new env vars.

### Re-entry `-G` re-created GUI mounts and re-applied socket ACLs

- **Bug**: every `-G` re-entry re-ran `setup_gui_mounts()` after chroot —
  bind-mounting `/tmp/.X11-unix` (and the XDG sockets) again into the
  already-joined mount namespace, so mountinfo lines grew 2 → 3 → 4 over
  two re-entries, and `set_x11_acls()` re-applied POSIX ACLs on the host
  sockets — violating lifecycle.md §Re-entry Flow rule 5 (env only).
- **Root cause**: `reuse_setup_env()` in `src/core/reuse.c` called
  `child_setup_gui()`, the first-entry setup (mounts + ACLs + env), with no
  env-only variant for the re-entry path.
- **Prevention**: split `child_setup_gui()` into shared `begin_gui_ctx()`
  and `apply_gui_env()` plus two thin variants — first entry keeps mounts
  (`child_setup_gui()`), re-entry calls `child_gui_env_only()`, which only
  resolves via detection (`x11_resolve_display()`, `xdg_detect_socks()` —
  mirroring the mount-time conditions including the `guest_ps` pulse
  nuance) and applies env. Covered by `test_reuse_env_wayland_sets_xdg` and
  `test_reuse_env_x11_fallback_display` asserting `_mock_mount_count` and
  `_mock_setxattr_count` stay 0 with env parity. Docker-verified: X11 and
  Wayland first-entry/two-re-entry runs keep mountinfo counts at 1=1=1 and
  their env files `diff` as identical. Added §Re-entry must not re-create
  GUI mounts or re-apply socket ACLs to Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — re-entry still needs a complete GUI
  env, so wiring the full first-entry setup back in is easy; the zero-mount
  assertions in the reuse env tests catch regressions.

### euid==0 path skipped the documented self-heal (`super_fix()` dropped)

- **Bug**: `super_escalate()`'s effective-uid-0 path performed only the
  `setreuid`/`setregid`/`umask` fixups and returned 0 without repairing the
  binary — the lifecycle spec (§Invocation Lifecycle, "self-heal") requires
  chown-to-root + setuid 04755 when the binary is not root-owned or lacks
  the setuid bit, so a root run against e.g. a `docker cp`'d uid-1000 cage
  silently left it unprivileged.
- **Root cause**: the euid==0 branch returned early without calling
  `super_fix()`; only the explicit `cage --fix` invocation and the
  unprivileged sudo path repaired the binary.
- **Prevention**: the euid==0 path now ends in `return super_fix()`
  (`super_fix()` self-checks, a no-op when the binary is already
  root-owned+setuid), and `super_escalate()` was restructured to the chained
  effective-uid dispatch specified in the lifecycle pseudo-code.
  Covered by `test_escalate_as_root_self_heals` (real root → 0; simulated
  root as an unprivileged runner → -1, since the self-heal's chown fails).
- **Recurrence likelihood**: Low — the unit test pins both outcomes and the
  function now mirrors the spec branch-for-branch.

### Named `-u` lost its complementary groups (enumeration guard used `uid_filled`)

- **Bug**: a guest specified by name (e.g. `-u daemon`, or an initscript-created
  user like `-u carol`) got only its primary gid inside the container; the
  `/etc/group` entries listing the user were never added to the setgroups list.
- **Root cause**: `should_enumerate_groups()` required `!uid_filled`, but
  `eager_resolve_user()` calls `user_resolve()` (which sets `uid_filled` while
  resolving the name) before `get_gid()`/`get_groups()`, so Path A (name-based
  enumeration) was never reached whenever passwd was parsed first. The result
  depended on getter order — direct getter-first use worked, the production
  eager order did not.
- **Prevention**: the guard now keys on `name_given`, recorded at parse time
  when `-u` supplied a name and immutable for the `user_t`'s lifetime, so
  enumeration is order-independent; numeric `-u` still never enumerates.
  Unit tests pin both directions (`test_name_groups_after_early_resolve`,
  `test_numeric_uid_no_enum_after_resolve`); `e2e/identity.sh` (n1, n4)
  asserts the end-to-end behaviour. Recorded in Domain-Specific Pitfalls
  (§Group enumeration guard must use parse-time intent).
- **Recurrence likelihood**: Medium.

### `-G` re-entry recomputed GUI env from the caller's host environment

- **Bug**: re-entering an existing instance with `-G` showed the re-entering
  process's own host values inside the guest (e.g.
  `DISPLAY=:2 cage -G -n g0 env` printed `DISPLAY=:2`) instead of the GUI
  environment the instance was started with; `-e` overrides still applied,
  everything else fell back to the caller's `getenv()`.
- **Root cause**: `child_gui_env_only()` (src/core/prep.c) always ran the
  compute-from-caller-env path (`begin_gui_ctx()`/`apply_gui_env()`), which
  reads the *current* process's environment — on re-entry that is the
  caller's env, not the instance's, and after `setns` the instance's real
  env is not reachable via `getenv()` at all.
- **Prevention**: `spawn_existing()` now reads the instance's
  `/proc/<pid>/environ` (the pidfile/setns pid, while still in the host
  mount/pid view, and only when `-G` is given) into `cfg->inst_env`, and
  `child_gui_env_only()` injects only the `-G`-managed keys from that blob
  via `env_map_ensure()` (explicit `-e` still wins), skipping all
  recompute/detection; an unreadable blob falls back to the previous
  compute path. Pinned by `test_reuse_env_inherits_instance_gui`,
  `test_reuse_env_e_overrides_instance`, `test_env_read_pid_environ`,
  `test_spawn_existing_gui_reads_env`, and `e2e/reentry.sh` (10/10,
  including caller-DISPLAY-ignored and DISPLAY-unset cases).
- **Recurrence likelihood**: Low — both directions are asserted at unit
  and e2e level.

### Test objects went stale because `obj/tests/*.d` were never `-include`d

- **Bug**: adding a field to `src/cli/args.h` made `obj/tests/test_args.o` keep
  the previous `cage_config_t` layout (only the test `.c` files I edited were
  recompiled), so the linked `obj/test_runner` read `cfg.user` at the wrong
  offset and segfaulted in `strcmp(NULL, "0")` with no test output at all;
  `make -n` afterwards reported everything up to date.
- **Root cause**: every object rule passes `-MMD -MP`, but the Makefile only
  `-include`d `$(DEPS)` (src objects) and `$(COV_DEPS)` (coverage objects).
  `TEST_OBJS` had no `TEST_DEPS` and no include, so header-only changes never
  rebuilt test objects.
- **Prevention**: `TEST_DEPS := $(TEST_OBJS:.o=.d)` plus `-include $(TEST_DEPS)`
  (Makefile:26, 150), verified by `touch src/cli/args.h && make -n test` listing
  the test objects again. Recorded in Domain-Specific Pitfalls (§Every object
  group's depfile must be wired into `-include`).
- **Recurrence likelihood**: Medium.

### Symlinked guest /etc/resolv.conf followed instead of replaced

- **Bug**: a guest with a symlinked `/etc/resolv.conf` (e.g. the usual
  distro `-> ../run/…` or a dangling link) aborted the run with
  `cage: unable to create …/etc/resolv.conf` instead of unlinking the link
  and installing the host resolver; a valid symlink silently operated on
  the link's target instead of replacing the link itself.
- **Root cause**: `check_or_create_resolv()` (src/sandbox/setup.c) probed
  with `io_exists()` (which uses `access()`, so a dangling symlink reads
  as *missing*) and then `stat()` (which follows the link) for the
  exists-branch. Missing → `create_resolv_file()` → `io_touch()` opened
  with `O_CREAT` *through* the symlink, failing `ENOENT`/`EEXIST` on the
  target path instead of unlinking the link. Spec rule 5
  (§Resolv.conf Strategy) requires: symlink/other/missing → remove if
  present, touch, replace.
- **Prevention**: `check_or_create_resolv()` dispatches on a single
  `lstat()`: `ENOENT` → create, non-regular (incl. symlinks) →
  `handle_nonreg_resolv()` (unlink the link itself + touch), regular →
  existing size/DNS logic. Pinned by `test_setup_resolv_dangling_symlink`
  and `test_setup_resolv_valid_symlink_replaced` (both assert the path
  ends up a regular file; the valid-link test also asserts the link
  target is untouched), plus `e2e/resolv.sh` non-`-N` symlink cases
  (rc + host-resolver content).
- **Recurrence likelihood**: Medium — `io_exists()` (`access()`) and
  `stat()` both follow symlinks; any future existence/probe of a
  possibly-symlinked path must use `lstat()`.

### Overlay mount failure on an unsupported appdir/upperdir was silent

- **Bug**: when the appdir/upperdir (or workdir) lived on a filesystem
  that cannot back an overlay (typically a nested overlayfs), the overlay
  mount failed and the run exited 1 with no diagnostic at all.
- **Root cause**: `prepare_overlay()` (src/sandbox/overlay.c) returned the
  raw `sys_mount()` failure; the container child in
  `nssetup.c` treats any `prepare_overlay() != 0` as a plain `_exit(1)`,
  so no message ever reached stderr.
- **Prevention**: `overlay_mount_result()` reports every mount failure:
  it `statfs()`es the upperdir and, when the backing filesystem is
  overlayfs, prints `appdir/upperdir … is on overlayfs;
  overlay-on-overlay is not supported`; otherwise it prints the mount
  target and preserved `errno` (`strerror` is called before `statfs` can
  clobber errno). `sys_statfs` was added to the syscall wrapper layer so
  the branch is testable: `test_prepare_overlay_mount_fails` (generic
  branch) and `test_prepare_overlay_upper_on_overlayfs`
  (`_mock_statfs_type = OVERLAYFS_SUPER_MAGIC`) cover both outcomes.
  §Container Lifecycle now notes the error contract in the pseudo-code.
- **Recurrence likelihood**: Low — both report branches are executed by
  unit tests and any future silent failure path in the child would have
  to bypass `overlay_mount_result()`.

### Re-entry silently joined a dying session (zombie / kill window)

- **Bug**: a same-`-n` run started while the previous session was exiting
  was treated as a re-entry into a live instance. Because re-entry never
  re-applies mounts or socket ACLs, the new run silently lost its GUI
  setup — `e2e/acl.sh`'s install → `-G` sequence intermittently ran with
  no X11 ACL applied (papered over by the `e2e_wait_session_gone` poll in
  `e2e/_common.sh`).
- **Root cause**: `detect_existing()` (src/core/proc.c) counted any
  `kill(pid, 0)` success on the pidfile PID as "instance alive", but
  `kill(pid, 0)` also succeeds for an **unreaped zombie** (guest exited,
  instance parent not yet reaped) and `-k` keeps the pidfile alive until
  after the graceful-kill grace period — both windows expose a session
  that is dying or being killed, not a joinable instance.
- **Prevention**: `detect_existing()` additionally consults
  `pid_zombie()` (state char after the last `)` of `/proc/<pid>/stat`;
  `Z` → stale → unlink + fresh spawn), and `killpid()` now unlinks the
  pidfile *before* signalling (quiet, ENOENT-tolerant — including the
  instance parent's own unlink) and skips the grace wait for a zombie
  target. §Re-entry Flow rules/pseudo-code updated (alive = alive and
  not zombie). Pinned by `test_detect_existing_zombie` and
  `test_killpid_zombie_target` (reaped-child fixture via a bounded
  `/proc/<pid>/stat` poll), plus a Medium pitfall in qa.md
  ("Liveness probes must not trust kill(pid, 0) alone — zombies answer it").
- **Recurrence likelihood**: Medium — `kill(pid, 0)` is the idiomatic
  liveness probe and zombie success is non-obvious; any future
  supervisor/re-entry detection can reintroduce it.

### `-N` network setup: nfnetlink batch protocol, attr encoding, and address byte order

- **Bug**: `cage -N` built the veth in the wrong netns (guaranteed `EEXIST`
  against the host `eth0`, so the sandbox had no network) and its nftables
  NAT setup silently failed (`EINVAL`/`EOPNOTSUPP`), leaving no `cage-nat`
  table. Once the veth landed, the host end showed `9.0.24.172` instead of
  `172.24.0.1` and a `/24` address got broadcast `255.255.255.1`.
- **Root cause**: several independent defects:
  1. The pair was created in the **parent** (host netns) with the peer named
     `eth0`, colliding with the host's own `eth0`. Per lifecycle.md it must be
     created once from the parent with `IFLA_NET_NS_FD` inside the
     `VETH_INFO_PEER` nest so the peer is born in the child netns.
  2. The kernel rejects a **bare** `NFT_MSG_NEWTABLE` (`EINVAL`) — changes must
     be wrapped in `NFNL_MSG_BATCH_BEGIN`…`NFNL_MSG_BATCH_END`; the chain hook
     needs `NLA_F_NESTED` and `be32` hooknum/priority or the kernel answers
     `EOPNOTSUPP`.
  3. `netlink_fill_addr`/`netlink_fill_up` omitted `NLM_F_ACK`, so no reply
     was generated on success and a blocking read could hang.
  4. `fill_addr_attrs` passed host-order addresses to `IFA_LOCAL` and derived
     the broadcast from the **netmask** instead of the **hostmask**.
- **Prevention**: create the veth from `parent_nl_cmds()` via
  `netlink_create_veth(fd, host, "eth0", nsfd, …)` after the child unshares
  `CLONE_NEWNET`; route all nftables changes through the batch transport in
  src/net/net.c (`nft_batch_*`, `nft_put_nest`/`nft_put_be32`) and drain every
  reply (`netlink_recv_nb` + `nft_drain_rest`); add `NLM_F_ACK` to the addr/up
  builders; `htonl()` exactly once at the attribute boundary and compute the
  broadcast from `(1u << (32 - prefix)) - 1`. Unit tests assert the peer
  `IFLA_NET_NS_FD`, the `NLM_F_ACK` flags, and the network-order local/broadcast
  values; three Medium pitfalls added to qa.md. `e2e/network.sh` sandbox-side
  checks (eth0 `172.24.0.2/31`, loopback, `cage-nat`) now pass.
- **Recurrence likelihood**: Medium — the netlink encodings are undocumented in
  the headers and byte-order/mask mistakes are invisible to the compiler.

### Host XDG_RUNTIME_DIR: a set-but-empty value was treated as authoritative

- **Bug**: a `-G` run with `XDG_RUNTIME_DIR=` (empty) mapped no Wayland/Pulse
  socket: the empty value was used verbatim as the host runtime dir, so the
  socket-detection check looked under `""` and found nothing.
- **Root cause**: `get_host_xdg_dir()` (src/util/env.c) used
  `if (x) return x;`, and `getenv()` returns `""` (non-NULL) for a
  set-but-empty variable. lifecycle.md §GUI Setup says an "empty or unset"
  `XDG_RUNTIME_DIR` falls back to `/run/user/${SUDO_UID:-<real uid>}`.
- **Prevention**: treat empty as unset (`if (x && x[0]) return x;`) so the
  `SUDO_UID`/`ruid` fallback runs; the qa.md "Host XDG_RUNTIME_DIR resolution"
  snippet and the new "Set-but-empty environment variables must be treated as
  unset" pitfall were reconciled. Covered by
  `test_gui_envs_xdg_empty_is_unset` and `e2e/gui_wayland.sh` case `wy3`.
- **Recurrence likelihood**: Medium — `if (getenv("X"))` is idiomatic and the
  empty-string case is easy to miss.

### Custom -e PULSE_SERVER created its parent outside the guest root

- **Bug**: a `-G` run with a custom `-e PULSE_SERVER=/run/…/gsock` whose parent
  did not already exist in the guest aborted with `cage: unable to create
  …/gsock` ("GUI setup failed"). The parent directory was created on the
  container host FS outside the guest root instead of inside it.
- **Root cause**: `prepare_custom_pulse_target()` (src/sandbox/gui_xdg.c) ran
  `io_mkdir_own()` on the parent of the **raw guest-absolute** path. GUI setup
  runs before `move_and_chroot`, so the guest path must be prefixed with
  `ctx->root`; without it the mkdir targeted the container host FS and the
  subsequent `touch_target()` (which *does* prefix the root) found no parent.
- **Prevention**: build the parent with `mem_path(ctx->root, tgt)` before
  `io_mkdir_own`, consistent with `touch_target()`/`ensure_xdg_parent()`; a new
  "GUI setup runs before chroot" pitfall was added to qa.md. Covered by
  `test_setup_gui_custom_pulse_parent_under_root` and `e2e/pulse.sh` cases
  `ps8`/`ps11`.
- **Recurrence likelihood**: Medium — guest-absolute paths resolve to a valid
  but wrong host location before the chroot, with no build-time signal.

### Empty -e PULSE_SERVER= / XDG_RUNTIME_DIR= were treated as authoritative

- **Bug**: `-G -e PULSE_SERVER=` (empty) resolved the guest pulse target to
  `""`, so the bind mount failed and GUI setup aborted; `-G -e
  XDG_RUNTIME_DIR=` (empty) resolved the target under `""` so the default
  pulse socket was never mapped. Both should behave as if the `-e` flag was
  omitted (default `/run/user/<uid>/pulse/native`, uid-generated runtime dir).
- **Root cause**: `find_envvar()` (src/core/prep.c) returned the value after
  `KEY=` verbatim, and `getenv`-style thinking missed that `-e KEY=` yields a
  non-NULL empty string. Every guest override (`guest_ps`, `guest_xrd`, …) and
  the `guest_xrd_env` flag therefore saw the empty value as set.
- **Prevention**: `find_envvar()` now returns `NULL` for a set-but-empty value
  (`v[0] ? v : NULL`), so an empty `-e` override falls back exactly like an
  omitted one; lifecycle.md §GUI Setup was clarified to say "given and not
  empty". Covered by `test_find_envvar_empty_is_unset` and `e2e/pulse.sh`
  cases `ps6`/`ps7`.
- **Recurrence likelihood**: Medium — the empty-string case of `getenv`/`-e`
  parsing is repeatedly mistaken for "unset".

### -N veth peers straddled /31 block boundaries

- **Bug**: `NET_HOST_IP(N)=172.24.0.(2N+1)` and `NET_CAGE_IP(N)=172.24.0.(2N+2)`
  (src/net/net.h) placed the two ends of the `/31` veth link in different
  `/31` blocks (`{.1}` vs `{.2,.3}`), so they were not on-link: the interface
  came up but `ip route get` on the peer returned `Network unreachable` and
  the sandbox could not reach the host end.
- **Root cause**: the two address macros incremented independently (`2N+1`,
  `2N+2`) instead of sharing one block index. A `/31` block `k` owns exactly
  `{2k, 2k+1}`.
- **Prevention**: both macros now derive from the same block — cage `2N`, host
  `2N+1` — and lifecycle.md §Network Namespace Setup rule 4 states the pair
  must be in the same `/31`. Covered by `e2e/network.sh` address assertions and
  its sandbox → host-peer reachability case.
- **Recurrence likelihood**: Medium — the offset arithmetic looks harmless and
  only fails as a runtime reachability error.

### -N sandbox had no default route

- **Bug**: `net_setup_child()` assigned `NET_CAGE_IP(N)` to `eth0` and brought
  it up but never created a route, so a `-N` sandbox could reach only the host
  peer on its own `/31`. Out-of-subnet traffic (e.g. `8.8.8.8`) was
  `Network unreachable` and the NAT masquerade rule never matched.
- **Root cause**: lifecycle.md §Network Namespace Setup specified veth,
  addresses and NAT but no `RTM_NEWROUTE`, and nothing in the code created a
  default route. Address assignment silently provides only the connected route.
- **Prevention**: `net_setup_child()` now adds a default route via
  `NET_HOST_IP(N)` (`netlink_fill_default_route()`/`default_route_add()`);
  lifecycle.md pseudo-code and rule 4 document it. Covered by
  `test_fill_default_route` and `e2e/network.sh` (default-route assertion plus
  an `ip route get 8.8.8.8` out-of-subnet probe).
- **Recurrence likelihood**: Medium — a fresh netns has no routes; only
  loopback and connected routes exist unless one is added explicitly.

### nameserver localhost was not recognised as a loopback resolver

- **Bug**: `line_is_localhost_ns()` (src/sandbox/setup.c) matched only `127.*`,
  `::1` and empty values, so a host `/etc/resolv.conf` containing
  `nameserver localhost` was treated as having a real resolver. Under `-N` cage
  kept the host bind mount (`nlink=1`) instead of the `8.8.8.8` tmpfs
  replacement (`nlink=0`), so the sandbox used an unreachable loopback
  resolver.
- **Root cause**: the loopback classifier enumerated the loopback *addresses*
  but omitted the loopback *name* `localhost`.
- **Prevention**: a single `val_is_localhost()` helper now covers `127.*`,
  `::1`, the literal `localhost` and empty/whitespace; lifecycle.md
  §Resolv.conf Strategy enumerates the forms. Covered by
  `test_needs_default_dns` and `e2e/resolv.sh` case `localhost`.
- **Recurrence likelihood**: Medium — a resolver name is easy to omit when
  classifying by address prefix.

### -N NAT masquerade rule accumulated on every run

- **Bug**: `net_setup_nat()` re-created the `cage-nat` table, its `postrouting`
  chain and a masquerade rule on every `-N` invocation, appending a fresh rule
  each time (`NFT_MSG_NEWRULE | NLM_F_APPEND`). Since the table is deliberately
  never torn down (it is shared by concurrent cages), an e2e run showed 29
  masquerade rules in one chain.
- **Root cause**: the setup was written as "create" but executed as "append":
  no existence check and no `NLM_F_EXCL`, so re-runs added duplicates instead
  of being no-ops.
- **Prevention**: the table/chain/rule are now one atomic batch with
  `NLM_F_EXCL` on the table; an existing table makes the kernel abort with
  `EEXIST`, which `net_setup_nat()` treats as success so nothing is
  re-created. lifecycle.md §Network Namespace Setup records the idempotency
  rule. Covered by `test_net_setup_nat_eexist_is_ok`,
  `test_net_setup_nat_other_error_fails` and `e2e/network.sh`'s
  masquerade-count case.
- **Recurrence likelihood**: Medium — a shared, long-lived resource that is
  "set up" each run is easy to accidentally append to.

### Samefs loopback relocation discarded a custom `-a`

- **Bug**: on the samefs ext4-loopback path, `handle_same_fs()` always
  relocated the appdir to the hardcoded `/var/lib/cage-loop/<leaf>`, so a
  custom `-a /other/dir/xpto` ran from `/var/lib/cage-loop/xpto` (image at
  `/var/lib/cage-loop.img`) — the user's path was silently replaced, and a
  custom `-w` was overwritten with `${appdir}/work` instead of being kept.
- **Root cause**: the loopback constants `EXT4_IMG`/`EXT4_MNT` predate
  custom `-a`; the derivation never considered `parent(computed_appdir)`, and
  the raw config `-U`/`-w` values were not distinguishable from the computed
  ones inside `handle_same_fs()`.
- **Prevention**: `derive_loopdir()` now derives the loop from the appdir's
  parent (suffix `-loop` unless already present; a single-element appdir is
  its own parent), `handle_same_fs(cfg, dirs)` takes the config so raw `-U`
  (fatal) and `-w` (fatal unless under the loopdir) stay enforceable, and
  the image lives at `${loopdir}.img` beside the mount. Covered by
  `test_derive_loopdir_*`, `test_path_under_*`, `test_handle_same_fs_*` and
  the `e2e/appdir.sh` corner matrix (single-level/multi-level/`-loop`-parent
  adoption, custom `-w` inside/outside).
- **Recurrence likelihood**: High — a fixed "well-known" path constant looks
  like infrastructure, not policy, and the failure is a silent relocation of
  a user-supplied path.

### e2e zombie pollution: PID1 never reaped, later suites counted dead sleeps

- **Bug**: a full `e2e/_all.sh` run failed `concurrent.sh` with
  `both sessions running (expected [2], got [4])` and `killing a leaves b
  running (expected [1], got [3])`, and `reentry.sh` with `re-entry session
  torn down by -k (missing [GONE] in [ALIVE])` — while the same suites run
  standalone always passed.
- **Root cause**: the harness container ran `sleep infinity` as PID1.
  PID1 must reap orphans; `sleep` never waits, so every zombie became a
  permanent child of PID1 and `pgrep -x sleep` counts Z-state entries.
  Zombies accumulated: detached `setsid cage` supervisors exit immediately
  (orphaning their session leader to PID1), and `e2e_sanitize` SIGKILLs
  mid-teardown `cage` stragglers, whose guest `sleep` children then die
  unreaped. Suites early in a run stay green, which hid the accumulation.
- **Prevention**: `e2e_new_container()` now passes `--init` (docker-init as
  PID1 reaps orphans — verified `zombies: 0` after the acl→appdir→cgroup→
  concurrent sequence that reproduced the bug) and starts a supervisor
  named `tail -f /dev/null`, because `e2e_sanitize`'s `pkill -9 -x sleep`
  must never be able to kill the supervisor now that it is killable.
  Regression coverage: full `e2e/_all.sh`, 24/24 suites green.
- **Recurrence likelihood**: Medium — a supervisor named after a counted
  process plus a non-reaping PID1 each look harmless in isolation.

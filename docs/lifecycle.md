# Invocation Lifecycle

```
define spawn cage:
  if args are exactly "--fix":
    chown self to root, set setuid bit (04755), exit       # see CLI Reference --fix

  if effective uid is not 0:
    if self is root-owned+setuid:
      error "filesystem may be mounted nosuid", exit 1    # setuid ignored at exec
    run "sudo -- self --fix" in a child                   # std fds closed; sudo prompts via /dev/tty
    on failure: error "sudo --fix failed", exit 1         # see CLI Reference --fix
    re-exec self with original args                       # binary is now setuid-root
  elif self is not root-owned+setuid:
    chown self to root, set setuid bit (04755)            # self-heal; see CLI Reference --fix

  parse args → fill config struct

  if kill mode "-k":
    read "${pidfile}", SIGTERM → SIGKILL, wait for exit, unlink, exit   # unlink after target gone; zombie target skips grace

  if existing instance "${pidfile}":   # see §Re-entry Flow
    call re-entry flow                  # rejoin: setns, fork, child execve, parent waitpid
    return

  call container process
```

Steps 1–4 below describe CLI startup. Everything after step 4 is covered
in detail by the sub-sections that follow.

1. **Parse** — Read CLI arguments with getopt(3) and fill the config struct.
2. **Kill** — If -k is given, read the pidfile, send SIGTERM (then SIGKILL after timeout), wait for the process to be confirmed gone, then unlink the pidfile and exit; pidfile absence therefore means the session is gone. An unreaped-zombie target skips the grace wait.
3. **Directories** — Compute appdir based on -a and -n. Resolve upperdir (-U), workdir (-w), and lowerdir (-l) defaults.
4. **Ext4 image** — If lowerdir is a parent directory on the same filesystem (a custom -U fails; a custom -w outside the derived loopdir fails — rule 3), derive the loopdir from the appdir's parent plus a -loop suffix (kept when it already ends in -loop), e.g. /var/lib/cage → /var/lib/cage-loop; create a 10G sparse ext4 image at ${loopdir}.img, mount it at ${loopdir}, and use it as the application base. (see §Samefs / Ext4 Loopback Flow)

## Container Lifecycle

```
define container process:
  compute "${pidfile}" path (or accept "-p")
  open "${pidfile}" with O_EXCL
  call detect cgroup                             # see §Cgroup Lifecycle

  # setup networking (only when "-N" is given); root setup follows
  if "-N":
    call setup net                               # see §Network Namespace Setup

  # early preparations
  call handle same fs                            # see §Samefs / Ext4 Loopback Flow
  call cgroup pre-unshare                        # see §Cgroup Lifecycle

  # prepare mount namespace
  mkdtemp "/tmp/cage-XXXXXX"                     # see §Temp Root Cleanup
  map "root-ready" futex (MAP_SHARED)            # see §Temp Root Cleanup
  unshare CLONE_NEWPID | CLONE_NEWIPC           # both unconditional
  fork container child; inside container child:
    unshare CLONE_NEWNS [| CLONE_NEWCGROUP] [| CLONE_NEWNET|CLONE_NEWUTS]
                                                  # -C adds CGROUP, -N adds NET+UTS; one call
    if not root_private: mount --make-private /
    mount overlay                              # fails with a clear error if the
                                               # appdir/upperdir is on an
                                               # unsupported fs (e.g. overlayfs)

    # setup container filesystem
    mount proc, sysfs, dev                         # dev bind is recursive; includes /dev/pts
    mount tmpfs "/run" "/tmp" "/var/tmp"
    call mount cgroupfs                          # see §Cgroup Lifecycle
    call setup resolv                            # see §Resolv.conf Strategy
    create "/run" directories

    if lstat "/dev/shm" errors: fail
    elif is symlink: mkdtemp "/run/shm", sticky
    else:
      symlink "/dev/shm" "/run/shm"
      mkdir "/dev/shm"
      mount -t tmpfs tmpfs "/dev/shm"

    # bind mount user volumes ("-v")
    for each host:guest pair:
      # if guest_volume empty, use "${root}/${host_volume}" as target
      stat "${host_volume}"
      if not exist: recursive mkdir "${host_volume}" 0:0 0755
      stat "${root}/${guest_volume}"
      if not exist:
        if host volume is regular file:
          if parent "${root}/${guest_volume}" not exist: recursive mkdir parent
          touch "${root}/${guest_volume}"
        else:
          recursive mkdir "${root}/${guest_volume}" 0:0 0755
      mount --rbind "${host_volume}" "${root}/${guest_volume}"
      mount --make-private "${root}/${guest_volume}"

    parse env vars (capture host defaults, parse "-e" args)
    guest_PATH := -e PATH, else "/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
    guest_TERM := -e TERM, else host TERM, else "linux"
    guest_LANG := -e LANG, else host LANG, else "C"
    guest_SHELL_override := false
    guest_SHELL := -e SHELL; if unset or empty: "/bin/sh", guest_SHELL_override := true

    write "cgroup.procs" (if "-C")
    if "-N": setup veth                          # see §Network Namespace Setup

    if "-i" given: run init script
      if not exist script and not exist "${root}/" + script: _exit 1
      guest script path := script                # exec guest-relative, never "${root}/" + script
      fork + exec init script:
        child:
          lookup root user in "${root}/etc/passwd"
          chdir "${root}", chroot ".", chdir "/" # not "-c"
          PATH, TERM, LANG := guest_PATH, guest_TERM, guest_LANG (execve environ)
          HOME := home from root user, or "/root" if not found
          USER := user from root user, or "root" if not found
          SHELL := if guest_SHELL_override then shell from root user, or "/bin/sh"; else guest_SHELL
          umask 022
          setgroups 0                            # no setre[ug]id needed — uid/gid already 0
          execve computed SHELL, [computed SHELL, guest script path], environ
          _exit 127
        parent:
          waitpid pid, &st
          if st != 0: _exit 1

    if initscript given and uid=0 and gid=0 and no additional groups other than 0:
      same lookup root in "${root}/etc/passwd"
    else:
      lookup user in "${root}/etc/passwd" (+ "${root}/etc/group")
    guest_HOME := -e HOME, else home from user, else "/home/.user"
    call setup GUI mounts                        # see §GUI Setup

    chdir "${root}", mount --move "${root}" "/", chroot "."
    wake "root-ready" futex                      # temp root removable (see §Temp Root Cleanup)

    PATH, TERM, LANG := guest_PATH, guest_TERM, guest_LANG (execve environ, plus all remaining "-e")
    HOME := guest_HOME
    USER := -e USER, else username from user, or not set
    SHELL := if guest_SHELL_override then shell from user, or "/bin/sh"; else guest_SHELL
    umask 022
    if uid != 0: setreuid
    if gid != 0: setregid
    setgroups
    execve user command

  # parent process
  write child pid to "${pidfile}"
  close pidfd
  if "-N": setup net host side                    # see §Network Namespace Setup
  wait futex "root-ready"                        # child entered ${root}; path fds released
  rmdir "/tmp/cage-XXXXXX" temp root             # remove while the command runs (see §Temp Root Cleanup)
  waitpid child                                  # wait for container exit
  unlink "${pidfile}" (full path — parent did not chdir)
  call cleanup cgroup                            # see §Cgroup Lifecycle
```

### Lifecycle Rules

#### 1 — Exit discipline

- **Init process** (the original `spawn_cage`): any fatal error must cascade via return values and release all resources — never call `exit()`, `_exit()`, or `longjmp()` from intermediate functions. Only `main` may call `exit()`.
- **Fork children** (the container child, and the init script child when `-i` is given): `_exit()` is the correct mechanism after an error — each fork child is a separate process with no return path. Every fork child entry function is a terminal: it either `execve()` or `_exit()`. `_exit(127)` is the convention for execve failure (`exec` errors never return).
- **Exit status forwarding**: the container's exit status becomes `cage`'s exit status. `finish_child` (normal spawn) and `wait_status` (re-entry) decode the raw `waitpid` status through `exit_code_from_status`: `WIFEXITED` → `WEXITSTATUS`, `WIFSIGNALED` → `128 + WTERMSIG`, otherwise `1`. `main` must propagate this integer code — never booleanize it — and map setup failures (negative returns) to `1`.

#### 2 — Data flow

- All values must be cascaded via function arguments through the call chain — from `main` → `spawn_cage` → `do_root_prep` → `child_run` → etc. No function reaches outside its signature to read shared state.
- Non-const file-scope globals are **prohibited**.
- Module-scoped `static` variables are **permitted** in the following categories:
  - **File-read caches** that avoid redundant kernel I/O across multiple calls (e.g., `/proc/modules` cache, `modules.dep` cache).
  - **`static const` tables** and one-time-init performance data (e.g., CRC lookup tables, ACL templates, compression magic bytes).
- Values that must be available on **both sides of a fork boundary** (before and after) **must** be cascaded as function parameters — never stored in static variables. Examples: `cgroup_cgdir_pid`, `cgroup_has_cgdir`, `cgroup_fs_type` — these bridge pre-unshare → post-fork cleanup and must become parameters.
- Values that are consumed entirely within one fork side but span multiple function calls on that side **must** also be cascaded as parameters — just within the local call chain, not crossing the fork. Example: `root_private` flag in cgroup mount logic — used only during bind, should be a parameter threaded through `do_mount_cgroup2` → callers, not a static. It must **not** be placed in the same struct as cross-fork values.

## UID/GID Resolution (Lazy Getters)

CLI arguments `-u` and `-g` accept colon-separated values:

- `-u <user>[:<group>[:<group>...]]` — first value is the main user, remaining are supplementary groups
- `-g <group>[:<group>...]` — first value is the main group, remaining are supplementary groups (for `setgroups()`)

**Design principle:** `parse_user` parses CLI tokens immediately and copies the
root filesystem path into `user_t` (never opens files). Each field that requires
file data is resolved on-demand through getter functions. The first getter that
needs passwd/group data triggers a single scan of the relevant file, caching all
entries for subsequent getters.

**Chroot ordering:** `rootfs` is a host-side path, so any getter that reads a
file must run before `move_and_chroot`. The sandbox child eagerly resolves the
user (`user_resolve` → passwd, then uid/gid/groups) before chrooting. Without
this, a numeric `-u` combined with a numeric `-g`/colon group token
short-circuits `get_gid` (no passwd read), leaving HOME/USER/SHELL to resolve
post-chroot where `${rootfs}/etc/passwd` is unreachable — silently falling back
to `/home/.user` and an empty `USER`.

### Initial parse (always, no file I/O)

```
define parse user:
  u->rootfs := "${rootfs}"
  # main user (first token of "-u")
  if "-u" given:
    if token all digits: u->uid := cast number token, u->uid_filled := 1
    else:                u->name := token
  else:
    u->uid_filled := 1, u->uid := 0     # default root
  if "-g" given:
    for each token in "-g": append group_token_t to u->group_tokens[]
  if "-u" has group tokens:
    for each group token in "-u": append group_token_t to u->group_tokens[]
  for each appended group_token_t:
    if token all digits: { .name = NULL, .gid = cast number token }
    else:                { .name = token }
```

### State structure

```
typedef struct {
  char *name;    // NULL if numeric, non-NULL if named
  gid_t gid;     // numeric value, valid only when name == NULL
} group_token_t;
```

```
user_t {
  // Pre-parsed from CLI (set by parse_user, no file I/O)
  uid_t uid;               // numeric UID, valid only if uid_filled
  int uid_filled;          // 1 if uid was parsed from a numeric -u token
  char *name;              // username from non-numeric -u token (or resolved later)

  group_token_t *group_tokens;  // ordered group tokens from -g then -u
  size_t group_tokens_count;

  // Lazily resolved (populated on first getter call that triggers file read)
  gid_t gid;               // main gid, resolved from first valid token or passwd
  int gid_filled;          // 1 after gid has been resolved
  gid_t *groups;           // supplementary groups (all resolved tokens + gid)
  size_t groups_count;

  // Passwd-derived (populated during single passwd scan)
  gid_t passwd_gid;        // gid from matched passwd entry
  char *home;              // from passwd entry
  char *shell;             // from passwd entry

  // Root filesystem path — stored by parse_user for lazy getter file I/O
  char *rootfs;

  int passwd_parsed;       // 0 initially, 1 after etc/passwd was read
  int group_parsed;        // 0 initially, 1 after etc/group was read
}
```

### Private helpers

```
define resolve passwd:  # void, may be called from any getter
  if passwd_parsed: return
  open "${rootfs}/etc/passwd" for read
  if uid_filled:
    if match u->uid in passwd:
      passwd_gid := value, name := value, home := value, shell := value, passwd_parsed := 1
      return
    if u->uid == 0:     # root default
      passwd_gid := 0, name := "root", home := "/root", passwd_parsed := 1
      return
    passwd_gid := u->uid, passwd_parsed := 1
    return
  else:                 # not uid_filled
    if match u->name in passwd:
      uid, passwd_gid, name, home, shell := values, passwd_parsed := 1, uid_filled := 1
      return
    if match "nobody" in passwd:
      uid, passwd_gid, name, home, shell := values, passwd_parsed := 1, uid_filled := 1
      return
    uid := 65534, passwd_gid := 65534, passwd_parsed := 1, uid_filled := 1
    return
```

```
define resolve groups:  # void, no external callers
  if group_parsed: return

  # name-based -u with no -g and no colon groups:
  # enumerate all groups from /etc/group that contain this username
  if group_tokens_count == 0 && !gid_filled && !uid_filled && name != NULL:
    call resolve passwd → fills uid, passwd_gid, uid_filled := 1
    u->gid := u->passwd_gid, gid_filled := 1
    scan "${rootfs}/etc/group" line by line
    for each line whose member list (4th field) contains u->name:
      append group_token_t { .name = NULL, .gid = <parsed GID> }
    group_parsed := 1
    return

  if no named token in u->group_tokens[]:
    group_parsed := 1
    return

  open "${rootfs}/etc/group" once, load name → GID hash
  for each token in u->group_tokens[] (in order):
    if token->name == NULL: numeric, already valid
    else:
      if found in group hash: token->gid := found GID, token->name := NULL
      else:                   leave as unresolved name   # invalid named tokens silently skipped
    if first token that became valid: u->gid := token->gid, u->gid_filled := 1
  group_parsed := 1
  return
```

### Getter flows

Each getter resolves its value lazily via either `resolve_passwd` or `resolve_groups`.
Once a helper has run, subsequent getters return cached values without file I/O.

```
define get user uid:
  if uid_filled: return u->uid
  call resolve passwd
  return u->uid
```

```
define get user name:
  if u->name != NULL || passwd_parsed: return u->name
  call resolve passwd
  return u->name
```

```
define get user home:
  if passwd_parsed: return u->home
  call resolve passwd
  return u->home
```

```
define get user shell:
  if passwd_parsed: return u->shell
  call resolve passwd
  return u->shell
```

```
define get user gid:
  if gid_filled: return u->gid
  if group_tokens_count > 0 && group_tokens[0]->name == NULL:
    u->gid := group_tokens[0]->gid, gid_filled := 1
    return u->gid
  call resolve groups
  if gid_filled: return u->gid
  call resolve passwd
  if u->uid != 0:
    u->gid := u->passwd_gid, gid_filled := 1
    return u->gid
  u->gid := u->uid, gid_filled := 1
  return u->gid
```

```
define get user groups → { gids[], count }:
  call resolve groups
  call get user gid
  add u->gid as first element of u->groups[]
  for each token in u->group_tokens[]:
    if token->name == NULL && token->gid != u->gid:
      sorted insert into u->groups[], no duplicates
  gl.groups := u->groups
  gl.count := u->groups_count
  return gl
```

### Single-passwd-scan behaviour

`resolve_passwd` scans `${root}/etc/passwd` once, then caches results. The
matching path depends on `uid_filled`:

**If `uid_filled`** (numeric `-u`):
1. Match `u->uid` in passwd → found: set `passwd_gid`, `name`, `home`, `shell` from entry.
2. `u->uid == 0` (root default): set `passwd_gid=0`, `name="root"`, `home="/root"` (hardcoded, no scan needed — already ruled out passwd match).
3. Otherwise (UID not in passwd): set `passwd_gid = u->uid` (no `name`/`home`/`shell`).

**If `!uid_filled`** (name-based `-u`):
1. Match `u->name` in passwd → found: set `uid`, `passwd_gid`, `home`, `shell`
   from entry; set `uid_filled = 1`. (`name` already set from CLI.)
2. Match username `"nobody"` in passwd → found: set `uid`, `passwd_gid`, `home`,
   `shell` from entry; set `uid_filled = 1`.
3. Neither found: `uid=65534`, `passwd_gid=65534`; set `uid_filled = 1`.

All branches set `passwd_parsed = 1` before returning.

### Single-group-scan behaviour

`resolve_groups` has two paths:

**Path A — name-based user with no groups given** (`group_tokens_count == 0 && !gid_filled && name_given && name != NULL`):
`name_given` is recorded at parse time when `-u` supplied a name, so the path
is reached regardless of which getter resolves passwd first (`eager_resolve_user`
and the GUI/priv-drop getters all resolve eagerly). A numeric `-u` never
enumerates, even after passwd resolution fills `name`. Calls `resolve_passwd`
to fill uid, then enumerates all `/etc/group` lines whose member list contains
`u->name`. Each matched group becomes a `group_token_t` for downstream use by
`get_groups`. The primary GID is taken from the passwd entry (`u->passwd_gid`).

**Path B — tokens present (CLI `-g` or colon groups):**
Opens `${root}/etc/group` only when named tokens exist. Resolves names to GIDs and sets `u->gid` from the first valid token (left-to-right priority: `-g` then `-u`). Does **not** populate `u->groups[]` — that is deferred to `get_groups`. `group_tokens` remains alive for the lifetime of `user_t`.

If all tokens are numeric (Path B), `resolve_groups` sets `group_parsed = 1` and returns without any file I/O; `get_gid` handles the first numeric token directly.

### Lazy optimisation (automatic)

If a code path never calls a getter, the corresponding file is never opened. In particular:
- If `-u` is numeric, `-g` is numeric, and `-e HOME/USER/SHELL` cover all passwd-derived values, the passwd file is never opened.
- If all group tokens are numeric GIDs, the group file is never opened.
- Root `user_t` (`-u 0`, no tokens) never opens any file — it defaults to `uid=0` and has no group tokens.

### Initscript re-read

Two `user_t` instances exist: one for **root** (`-u 0`, no group tokens) and one
for the **guest user** (from CLI `-u`/`-g`). After `--initscript` completes,
the caller creates a fresh guest `user_t` instance (re-parsed from the same CLI
args) so the next getter call re-opens and re-caches from the modified
`/etc/passwd` and `/etc/group`. The root `user_t` is never re-read — it was
hardcoded as `uid=0` and never opens any file. The re-read instance is rooted at
`/` because the init script runs inside the chroot, after the network and cgroup
setup, so this lookup is the first to see the init script's edits. The pre-init
instance remains in use only for the GUI context, which is assembled before the
chroot.

### Temp Root Cleanup

The temporary root directory (created by `mkdtemp` at `/tmp/cage-XXXXXX`) is the
mount point for the container's OverlayFS tree. It is created by the parent and
removed by the parent **as soon as the container child has entered it** — while
the command is still running — so it does not linger for the session's lifetime.

**Design:**

1. **Single fork** — The container child is the only child forked to prepare the
   container. It unshares its own mount namespace (`CLONE_NEWNS`) and mounts the
   overlay there, so its `mount --move` onto `/` never affects the parent's
   filesystem view.
2. **Parent stays on the host filesystem** — Because the container child owns a
   private mount namespace, the parent never mounts anything at the temp root;
   in the parent's view it stays a plain empty directory.
3. **Signal after entering the root** — Once the child has done `chdir(root)` +
   `mount --move root /` + `chroot(".")`, it no longer references `root` and all
   its path fds are released; it then wakes a `MAP_SHARED` "root-ready" futex.
4. **Early removal** — The parent waits on that futex and, on wake, removes the
   temp root by full path — while the command is still running — with a plain
   `rmdir`, falling back to `io_rmtree` if the directory is unexpectedly
   non-empty. The fallback only ever sees the parent's mount-free view (leftover
   mount-point directories or symlinks on the underlying filesystem), never the
   container's mounts, so it cannot touch container data. No separate cleanup
   child and no fds to leak: the futex mapping is shared memory, unmapped by
   both sides once the handshake completes.
5. **Child-death safety** — A setup failure makes the child `_exit` before it
   wakes the futex. The parent must not block forever, so the futex wait uses a
   20 ms timeout and the wait loop also peeks at the child with
   `waitid(WEXITED | WNOHANG | WNOWAIT)`: a child that dies before the wake ends
   the wait immediately, and the normal reap still happens in `finish_child`.

(The numbered steps are shown in #### Container Lifecycle
above at the correct sequence positions.)


### Cgroup Lifecycle

When `-C` is given and cgroup v2 is detected, cage sets up an isolated cgroup
subtree **before** the main `unshare()` call. This may require a private mount
namespace when `/sys/fs/cgroup` is not already a cgroup2 mount.

```
define detect cgroup:
  if not "-C": return 0                          # skip
  cgroup_fs_type := 0
  read "/proc/filesystems", cache result
    cgroup2  → cgroup_fs_type := 2
    v1 only  → cgroup_fs_type := 1
    none     → cgroup_fs_type := 0
    error    → return -1                          # cascade fatal
  if cgroup_fs_type == 2 and exist "${path}/cgroup.controllers":
    return 0                                      # already mounted on host
  if not exist "${path}": return -1               # no /sys/fs/cgroup at all = fatal
  unshare CLONE_NEWNS
  mount --make-private /
  root_private := true    # must be shared with main flow — see §Container Lifecycle
  mount -t cgroup2 cgroup2 "${path}"
    success → proceed
    ENODEV  → return 0                            # kernel doesn't support cgroup2, skip
    error   → return -1                           # cascade fatal
  return 0

define cgroup pre-unshare:
  if "-C" not given or cgroup_fs_type != 2: return 0    # skip cgroup2 (v1 handled later)
  read "${path}/cgroup.controllers"
  write → "${path}/cgroup.subtree_control"
    # retry on EBUSY ×3 with 50/100/200 ms backoff
  mkdir "${path}/cage-${pid}"
  write PID → "${path}/cage-${pid}/cgroup.procs"
  read "${path}/cage-${pid}/cgroup.controllers"
  write → "${path}/cage-${pid}/cgroup.subtree_control"
    # retry on EBUSY ×3 with 50/100/200 ms backoff
```

Then **post-chroot**, inside the sandbox child:

```
define mount cgroupfs:
  if not "-C": return 0                          # skip
  if cgroup_fs_type == 2:                        # cgroup v2 enter
    mkdir "${root}/sys/fs/cgroup/cage-parent"
    write PID 1 → "${root}/sys/fs/cgroup/cage-parent/cgroup.procs"
  else:                                          # cgroup v1 enter
    read "/proc/cgroups" → controller list
    for each: mkdir + write PID 1 to cgroup.procs
```

And **cleanup** on container exit (called from `finish_child`, `spawn_cleanup`,
`spawn_exec_fail`):

```
define cleanup cgroup:
  if "-C" not given or cgroup_fs_type != 2 or not exist "${path}/cage-${pid}": return 0
  write PID → "${path}/cgroup.procs"             # move current process out before rmdir
  recursive rmdir "${path}/cage-${pid}"
```

**Key rules:**

1. **cgroup_get_type caches** — `/proc/filesystems` is read once via
   `cgroup_get_type()`, cached for the process lifetime. Subsequent calls
   return the cached value.
2. **No cgroup2** — When cgroup2 is not in `/proc/filesystems`, the entire
   pre-unshare setup is skipped. Cgroup v1 is handled later by
   `mount_cgroup_v1` in the sandbox child, inside `mount_cgroups`.
3. **cgroup.controllers probe** — Existence of `/sys/fs/cgroup/cgroup.controllers`
   determines whether cgroup2 is already mounted. If found, skip the unshare+
   mount step entirely. If not found, proceed to mount.
4. **/sys missing is fatal** — If `/sys/fs/cgroup/cgroup.controllers` is absent
   AND `/sys/fs/cgroup` itself does not exist, return -1. There is no fallback.
5. **Private mount namespace** — `unshare(CLONE_NEWNS)` + `mount --make-private /`
   creates an isolated mount tree for the cgroup2 mount. Sets `root_private = 1`
   so subsequent code can check via `cgroup_root_is_private()`.
6. **ENODEV is graceful** — If cgroup2 is not supported by the kernel (ENODEV),
   return 0 (skip). `root_private` is still set on ENODEV.
7. **Two-level controller enable** — `enable_cgroup_controllers(root)` reads
   from the root cgroup and writes to root's `subtree_control` to delegate
   controllers; then `write_enable_controllers(cgdir)` does the same inside
   `cage-${pid}` to delegate to the container.
8. **cage-${pid} dir** — Created at `/sys/fs/cgroup/cage-${pid}`. The PID is
   stored in global `cgroup_cgdir_pid` and tracked via `cgroup_has_cgdir` for
   cleanup.
9. **EBUSY retry** — Writing `subtree_control` may fail with `EBUSY`. Retry
   with 50/100/200ms backoff, up to 3 attempts.
10. **mount cgroupfs post-chroot** — Creates `cage-parent`
    dir (not `cage-${pid}`) because inside the new PID namespace the container
    PID is 1.
11. **Cleanup** — Before recursive rmdir, the current process is written to the
    parent `cgroup.procs` to move it out of the subtree being removed. Called
    from 3 exit paths: `finish_child`, `spawn_cleanup`, `spawn_exec_fail`.
12. **Cgroup namespace owner** — Only the container child unshares
    `CLONE_NEWCGROUP`, folded into its existing `CLONE_NEWNS` unshare (before
    mounting cgroupfs). The parent stays in the host cgroup namespace, so
    cleanup can see `${path}/cage-${pid}` and move itself out via
    `${path}/cgroup.procs`.


### Network Namespace Setup

When `-N` is given, cage connects the container to the host with a veth pair
and a NAT masquerade rule (IPv4, plus a best-effort IPv6 one). There is **no
net-helper process**: the forked
container child provides the container-side netns (it unshares `CLONE_NEWNET`
together with the mount namespace — see L58), and the parent — the only process
still in the host netns — performs the host side. A futex coordinates them.

```
define setup net [main]:                        # before the fork (L45–47)
  modprobe_load("veth")
  open netlink socket                            # bound to the host netns
  allocate veth id N                             # next free "cage-vethN"
  enable ip_forward                              # IPv4
  enable IPv6 forwarding                         # best-effort; see rule 7
  create nft table "cage-nat" + postrouting masquerade for "ip" and "ip6"
                                                 # once; EXCL, EEXIST = ok

define setup veth [container child]:            # child side (L130; only with "-N")
  signal "net-ready" (futex 1)                   # NET already unshared at L58
  wait futex 2                                   # host side done
  assign NET_CAGE_IP(N) to "eth0", bring it up
  add default route via NET_HOST_IP(N) on "eth0"
  assign NET6_CAGE_ADDR(N)/127 to "eth0"         # IPv6 best-effort
  add IPv6 default route via NET6_HOST_ADDR(N)
  bring up "lo"

define setup net host side [main parent]:       # parent side (L145; only with "-N")
  wait futex 1                                   # child has unshared NET
  open "/proc/<child>/ns/net" → nsfd
  create veth pair: "cage-vethN" in host, peer "eth0" placed directly
    into nsfd (VETH_INFO_PEER + IFLA_NET_NS_FD, one RTM_NEWLINK)
  assign NET_HOST_IP(N) to "cage-vethN", bring it up
  assign NET6_HOST_ADDR(N)/127 to "cage-vethN"   # IPv6 best-effort
  signal futex 2
```

**Key rules:**

1. **One fork, no helper** — the container child (L58) unshares
   `CLONE_NEWNS [| CLONE_NEWCGROUP] [| CLONE_NEWNET|CLONE_NEWUTS]` itself;
   there is no separate net-helper process. `CLONE_NEWNET` cannot move up to
   L57: as with `CLONE_NEWNS`, that
   would put the parent in the same netns as the child, and only the host-side
   parent can create the host end or install the NAT rule.
2. **The peer is born in the container netns** — the pair is created in a
   single `RTM_NEWLINK` from the parent, with `IFLA_NET_NS_FD` on the
   `VETH_INFO_PEER` nest, so `eth0` is created *inside* the child netns. It
   must **not** be created in the host and then moved: naming the peer `eth0`
   in the host collides with the host's own `eth0` (`EEXIST`) — this is the
   current `-N` failure. The child netns is empty, so `eth0` is free there.
3. **Futex handshake** — a `MAP_SHARED` futex (1 = child has unshared NET,
   2 = host side done) coordinates the two processes; on `execve` the mapping
   is replaced automatically.
 4. **No DHCP/DNSMASQ** — IP assignment is static: the container gets
   `NET_CAGE_IP(N)` on `eth0`, the host end gets `NET_HOST_IP(N)`, both `/31`
   and **in the same `/31` block** (`NET_HOST_IP(N)=2N+1`, `NET_CAGE_IP(N)=2N`)
   so the two ends are on-link; the next id starts at `2(N+1)`. The container
   also gets a **default route via `NET_HOST_IP(N)` on `eth0`** (`RTM_NEWROUTE`,
   scope universe) so out-of-subnet traffic — and therefore NAT and the
   `8.8.8.8` resolv fallback — reaches the host.
 5. **NAT setup is idempotent and the table outlives sessions** — the
   `cage-nat` table, its `postrouting` chain and the single masquerade rule are
   created as one atomic batch with `NLM_F_EXCL` on the table. If the table
   already exists the kernel returns `EEXIST` and cage treats that as success,
   so re-running `-N` (or a concurrent session) never appends a second
   masquerade rule. The table is **not** torn down per session: it is shared by
   all concurrent cages and only removed out of band.
 6. **Modprobe** — `modprobe_load("veth")` runs before any netlink operation and
   loads `veth` from `/lib/modules/$(uname -r)` (`modules.builtin`,
   `modules.dep` and the `.ko`) with `init_module(2)`. When those files are
   present, a `-N` run on a kernel that booted without `veth` succeeds. When the
   module tree is missing, cage stops before touching netlink with
   `cage: unable to open /lib/modules/<kver>/modules.builtin` and exit status 1.
   Verified with the [netboot module-probe VM](manual-test-modules.md).
 7. **IPv6 is best-effort** — alongside IPv4, `-N` gives the link a ULA pair
   (`NET6_CAGE_ADDR(N) = fdca:6e::2N`, `NET6_HOST_ADDR(N) = fdca:6e::2N+1`,
   the same `/127` block, addressing mirrors `NET_CAGE_IP`/`NET_HOST_IP`), an
   IPv6 default route, `IFA_F_NODAD` to skip duplicate-address detection, and
   an `ip6` `cage-nat` masquerade table built as its own transaction (kept
   separate from the `ip` table so each side is independently idempotent).
   Every IPv6 step is skipped silently when the host has IPv6 disabled (the
   missing `/proc/sys/net/ipv6/...` or an unavailable family), so a whole `-N`
   run never fails for lack of IPv6. Full IPv6 internet egress additionally
   requires an IPv6 upstream on the host.

   On kernels with `force_forwarding` (6.17+), forwarding is enabled with
   `conf/all/force_forwarding=1` plus `conf/default/force_forwarding=1` (so
   the new `cage-vethN` inherits it), never with `conf/all/forwarding=1`:
   global forwarding purges the host's RA-learned default routes on
   interfaces with `accept_ra != 2`, which breaks host IPv6 connectivity.
   Older kernels fall back to raising `accept_ra` to 2 on every existing
   interface first (per-interface writes; `all/accept_ra` does not propagate)
   and only then setting `all/forwarding=1`, so `rt6_purge_dflt_routers()`
   keeps their RA default routes. If any per-interface raise fails, cage
   leaves forwarding off rather than risk the host's connectivity.


 8. **Futex waits are bounded by child death** — both shared handshakes (the
   "net-ready" wait on the parent's host-side path and the "root-ready" wait)
   use a 20 ms timed futex loop that peeks at the child with
   `waitid(WEXITED | WNOHANG | WNOWAIT)`. A child that fails before signalling
   (unshare, mount or init-script failure) therefore ends the wait at once;
   the parent skips the host-side veth work and the normal reap in
   `finish_child` still reports the failure.

### Resolv.conf Strategy

The cage's `/etc/resolv.conf` is set up by `setup_resolv()` in
`src/sandbox/setup.c`, called from the container child during container setup.
The strategy avoids unnecessary bind mounts and accounts for network namespace
isolation (`-N`).

```
define setup resolv:
  stat target → "/etc/resolv.conf"

  if regular file:
    if size == 0:
      goto replace
    else:                                   # size > 0
      if "-N" not given: return 0
      elif valid DNS:    return 0
      else:              goto replace
  else:                                     # symlink / other / missing
    if exist: remove it (fatal on fail)
    touch
    goto replace

  replace:
    if "-N" AND host has only localhost DNS:
      mkstemp + fchmod 0644 + write 8.8.8.8 + bind mount + unlink source
    else:
      bind mount host's resolv.conf into target
    return 0
```

**Key rules:**

1. **Regular file + empty** → replace unconditionally (empty resolv.conf
   is never useful; without `-N` falls to bind mount host's, with `-N`
   checks host DNS quality).
2. **Regular file + non-empty + no `-N`** → leave alone (shared netns
   can reach host's localhost resolver).
3. **Regular file + non-empty + `-N` + valid DNS** → leave alone.
4. **Regular file + non-empty + `-N` + invalid/localhost DNS** → replace
   (no unlink, bind mount works on top of a regular file).
5. **Symlink / other / non-existent** → remove if present (fatal on
    failure), `touch` create empty file, replace.
6. **Replace + `-N` + host has only localhost DNS** → anonymous
   root-owned file with 8.8.8.8/1.1.1.1: `mkstemp`, `fchmod 0644`, write,
   bind mount the file into the target, unlink the source. The bind pins the
   unlinked inode, so no extra tmpfs mount is needed. Cage's netns can't reach
   host's loopback resolver.
7. **Replace + `-N` + host has real DNS** → bind mount host's resolv.conf
   into target. Real DNS is reachable from any network namespace.
8. **Replace + no `-N`** → bind mount host's resolv.conf into target
   (host resolver reachable via shared netns).
9. **Size cap** → reads are capped at 64 KiB. An oversized host file is
   treated as having no real DNS (default DNS is used); an oversized guest
   file fails the setup closed.

> "Localhost DNS" (as opposed to *valid/real* DNS) means a `nameserver` line
> whose value is on loopback (`127.*`, `::1`), the literal name `localhost`, or
> empty/whitespace. Such a resolver is unreachable from an isolated netns, so
> rules 4/6 treat it as invalid. Any other value is real DNS.


### GUI Setup

When `-G` (GUI) is given, cage sets up environment variables and bind-mounts
to enable graphical applications inside the container. The setup runs inside
the sandbox child, before `move_and_chroot`.

#### Socket Resolution

**Wayland socket:**
1. Read `XDG_RUNTIME_DIR` from host env. If empty or unset: use `/run/user/${SUDO_UID:-<real uid>}`.
2. Read `WAYLAND_DISPLAY` from host env. If empty or unset: default to `wayland-0`.
3. Check existence of `${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}` (e.g. `/run/user/1000/wayland-0`).

**PulseAudio socket:**
1. Read `PULSE_SERVER` from host env. If non-empty, parse the first `unix:` path
   (format: `unix:/path/to/socket`). Strip the `unix:` prefix to get the socket
   path. If no `unix:` path is found, fall through to step 2.
2. If `PULSE_SERVER` is empty, unset, or contained no `unix:` path: read
   `XDG_RUNTIME_DIR` from host env. If empty or unset: use
   `/run/user/${SUDO_UID:-<real uid>}`. Check existence of
   `${XDG_RUNTIME_DIR}/pulse/native`.
3. In the guest, `PULSE_SERVER` is **not** set — libpulse defaults to
   `$XDG_RUNTIME_DIR/pulse/native` which resolves to the bind-mounted host
   socket inside the container.

**X11 socket dir:** Check existence of `/tmp/.X11-unix/`.

**Pulse cookie:**
1. Read `PULSE_COOKIE` from host env. If non-empty, use that path directly.
2. Otherwise, read `XDG_CONFIG_HOME` from host env. If non-empty: use
   `${XDG_CONFIG_HOME}/pulse/cookie`.
3. If `XDG_CONFIG_HOME` is empty or unset: use
   `${SUDO_HOME:-$HOME}/.config/pulse/cookie` (where `$SUDO_HOME` and `$HOME`
   are read from host env).
4. Fallback: if none of the above exist, try `${SUDO_HOME:-$HOME}/.pulse-cookie`.
5. In the guest, `PULSE_COOKIE` is **not** set. The cookie is bind-mounted
   to the same path where it was resolved on the host (computed via the same
   `${XDG_CONFIG_HOME:-${HOME}/.config}/pulse/cookie` logic). libpulse in the
   guest uses the same resolution logic with the same `XDG_CONFIG_HOME` and
   `HOME` values (from host env or `-e` overrides), so it finds the cookie
   without needing `PULSE_COOKIE`. Exception: if an explicit `-e PULSE_COOKIE=`
   is given, it IS set in the guest (via `-e`) and we mount the cookie at that
   path — no extra handling needed.

#### GUI Socket Detection

```
define setup GUI mounts:

  # mount X11 socket
  host_DISPLAY := host env                       # even if empty
  guest_DISPLAY := "-e" "DISPLAY" if given and not empty, else host_DISPLAY
  parse host_DISPLAY as [host]:N[.M]:
    host part empty and N parsed → host_X11unix := N
    empty/remote/parse failure   → host_X11unix := -1
  if "-e" "DISPLAY" given and not empty:
    parse guest_DISPLAY as [host]:N[.M]:
      host part empty and N parsed → guest_X11unix := N
      empty/remote/failure         → return 0
  else:
    if host_X11unix == -1 and host_DISPLAY not empty: return 0
    else: guest_X11unix := host_X11unix
  if host_X11unix == -1:
    if not exist "/tmp/.X11-unix/": return 0     # skip X11
    else:
      readdir for lowest "X${N}", host_X11unix := ${N}
      if none found (still -1): return 0         # skip X11
  else:
    if not exist "/tmp/.X11-unix/X${host_X11unix}": return 0   # skip X11
  if guest_X11unix == -1:
    guest_X11unix := host_X11unix
    guest_DISPLAY := ":${guest_X11unix}.0"       # env for guest
  mkdir "${root}/tmp/.X11-unix"
  if host_X11unix == guest_X11unix:
    mount --bind "/tmp/.X11-unix" "${root}/tmp/.X11-unix"
  else:
    touch "${root}/tmp/.X11-unix/X${guest_X11unix}"
    mount --bind "/tmp/.X11-unix/X${host_X11unix}" "${root}/tmp/.X11-unix/X${guest_X11unix}"
  stat "${root}/tmp/.X11-unix/X${guest_X11unix}", S_ISSOCK?
  if no rw permission: setxattr POSIX ACL on "${root}/tmp/.X11-unix/X${guest_X11unix}"

  # mount XDG sockets
  host_xrd := host XDG_RUNTIME_DIR, else "/run/user/${SUDO_UID:-<real uid>}"
  guest_xrd_env := false
  if "-e" "XDG_RUNTIME_DIR" given and not empty:
    guest_xrd := <value from "-e">, guest_xrd_env := true
  else:
    guest_xrd := "/run/user/${uid}"              # from "-u" or parsed
  guest_xrd_dir_created := false

  # mount Wayland socket
  host_WAYLAND_DISPLAY := host env, fallback "wayland-0"
  guest_WAYLAND_DISPLAY := "-e" if given and not empty, fallback host_WAYLAND_DISPLAY
  if not exist "${host_xrd}/${host_WAYLAND_DISPLAY}": return 0   # skip Wayland
  if not exist "${guest_xrd}/${guest_WAYLAND_DISPLAY}":
    if not guest_xrd_env: recursive mkdir parent of "${guest_xrd}" uid:gid 0700
    mkdir "${guest_xrd}" uid:gid (container user) 0700
    touch "${guest_xrd}/${guest_WAYLAND_DISPLAY}"
    guest_xrd_dir_created := true
  mount --bind "${host_xrd}/${host_WAYLAND_DISPLAY}" "${root}/${guest_xrd}/${guest_WAYLAND_DISPLAY}"
  if no rw permission: setxattr POSIX ACL on "${root}/${guest_xrd}/${guest_WAYLAND_DISPLAY}"

  # mount PulseAudio socket
  pulse_xrd := false
  host_PULSE_SERVER := parse host env PULSE_SERVER   # unix: path
    found  → use that path
    absent → use "${host_xrd}/pulse/native"
  guest_PULSE_SERVER := parse "-e" "PULSE_SERVER" if given and not empty,
                        else "${guest_xrd}/pulse/native", pulse_xrd := true
  if not exist "${host_PULSE_SERVER}": return 0  # skip Pulse
  if not exist "${guest_PULSE_SERVER}":
    if pulse_xrd:
      if not guest_xrd_dir_created:
        if not guest_xrd_env: recursive mkdir parent of "${guest_xrd}" uid:gid 0700
        mkdir "${guest_xrd}" uid:gid (container user) 0700
        guest_xrd_dir_created := true
      mkdir "${guest_xrd}/pulse" uid:gid 0700
    else:
      recursive mkdir parent of "${guest_PULSE_SERVER}" uid:gid 0700
  elif pulse_xrd:                                # "${guest_PULSE_SERVER}" already exists
    guest_xrd_dir_created := true
  touch "${guest_PULSE_SERVER}"
  mount --bind "${host_PULSE_SERVER}" "${root}/${guest_PULSE_SERVER}"
  if no rw permission: setxattr POSIX ACL on "${root}/${guest_PULSE_SERVER}"

  if guest_xrd_dir_created AND not guest_xrd_env:
    add container env XDG_RUNTIME_DIR := guest_xrd

  # mount user files (pulse cookie, ".Xauthority")
  host_HOME := SUDO_HOME from host env, fallback HOME
  guest_HOME := received from Container Lifecycle
  guest_HOME_used := false

  # mount ".Xauthority"
  stat "${host_HOME}/.Xauthority"
  if not exist: skip
  guest_HOME_used := true
  if not exist "${guest_HOME}/.Xauthority":
    if not exist "${guest_HOME}": recursive mkdir "${guest_HOME}"   # last component uid:gid 0755, parents 0:0 0755
    touch "${guest_HOME}/.Xauthority"
  mount --bind "${host_HOME}/.Xauthority" "${root}/${guest_HOME}/.Xauthority"
  if no rw permission: setxattr POSIX ACL on "${root}/${guest_HOME}/.Xauthority"

  # resolve pulse cookie
  host_PULSE_COOKIE := host env PULSE_COOKIE if set,
                       else "${XDG_CONFIG_HOME}/pulse/cookie" → "${host_HOME}/.config/pulse/cookie"
                       → "${host_HOME}/.pulse-cookie"
  if none found: skip (no cookie mount)
  guest_PULSE_COOKIE_custom := false
  if "-e" "PULSE_COOKIE" given and not empty:
    guest_PULSE_COOKIE := <value from "-e" "PULSE_COOKIE">, guest_PULSE_COOKIE_custom := true
  else:
    if "-e" "XDG_CONFIG_HOME" given and not empty:
      guest_PULSE_COOKIE := <value from "-e" "XDG_CONFIG_HOME"> + "/pulse/cookie", guest_PULSE_COOKIE_custom := true
    else:
      guest_PULSE_COOKIE := "${guest_HOME}/.config/pulse/cookie"
  if not exist "${guest_PULSE_COOKIE}":
    if guest_PULSE_COOKIE_custom:
      if not exist parent "${guest_PULSE_COOKIE}":
        recursive mkdir parent of "${guest_PULSE_COOKIE}" uid:gid 0755
    else:
      if not exist "${guest_HOME}/.config/pulse":
        if not guest_HOME_used AND not exist "${guest_HOME}":
          recursive mkdir "${guest_HOME}"       # last component uid:gid 0755, parents 0:0 0755
        if not exist "${guest_HOME}/.config":
          mkdir "${guest_HOME}/.config" uid:gid 0755
        mkdir "${guest_HOME}/.config/pulse" uid:gid 0755
  touch "${guest_PULSE_COOKIE}"
  mount --bind "${host_PULSE_COOKIE}" "${root}/${guest_PULSE_COOKIE}"
  if no rw permission: setxattr POSIX ACL on "${root}/${guest_PULSE_COOKIE}"
```

#### Environment Variables

Env vars are set by `parse_envs(env, cfg->envvars, cfg->envvar_count, gui=true)`
in `env.c:set_gui_env`. They are stored in `env->gui_envs[]` and injected into
the child's environment by `launch_cage`.

`XDG_RUNTIME_DIR` is set only if a Wayland socket OR a PulseAudio socket
is detected on the host. It is never set when neither is found.

`DISPLAY` does not require the host `DISPLAY` variable to be set: when it is
empty or unparseable, cage scans `/tmp/.X11-unix/` for the lowest `X<n>` socket
and uses that node (see §GUI Socket Detection).

| Env Var | Value | Condition |
|---------|-------|-----------|
| `DISPLAY` | see §GUI Socket Detection | see §GUI Socket Detection |
| `XDG_RUNTIME_DIR` | `/run/user/$uid` | Wayland or Pulse socket detected |
| `WAYLAND_DISPLAY` | host's `$WAYLAND_DISPLAY` or `wayland-0` | Wayland socket detected |
| `XDG_SESSION_TYPE` | `wayland` | Wayland socket detected |
| `XDG_CURRENT_DESKTOP` | host's `$XDG_CURRENT_DESKTOP` or `wlroots` | Wayland socket detected |
| `GDK_BACKEND` | `wayland` | Wayland socket detected |
| `QT_QPA_PLATFORM` | `wayland-egl` | Wayland socket detected |
| `SDL_VIDEODRIVER` | `wayland` | Wayland socket detected |
| `CLUTTER_BACKEND` | `wayland` | Wayland socket detected |
| `MOZ_ENABLE_WAYLAND` | `1` | Wayland socket detected |
| `_JAVA_AWT_WM_NONREPARENTING` | `1` | Wayland socket detected |

> All env vars set by `-G` can be overridden by explicit `-e` flags. The `-e` value always wins if given and non-empty (an empty `-e` value is treated as if the flag was omitted). This applies uniformly to all variables above.

On **re-entry** (`-G` joining an existing instance) these variables are not
recomputed from the re-entering process's host environment. They are inherited
from the running instance by reading `/proc/<pid>/environ` (the PID used for
`setns`, written to the pidfile), so the values always match the original
instance even when the re-entry's host `DISPLAY`/`XDG_RUNTIME_DIR` differ or are
unset. Only a `-G` re-entry reads `/proc/<pid>/environ`; a re-entry without
`-G` performs no such read. Explicit `-e` flags still override (see §Re-entry
Flow rule 5).

All Wayland-specific variables are set **only** when the Wayland socket
is detected. They are guarded together — either all or none.

#### POSIX ACL Rules

ACLs are set via `setxattr("system.posix_acl_access", ...)` with a binary
v2 header and up to 5 ACL entries. The same logic applies to **all** socket
types (X11, Wayland, PulseAudio).

**Permission check** before setting ACL (checks UNIX write-access bits before applying binary ACL):

The socket is already `stat()`-ed to verify `S_ISSOCK`. Using the same
`stat` result (`st_mode`, `st_uid`, `st_gid`):

1. If target `uid` matches `st_uid` and owner mode has write (`S_IWUSR`) → skip.
2. If target `gid` matches `st_gid` and group mode has write (`S_IWGRP`) → skip.
3. For each supplementary group of the target user: if the group matches
   `st_gid` and group mode has write (`S_IWGRP`) → skip.
4. If none matched → grant `rw-` via binary POSIX ACL with entries:
   `ACL_USER_OBJ` (owner), `ACL_USER` (target uid), `ACL_GROUP_OBJ`
   (owning group), `ACL_MASK` (max permissions), and `ACL_OTHER` (world)
   — all `rw-` (6).

**X11 socket selection for ACL:**

ACL is applied to X11 sockets in `/tmp/.X11-unix/` matching the display
number N parsed from host `$DISPLAY`:

1. If host `$DISPLAY` is non-empty and does NOT start with `:` (remote display),
   the entire X11 mount is skipped — no ACLs are set.
2. If host `$DISPLAY` starts with `:` (e.g. `:0`, `:0.0`), parse the display
   number N. ACL is applied only to `X{N}`.
3. If host `$DISPLAY` is empty or unset, ACL is applied to **all** sockets
   in `/tmp/.X11-unix/`.

#### Socket Bind-Mount Targets

| Resource | Host Source | Container Target | Effect |
|----------|-------------|------------------|--------|
| X11 socket dir | `/tmp/.X11-unix/` | `/tmp/.X11-unix/` | X11 client-server comms |
| Wayland socket | `${host_xrd}/${host_WAYLAND_DISPLAY}` | see §GUI Socket Detection | Wayland compositor comms |
| PulseAudio socket | `${host_xrd}/pulse/` | see §GUI Socket Detection | Audio server comms |
| Pulse cookie | see §GUI Socket Detection | see §GUI Socket Detection | Audio auth |
| .Xauthority | `${host_HOME}/.Xauthority` | see §GUI Socket Detection | X11 auth |

#### Mount Order Constraint

All GUI socket bind-mounts must happen **after** the tmpfs mounts at `/tmp`
and `/run`, because the bind target directories live inside those tmpfs
mounts.

#### Key Rules

1. **Graceful skip** — If a host socket or directory does not exist (e.g. no
   Wayland compositor running), the corresponding mount is silently skipped.
   No error is raised.
2. **No fallback to software rendering** — Cage sets Wayland env vars if the
   Wayland socket exists, but does not set any `LIBGL_ALWAYS_SOFTWARE` or
   equivalent fallback. Software rendering is the user's concern.
3. **ACL hardening** — Every bind-mounted socket gets a POSIX ACL granting
   `rw-` to the target UID. Without this, the guest process would not be
   able to connect to the socket (the host side keeps `0700` permissions
   owned by the host user).
4. **Pulse cookie search order** — First check `PULSE_COOKIE` from host env.
   If empty or unset, check `${XDG_CONFIG_HOME:-$HOME/.config}/pulse/cookie`.
   If that doesn't exist, try `${HOME}/.pulse-cookie`. If none of these exist,
   the cookie mount is skipped.
5. **Container home detection** — The target home directory for `.Xauthority`
   and Pulse cookie mounts comes from `get_home(u)`, resolved lazily from
   container's `/etc/passwd` or fallback (`/home/.user`).
6. **Host home resolution** — The host home for pulse cookie, `.Xauthority`,
   and `XDG_RUNTIME_DIR` detection is resolved with `SUDO_HOME` from host env
   first, falling back to `HOME` from host env. Without this, running via
   `sudo` would look in `/root/` instead of the actual user's home.
7. **X11 DISPLAY fallback** — `DISPLAY` is set to the host's `$DISPLAY`
   value if non-empty; otherwise, Cage scans `/tmp/.X11-unix/` for sockets
   and sets `DISPLAY=:N.0` where N is the lowest socket number found (e.g.
   `X0`→`:0.0`, `X1`→`:1.0`). The X11 socket bind mount is not required
   for `DISPLAY` to be set — a remote `DISPLAY` (e.g. `:10` forwarded over
   SSH) should still be passed through.
8. **XDG_RUNTIME_DIR fallback** — If the host's `$XDG_RUNTIME_DIR` is empty
   or unset, cage computes it as `/run/user/${SUDO_UID:-<effective uid>}`.
   This ensures the runtime dir resolves correctly under `sudo` where
   `XDG_RUNTIME_DIR` may not be inherited.
9. **Wayland socket path resolution** — The Wayland socket path is
   `${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}` where `$WAYLAND_DISPLAY` defaults
   to `wayland-0` if the host env var is empty or unset.
10. **PulseAudio socket path resolution** — The pulse native socket path is
    resolved in order:
    1. Parse host's `$PULSE_SERVER` for the first `unix:` path (e.g.
       `unix:/run/user/1000/pulse/native` → `/run/user/1000/pulse/native`).
       Only the path component is used; the `unix:` prefix is stripped.
    2. Fall back to `${XDG_RUNTIME_DIR}/pulse/native` if `$PULSE_SERVER` is
       empty, unset, or contains no `unix:` path.
    The parent directory (`pulse/`) is bind-mounted whole so that libpulse can
    enumerate available sockets.
11. **No PULSE_SERVER in guest** — `PULSE_SERVER` is not set inside the
    container. libpulse defaults to `$XDG_RUNTIME_DIR/pulse/native` which
    resolves to the bind-mounted host socket.
12. **Guest `/run/user/$uid` permissions** — The directory `/run/user/$uid`
    inside the container must exist with mode `0700` and be owned by the
    container's `uid:gid` before any Wayland or PulseAudio socket bind-mounts
    are applied. This satisfies the XDG Base Directory Specification and is
    required for `libwayland-client` and `libpulse` to accept the runtime
    directory. The parent `/run/user` is created by `setup_run_dirs()` with
    a tmpfs mount at `/run`; the `$uid` subdirectory is created by
    `setup_gui_mounts()`.
13. **XDG_CURRENT_DESKTOP** — Copied from host's `$XDG_CURRENT_DESKTOP` if
    non-empty; falls back to `wlroots`. Only set when Wayland socket is
    detected.

### Samefs / Ext4 Loopback Flow

When the appdir (default `/var/lib/cage` or `/var/lib/cage/app-${name}`) lands
inside the lowerdir tree (usually under `/`) **and on the same filesystem**,
OverlayFS refuses to overlay them. Cage auto-detects this and creates an ext4
loopback image to serve as a separate filesystem for the appdir. An appdir that
is a sibling of the lowerdir on an ordinary filesystem, or that already sits on
its own filesystem (a mount under the lower tree), needs no loopback.

This detection must happen **before** any `unshare()` call (including
the private mount unshare that may be needed to mount cgroup2), so the ext4 loopback message
appears before namespace operations, not after.

```
define handle same fs:
  # inputs (step 3, defaults already applied): computed_appdir,
  # computed_upperdir, computed_workdir; the raw config "upperdir" and
  # "workdir" are non-empty only for a custom -U/-w
  parent of "${computed_upperdir}" → appdir              # raw, for the stat
  copy of appdir, search existing parent → appdir_exist  # only for the stat
  if path_is_parent("${lowerdir}", "${appdir_exist}"):
    # upperdir lands inside the lower tree: same fs is the conflict
    stat "${lowerdir}" + stat "${appdir_exist}" → compare st_dev
    r := (same filesystem? >0 : 0, -1 on error)
  else if "${appdir_exist}" is on overlayfs:
    r := (stat ok? >0 : -1)                 # overlay-on-overlay, no ancestry
  else:
    r := 0                                  # sibling on an ordinary filesystem

  if r <= 0: return r    # different fs (own mount) or error: skip loopback

  # r > 0: same filesystem and the appdir is inside the lower tree
  if "${upperdir}" non-empty: return -1   # custom -U, fatal (rule 3)

  # raw upperdir is empty ⇒ only computed values remain: derive the loop
  # dir from computed_appdir's parent (rules 3 and 4)
  parent of "${computed_appdir}" → p             # no parent → the appdir itself
  if "${p}" ends with "-loop": loopdir := "${p}"
  else: loopdir := "${p}-loop"
  loop_img := "${loopdir}.img"
  leaf of "${computed_appdir}" → leaf
  appdir_loop := "${loopdir}/${leaf}"
  if "${workdir}" non-empty and "${workdir}" not under "${loopdir}": return -1
                                                   # custom -w, fatal (rule 3)
  computed_upperdir := "${appdir_loop}/upper"     # recompute under the loop
  if exist "${computed_upperdir}":
    # use existing loop — a samefs upper here means the loop never worked
    stat "${loopdir}"
    if same fs as "${lowerdir}": return -1       # fatal
    else: appdir := appdir_loop, return 0
  else:
    # ensure loop mounted
    if not exist "${loopdir}": mkdir "${loopdir}"  # io_mkdir creates missing
                                                   # parents; loop_img sits
                                                   # beside loopdir
    stat "${loopdir}"
    if different fs from "${lowerdir}":
      # already mounted
      appdir := appdir_loop, return 0
    create "${loop_img}"                         # skip if exists
    losetup → find free loop device
    mount -t ext4 ext4 "${loopdir}"
    mkdir "${appdir_loop}"
    appdir := appdir_loop     # computed_workdir = ${appdir}/work follows
    proceed to cgroup flow ("-C")
```

**Key rules:**

1. **Samefs detection** — the loopback is needed only when the appdir
   would land *inside the lowerdir tree* on the *same filesystem*, because
   OverlayFS would then be trying to overlay a mount that already contains its
   own lower content. Detection therefore has three parts:
   - **Ancestry** — walk the appdir's path upward to its closest existing
     ancestor (`appdir_closest_existing`) and test whether it is the lowerdir
     itself or a path under it (`path_is_parent(lowerdir, ancestor)`). An
     appdir that is a *sibling* of the lowerdir (e.g. `-l /srv/lower -a
     /srv/app`) is not inside the lower tree.
   - **Same filesystem** — for an ancestor inside the lower tree, `stat(lowerdir)`
     and `stat(ancestor)` must share `st_dev`; if they differ, the appdir
     already sits on its own filesystem (e.g. a tmpfs mounted under the lower
     tree) and no loopback is needed.
   - **Overlay-on-overlay** — for an ancestor *not* inside the lower tree, a
     loopback is forced only when that ancestor is itself on an overlayfs
     (`statfs(ancestor).f_type == OVERLAYFS_SUPER_MAGIC`), because OverlayFS
     cannot nest. A sibling appdir on an ordinary filesystem needs no loopback
     even though it shares the device with the lowerdir.
   Only an inside-the-lower-tree ancestor with a matching `st_dev`, or a
   non-ancestor on an overlayfs, requires a loopback. The device of `lowerdir`
   is passed through to the upper-dir-exists check (step 3), avoiding a
   redundant `stat("/")`.
2. **Default appdir resolution** — `/var/lib/cage` is the default; if it does
   not exist, fall back to the closest existing parent (e.g. `/var/lib`).
3. **Custom paths are contracts** — Custom `-U`, `-w`, `-l` are never moved
   or silently replaced; the only tolerated deviation is the `-loop` append
   on a custom `-a`'s parent (rule 4). In the samefs flow:
   - Custom `-U` → -1 (nothing can be done; the overlay-on-overlay guard —
     cli-arguments.md `-U` states the same contract). Auto-loopback applies
     to the implicit `${computed_appdir}/upper` only, keeping the custom `-a`
     name as the leaf under `loopdir` (rule 4).
   - Custom `-w` → -1 unless it is a path under `loopdir`: the kernel
     requires the workdir on the same mount as upperdir (kernel docs +
     verified `overlayfs: workdir and upperdir must reside under the same
     mount`), so a workdir outside the loop cannot accompany the relocated
     upper. Under `loopdir` it is kept as-is; a computed workdir recomputes
     as `${appdir}/work` and follows the relocation.
4. **Loopdir derivation** — `p := parent(computed_appdir)` (the appdir
   itself when it has no parent, e.g. `-a /xpto` → `/xpto-loop/xpto`); if
   `p` ends in `-loop` it is used as-is, otherwise `loopdir := ${p}-loop`;
   `loop_img := ${loopdir}.img`; the appdir's last element becomes the leaf
   under `loopdir`. Defaults are unchanged: `/var/lib/cage` →
   `/var/lib/cage-loop` + `/var/lib/cage-loop.img`. A custom `-a` keeps its
   own parent: `-a /other/dir/xpto` → `/other/dir-loop/xpto` +
   `/other/dir-loop.img`; an already-suffixed parent is not doubled: `-a
   /other/dir-loop/xpto` → stays `/other/dir-loop/xpto` with
   `/other/dir-loop.img`.
5. **Upper dir exists + samefs guard** — If the upper dir already exists under
   `loopdir`, stat `loopdir` and compare with the
   `lowerdir` device from step 1. If same filesystem, fatal error — the
   loopback mount is on the same fs as lowerdir, defeating the purpose.
6. **Samefs mount detection** — If `loopdir` existed before (no
   `io_mkdir` needed), `stat(loopdir)` and compare `st_dev` with
   the `lowerdir` device from step 1. If same filesystem, the loopback is not yet mounted —
   continue to image creation. If different filesystem, the loopback is
   already active — skip image creation and mount steps. Cheaper than
   reading `/proc/self/mountinfo`.
7. **Idempotent mount** — `find_free_loop` skips already-bound loop devices.
    The mount is skipped if a mount was detected at the target path.
8. **Sparse image, built aside** — `ext4_create_img()` writes a sparse 10G
   file. Actual space is allocated on demand by ext4. The image is built at
   `${loopdir}.img.tmp.<pid>` and `rename()`d into place, so an interrupted
   build never leaves a truncated `${loopdir}.img`. An existing image is
   reused only when `ext4_image_valid()` confirms its exact size and the
   `0xEF53` superblock magic; otherwise it is rebuilt and atomically replaced.
9. **Cleanup on exit** — The ext4 loopback mount persists (the image stays
   mounted across invocations). To reset, unmount and remove manually. Cage
     does not unmount the loopback on exit.
10. **Loop device lifetime** — `io_loop` configures the device with
    `LO_FLAGS_AUTOCLEAR` and returns the open loop fd. The caller holds it open
    until the ext4 `mount()` succeeds, then closes it. The mount keeps the device
    open, so AUTOCLEAR detaches only when the filesystem is unmounted; closing the
    setup fd *before* `mount()` detaches the device and the mount fails (EIO).


### Host Environment Discipline

Host environment variables are read via `getenv()`. The rule depends on
how many code paths need the value.

**Single-access getenv:**
When a host env var is needed by exactly one code path in the entire runtime,
it is fine to call `getenv()` inline at that point of use. No capture, no
cascade, no struct field. Example: `XDG_CURRENT_DESKTOP` is read once inside
`add_wl_vars` and used immediately — no struct field, no parameter threading.

**Multi-access getenv:**
When a host env var is needed by multiple code paths, the value must be
captured once and cascaded through a shared struct or parameter chain, using
a **lazy-fill** pattern — fill on first need, never re-read the host env.

The lazy-fill pattern:
1. Add a pointer field to the shared struct (`struct gui_ctx` or equivalent).
2. On first access, fill the field via `getenv()`.
3. All subsequent accesses read from the struct field — never call `getenv()`
   again.

The lazy-fill guarantees:
- **No speculative reads** — no env var is read unless actually needed.
- **No stale reads** — a path that uses an env var sees the same snapshot
  regardless of which code path triggered the fill first.
- **No re-read** — `getenv()` is called at most once per var; the captured
  value is the sole source from that point on.

**Filled-flag for partial-coverage conditions:**
When the conditions for access differ between code paths (i.e., a downstream
access can fire without the upstream fill having run), a boolean `filled` flag
must accompany the pointer. Rationale: `getenv()` can legitimately return
`NULL` for an unset variable, so `NULL` alone cannot distinguish "never
queried" from "queried and unset".

| Scenario | Conditions | Flag needed? |
|----------|------------|-------------|
| Both accesses share all gating conditions | A ∧ B | No — if first ran, second runs too |
| Downstream access can run without upstream | A (downstream) vs A ∧ B (upstream) | Yes — second access may need to fill |

**setenv prohibition:**
1. `setenv()` must **never** be called in the child setup path. Writing to the
   process environment pollutes the parent's state and breaks isolation between
   capture and use.
2. All environment variables destined for the container process go through
   `env_map` → `env_map_build()` → `execve` environ — the execve array is the
   sole mechanism.
3. This applies uniformly: GUI-detected vars, `-e` overrides, and defaults all
   flow through the hash map.

**Execve-only delivery:**
1. The child process inherits no env vars from the parent's process environment.
2. Every variable that should appear in the container must be explicitly present
   in the `env_map` before `env_map_build()`.
3. Variable not in the map does not exist in the container — there is no
   automatic passthrough from the host environment (except the execve array
   itself which is built from the map).


### Environment Variable Precedence

For `launch_cage` (child_run), env vars are resolved in `parse_envs` and
`put_6` with this precedence:

| Var | Priority | Default if unset |
|-----|----------|------------------|
| `PATH` | `-e PATH` | `/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin` |
| `TERM` | `-e TERM` → host's `$TERM` | `linux` |
| `LANG` | `-e LANG` → host's `$LANG` | `C` |
| `HOME` | `-e HOME` → guest passwd home | `/home/.user` |
| `USER` | `-e USER` → guest passwd name | not set (omitted) |
| `SHELL` | `-e SHELL` → guest passwd shell | `/bin/sh` |

`PATH`, `TERM`, `LANG`, `HOME`, `USER`, `SHELL` are recognized by key — any
other `-e` flag is passed through verbatim.

For `do_init` (initscript), the child is always root. Same precedence rules
apply for `PATH`, `TERM`, `SHELL`, `LANG` as `launch_cage`. Only `HOME` and
`USER` are overridden:

| Var | Precedence |
|-----|------------|
| `PATH` | `-e PATH` → default as above |
| `TERM` | `-e TERM` → host's `$TERM` → `linux` |
| `LANG` | `-e LANG` → host's `$LANG` → `C` |
| `USER` | root passwd name → `root` — `-e USER` ignored |
| `HOME` | root passwd home → `/root` — `-e HOME` ignored |
| `SHELL` | `-e SHELL` → root passwd shell → `/bin/sh` |


## Re-entry Flow (Joining Existing Instance)

When a named application already has a running instance, cage can **re-enter**
it — joining the existing namespaces and running a new command inside the
same container, without re-creating the overlay or volumes.

```
define re-entry flow:
  if not exist "${pidfile}": return 0            # proceed as main
  read pid from "${pidfile}"
  if read pid fails:
    unlink corrupt "${pidfile}", return 0
  if kill pid, 0:
    alive and not zombie → return pid     # kill(0) succeeds on unreaped zombies too
    ESRCH or zombie → unlink stale "${pidfile}", return 0

  if result == 0:
    proceed as main instance
  else:                                          # result > 0: join existing
    setns pid, CLONE_NEWNS | CLONE_NEWPID | ...
    if "-G" given:                               # only -G reads the instance env
      instance_gui := read "-G" vars from "/proc/${pid}/environ"  # NUL-separated KEY=VALUE
    fork:
      child:  set env (incl. -G GUI vars from instance_gui), chdir, drop privs, execve
      parent: waitpid, exit
    # mounts, ACLs and volumes NOT re-applied — the original instance owns them
    # pidfile NOT re-created — existing instance owns it
```

**Key rules:**

1. **Pidfile is the authority** — If a pidfile exists and its PID is alive
   and not an unreaped zombie, the instance exists. No separate lock file.
 2. **Corrupt/stale recovery** — Empty pidfiles (crash between O_EXCL
    open and dprintf after fork), non-numeric content, dead PIDs, and
    unreaped zombies of exited children are all cleaned up silently, and
    the caller proceeds as the main instance.
3. **setns joins all namespaces** — Mount, PID, cgroup, network, and user
   namespaces are joined via `setns()`. The re-entering process sees the
   same filesystem, processes, and network as the original instance.
4. **Limited re-entry scope** — Only `-e`, `-G`, `-u`, `-g`, `-c`, and the
   target command are applied. Initscript, overlay paths, volumes, and
   device mappings from the original invocation are inherited and not
   re-processed.
5. **`-G` re-entry inherits the instance's GUI env, not the caller's** — A
   re-entering `-G` run must expose the **same GUI environment variables as
   the running instance** (`DISPLAY`, `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`,
   and the Wayland variables). It reads them from the instance's own
   environment — `/proc/<pid>/environ`, the NUL-separated `KEY=VALUE` block of
   the PID read from the pidfile and used for `setns` — instead of recomputing
   them from the re-entering invocation's host environment. This read happens
   **only when `-G` is given**; re-entry without `-G` never inspects the
   instance's environment. The re-entry's own
   `DISPLAY`, `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, and `PULSE_*` host values
   are ignored even when they differ or are unset. Explicit `-e KEY=VALUE`
   flags on the re-entry still override the inherited value (their precedence
   is unchanged). The re-entry does **not** re-create mounts or re-apply socket
   ACLs: those were done at first entry and belong to the original instance.
   If the first entry ran as host uid 1001 with guest `-u 1000`, and a later
   re-entry uses 1002, the environment variables are still inherited, but the
   sockets stay writable only by uid 1000 (guest) and uid 1001 (via the ACL set
   at first entry). If `/proc/${pid}/environ` cannot be read, the re-entry
   falls back to computing the GUI env from its own host environment.
6. **No pidfile on re-entry** — The re-entering process does not create or
   compete for the pidfile. The original instance owns it until exit.

The pidfile (`/run/cage/pid` or `/run/cage/${name}.pid`) is the authoritative
mechanism for tracking which "cage" instance is running for a given application.

1. **Creation** — The main process creates the pidfile with `O_EXCL` (fails
   if the file already exists) **before** `fork init process`. After the
   fork, the parent writes the **child** PID to the pidfile, then closes
   the pidfd — all before `setup net host side`. This creates a window between
   `O_EXCL` open and `dprintf` where the pidfile is empty but exists,
   covered by crash recovery (rule 4).

2. **Removal on exit** — After `waitpid(child, &status)` returns, the parent
   (still on the host filesystem, never chrooted) calls `unlink()` with the
   **full pidfile path** (the parent never chdir'd to the pidfile directory).
   The parent stays on the host filesystem throughout, so the full path
   resolves correctly from any cwd. The unlink tolerates `ENOENT`: `-k` may
   have removed the pidfile first (`killpid()` unlinks after signalling and
   waiting for the target to be gone).

3. **Re-entry detection** — When `cage` is invoked for a named application
   that already has a running instance:
   - `detect_existing()` in `src/core/proc.c:301` checks if the pidfile
     exists via `io_exists()`. If not found, returns 0 (no instance).
   - If the pidfile exists, `readpid()` reads the PID. `pidexists(pid)`
     probes with `kill(pid, 0)`; because `kill(pid, 0)` also succeeds on an
     unreaped zombie, `pid_zombie()` reads the state field from
     `/proc/<pid>/stat` (the character after the last `)`):
     - Returns 0 (alive) and state is not `Z` → the PID is returned,
       `do_spawn()` branches to `spawn_existing()` which joins the existing
       namespaces and forks.
     - Returns 1 (ESRCH / not alive), or the PID is a zombie left by an
       exiting instance → the pidfile is stale. `detect_existing()` unlinks
       it and returns 0. The caller then proceeds as the main process.
   - If `readpid()` fails (empty pidfile, non-numeric content, or I/O
     error), `detect_existing()` unlinks the corrupt pidfile and returns 0
     (no instance). This handles crash recovery: if the binary crashed
     after `create_pidfile()` (O_EXCL, before fork) but before the
     `dprintf()` that writes the PID (after fork, before the host-side net setup),
     the empty pidfile is cleaned up on the next invocation.

4. **Crash recovery** — If the binary crashes after `create_pidfile()`
   (opened before fork) but before the `dprintf()` that writes the PID
   (after fork, before the host-side net setup), the pidfile exists but is empty.
   On the next invocation, `detect_existing()` reads 0 bytes, `readpid()` →
   `parse_pid("")` returns -1, and `detect_existing()` unlinks the empty
   pidfile, returning 0 (no instance). The same recovery applies to
   `killpid()` (returns 0).

5. **Race safety** — The pidfile chain is single-threaded and sequential:
   `detect_existing()` runs before `spawn_cage()`. `create_pidfile()` is
   called early in `spawn_init()`, **before** the fork, so the pidfile is
   created and locked before any child process exists. If another instance
   races between `detect_existing()` and `create_pidfile()`, the second
   caller's `create_pidfile()` fails with `EEXIST`, causing it to exit —
   `O_EXCL` provides fail-safe atomicity. Teardown windows are closed by
   ordering: `killpid()` unlinks the pidfile only after the old target is
   confirmed gone (a concurrent `detect_existing()` then sees no pidfile and
   spawns fresh), and a fresh spawn overlapping post-unlink teardown of the
   old session is safe (unique `mkdtemp` root, per-run cgroups; the old mounts
   die with its namespaces).
   The residual zombie→`setns` TOCTOU remains: if the zombie is reaped
   between detection and `setns`, `setns` fails with `ESRCH` and the run
   reports `cage: setns failed` — a clean error, not a degraded join.

6. **Kill mode** (`-k` flag) — `killpid()` reads the pidfile, releases the
   flock, then sends SIGTERM (then SIGKILL after a grace period), waits for
   the target to be confirmed gone, and only then unlinks the pidfile
   (ENOENT-tolerant — a concurrent main process may have removed it already);
   if the target is already an unreaped zombie the grace wait is skipped.
   Same stale-detection logic applies.

**Retry behavior:**
- **Syscall retry**: Syscalls that may fail with `EINTR` (e.g. `waitpid`, `write`, `read`) are wrapped in loops with exponential backoff: `wait = min((1 << attempt) * 10ms, 1s)`, max 8 attempts.
- **Resource acquisition**: `malloc` / `realloc` failures inside tight loops retry with backoff after `COND_BROADCAST` wakeups. After 3 consecutive failures, return `NULL` to caller.
- **Cgroup operations**: Writing to cgroup control files retries up to 3 times with 50ms / 100ms / 200ms delay on `EBUSY`.

# Past Incidents (continued)

### Non-idempotent indent made the format check fail in both directions

- **Bug**: the CI format step (`make format && git diff --exit-code`) reported
  `src/user/user_parse.c:67` as unformatted, yet running the same step again
  locally flipped the line back — the check could never pass for either
  committed form.
- **Root cause**: GNU indent 2.2.13 is not idempotent for a comma immediately
  followed by a cast of a parenthesized expression. Every pass toggles
  `parse_one_token(&(*t)[*n], a, (size_t)(end - a));` between `a, (size_t)` and
  `a,(size_t)`. Whichever form was committed, one `make format` pass changed it
  and the following `git diff --exit-code` failed; a second pass restored the
  committed form, which looked like an inconsistent check. `append_tokens()`
  was the only place in `src/`/`tests/` carrying the pattern (verified with a
  per-file two-pass scan; `find -exec indent {} +` batching was not a factor).
- **Prevention**: the cast is now bound to a local
  (`size_t len = (size_t)(end - a);`), removing the pattern and making the whole
  tree idempotent (per-file two-pass scan: one oscillating file before, none
  after). `.github/scripts/build` formats a copy of every file twice and fails
  with the offending file name when the passes disagree. docs/qa.md
  §Domain-Specific Pitfalls records the rule.
- **Recurrence likelihood**: Medium — the failure presents as a flaky format
  check rather than a formatting error, so the real cause is easy to miss.

### Release cross-build broke on a newer UAPI constant and format truncation

- **Bug**: the `release` toolchains (`x86_64-linux-musl-gcc` and the other
  three) failed to build `cage`, first at `src/pkg/modload.c:163`
  (`'MODULE_INIT_COMPRESSED_FILE' undeclared`), then at
  `src/sandbox/gui_x11.c:22` (`'%s' directive output may be truncated`), while
  `make CC=gcc` on the host succeeded with no warnings.
- **Root cause**: the release toolchains bundle Linux 5.15 UAPI headers, but
  `MODULE_INIT_COMPRESSED_FILE` is defined only since Linux 5.17, so
  `#include <linux/module.h>` no longer provided it. Independently,
  `set_sock_acl()` formatted `/tmp/.X11-unix/` plus a directory entry name (up
  to 255 bytes) into a 64-byte buffer; the toolchains' GCC reported the
  potential truncation as an error (`-Werror`), whereas the host GCC did not.
  The build CI compiles with the distro `gcc` only, so neither failure appeared
  until the release was cut.
- **Prevention**: `finit_module`'s compressed-file flag now has a version-gated
  fallback (`#ifndef MODULE_INIT_COMPRESSED_FILE`); `set_sock_acl()` uses
  `char path[PATH_MAX]` and rejects a truncated result. The release toolchain
  build is the verification step for every future UAPI/`snprintf` change (all
  four arches were rebuilt clean). docs/qa.md §Domain-Specific Pitfalls records
  the rule.
- **Recurrence likelihood**: Medium — the host `gcc` accepts both patterns, so
  the failure surfaces only in the cross/release build.

### Release RPM cross-build failed for every non-native architecture

- **Bug**: the `release` packaging step aborted with
  `error: No compatible architectures found for build` immediately after the
  i686 link line, so it looked like `i686-linux-musl-gcc -static -flto` had
  failed to link `bin/cage`.
- **Root cause**: the generated spec set `BuildArch: ${RPM_ARCH}` (the *target*
  arch) while `rpmbuild` ran on an x86_64 runner. `rpmbuild` refuses to build a
  package whose `BuildArch` is not compatible with the build host — true for
  i686, armv7hl and aarch64 — so the first non-native arch aborted the release.
  The follow-up `cp` also read from
  `RPMS/$HOST_ARCH/cage-${VERSION}-1.${HOST_ARCH}.rpm` (the host dir and name),
  not the target, so it would have copied the wrong file even on success.
  Reproduced locally: `rpmbuild -ba` on x86_64 with `BuildArch: i686` prints the
  exact message.
- **Prevention**: the spec no longer sets `BuildArch`; `rpmbuild` is invoked
  with `--target "$RPM_ARCH"` to cross-build, and the RPM is copied from
  `RPMS/$RPM_ARCH/cage-${VERSION}-1.${RPM_ARCH}.rpm`. All four target arches now
  build on the x86_64 host (verified). docs/qa.md §Domain-Specific Pitfalls
  records the rule.
- **Recurrence likelihood**: Medium — the spec looks correct and the failure
  masquerades as a compiler/linker error at the tail of the build log.

### `-n` was interpolated into root-privileged paths without validation

- **Bug**: `cage -n ../../tmp/x` reached `compute_pidfile` and `set_appdir`,
  which build `/run/cage/${name}.pid` and `/var/lib/cage/app-${name}`, so the
  name created and unlinked files outside those bases — as root.
- **Root cause**: `handle_n` stored `optarg` verbatim. Nothing checked it,
  because both consumers treat the name as a single path element by assumption
  rather than by contract, and the assumption is invisible at the parse site.
- **Prevention**: `handle_n` now rejects an empty value or any `/`, which makes
  the name provably one path element (and therefore removes `..` as well).
  `docs/cli-arguments.md` documents the constraint; `test_option_n_rejects_path`
  and `test_option_n_rejects_empty` cover it. docs/qa.md §Domain-Specific
  Pitfalls records the class rule for future options.
- **Recurrence likelihood**: Medium — the same omission repeats for any new
  option whose value is concatenated into a filesystem path.

### A volume's container target escaped the sandbox root

- **Bug**: `cage -v /src:/../../etc` reached `mount_user_volume`, which joins
  the target onto the sandbox root with `mem_path`, so `create_guest` and
  `mount_bind_private` created and mounted outside the root.
- **Root cause**: `parse_vol` bounded the target's length but never inspected
  its content, and `mem_path` only strips leading and trailing slashes, so a
  `..` component survived the join.
- **Prevention**: `parse_vol` now rejects any `..` component
  (`target_escapes()`), which covers both the colon and the no-colon form.
  `docs/cli-arguments.md` documents the constraint beside `-v`; three unit
  tests cover leading, inner and bare `..`. The class rule lives in
  docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — the join helper itself stays permissive,
  so any future caller that skips validation repeats the escape.

### An environment-derived guest home escaped the sandbox root

- **Bug**: `cage -G -e HOME=../../root/.ssh ...` made `ensure_guest_home` and
  `prepare_cookie_dirs` create and chown a host directory outside the sandbox
  root, as root, before the chroot.
- **Root cause**: the guest paths arrive from the caller's environment and were
  joined with `mem_path`, which strips leading/trailing `/` but leaves `..`
  intact; no consumer re-checked containment because `mem_path` looked like the
  safe helper.
- **Prevention**: `mem_path_escapes()` now lives beside `mem_path` and rejects a
  `..` component; it guards the entry point (`setup_gui_mounts` on the
  env-derived home) and both consumers. Five unit tests in `tests/test_mem.c`
  and two in `tests/test_gui.c` cover it, and the symlink half of the same
  class is tracked as its own TODO (`src/util/io.c`, root-fd `O_NOFOLLOW`).
  docs/qa.md §Domain-Specific Pitfalls records the class rule.
- **Recurrence likelihood**: Medium — the environment stays an untrusted input
  everywhere the GUI context is built, and the guard is opt-in per call site.

### X11 socket ACLs were granted through a stat/setxattr race

- **Bug**: `acl_set()` did `sys_stat(path)` then `sys_setxattr(path, …)` on a
  socket under world-writable `/tmp/.X11-unix/`, so a local attacker could swap
  the socket for a symlink between the two calls and have root write a
  `system.posix_acl_access` xattr to an arbitrary file.
- **Root cause**: the path was re-resolved for the second call, and both calls
  follow symlinks. The code assumed the entry could not change because cage had
  just created it, but the directory is owned by mode-1777 semantics.
- **Prevention**: `acl_set()` now opens the target once with `O_PATH` and uses
  `fsetxattr` on that fd (`acl_set_fd()`); `SYS_OPEN_NOFOLLOW` additionally
  refuses a symlink at the final component. Two new tests
  (`test_acl_set_symlink_rejected`, `test_acl_set_fd_success`) cover it and
  docs/qa.md §Domain-Specific Pitfalls records the class rule.
- **Recurrence likelihood**: Medium — the same stat-then-act shape recurs for
  every metadata operation on a shared directory.

### The ext4 loopback mount point was trusted without checking for a symlink

- **Bug**: `ensure_ext4_mounted` `stat`ed `lp->loopdir` and then mounted the
  loop device onto that same path as root. A symlink planted at the derived
  loopdir (its parent is writable by the unprivileged appdirectories) redirected
  the privileged mount to an attacker-chosen target.
- **Root cause**: `stat` follows symlinks, so the check validated the target and
  not the path, and the path was re-resolved for the mount.
- **Prevention**: `require_real_dir()` now `lstat`s the mount point and refuses
  anything that is not a real directory before the mount; it guards both
  `ensure_ext4_mounted` and `use_provisioned_loopback`. Covered by
  `test_handle_same_fs_symlink_loopdir_fails` and the pitfall in docs/qa.md
  §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — mounting/create paths derived from user
  configuration recur across the sandbox setup.

### An out-of-range uid wrapped to root

- **Bug**: `cage -u 4294967296` silently produced uid 0 (and the same for the
  numeric `-g` group tokens), because `(uid_t) strtoul(...)` truncates the
  value to 32 bits and nothing checked `ERANGE`.
- **Root cause**: the raw `strtoul` result was cast straight to `uid_t`/`gid_t`
  with no bounds check; a very large input wrapped to a small valid id.
- **Prevention**: a checked `parse_id()` rejects `ERANGE`, trailing garbage and
  values above `UINT32_MAX`; `parse_one_token()` and `store_identity()` now
  return an error that propagates out of `user_new()` as `NULL`. Four unit
  tests cover the boundary (`test_uid_max_accepted` still passes) and docs/qa.md
  §Domain-Specific Pitfalls records the class rule.
- **Recurrence likelihood**: Medium — the truncating cast idiom is easy to
  reintroduce in any new id parser.

### A module path was interpolated into a shell command

- **Bug**: `decompress_xzstd_to_fd()` built `"%s \"%s\""` from the module path
  (derived from `modules.dep`) and ran it via `popen()`. A path containing a
  `"`, `$` or backtick executed arbitrary commands as root, since cage loads
  modules before dropping privileges.
- **Root cause**: the double-quote wrapper was treated as sufficient escaping,
  but a quote in the value closes it early and the shell interprets the rest.
- **Prevention**: `spawn_filter()`/`exec_filter()` now run `xzcat`/`zstdcat`
  through `fork`+`execlp` with an explicit `argv` and no shell. Covered by
  `test_load_module_path_xz_no_shell` (module name carries `"; touch …`, the
  sentinel must not exist) and the pitfall in docs/qa.md §Domain-Specific
  Pitfalls.
- **Recurrence likelihood**: Medium — any new shell-out reintroduces the bug.

### A `--` consumed as an option argument was mistaken for the separator

- **Bug**: `has_dashdash` was computed from `argv[optind - 1] == "--"`. For
  `cage -a --` (no command) that was true even though `--` was `-a`'s value, so
  the "no command specified" guard was skipped and the default shell ran.
- **Root cause**: the separator was inferred from a token position after the
  loop instead of from getopt's own step over the separator, and an option
  argument can occupy that same position.
- **Prevention**: `cage_parse_args()` now tracks `prev = optind` after each
  option and treats it as a separator only when the loop stopped one step later
  on the same token. Five `test_dash_dash_*` cases cover bare, after-flag,
  as-option-arg, and mixed forms; docs/qa.md §Domain-Specific Pitfalls records
  the rule.
- **Recurrence likelihood**: Medium — the post-hoc positional check is an easy
  habit to repeat in new parsers.

### An unvalidated SUDO_HOME fed the GUI bind mounts

- **Bug**: `resolve_host_home()` stored `SUDO_HOME`/`HOME` verbatim into
  `env->host_home`, which the GUI code uses as a bind-mount source and joins
  with `mem_path(host_home, "/.Xauthority")`. A hostile `SUDO_HOME=../../etc`
  (or a relative value) redirected those privileged operations.
- **Root cause**: the value is caller-controlled and was taken on trust, even
  though it must name a real directory to be usable.
- **Prevention**: `host_home_ok()` requires an absolute path, no `..` component
  (`mem_path_escapes`), and an existing directory; `resolve_host_home()` stores
  `NULL` otherwise, which safely disables the optional home mounts. Four tests
  in `tests/test_prep.c` cover the valid, `..`, relative and missing cases, and
  docs/qa.md §Domain-Specific Pitfalls extends the SUDO_HOME rule.
- **Recurrence likelihood**: Medium — the precedence rule is documented but the
  missing validation was not.

### getresuid/getresgid results were discarded before escalating

- **Bug**: `get_ids()` called `getresuid()`/`getresgid()` without checking the
  result, so a failure left the out-parameters uninitialized and fed
  `super_escalate()` an undefined uid/gid/real-uid triple.
- **Root cause**: the calls were assumed infallible, so the return was ignored.
- **Prevention**: `get_ids()` now checks both returns and aborts with a
  diagnostic; `main()` extracts the escalation into `escalate()` so the added
  branch does not push `main()` past the complexity budget. `main.c` has no
  unit-test seam, so this is recorded as a pitfall in docs/qa.md §Domain-Specific
  Pitfalls rather than covered by a test.
- **Recurrence likelihood**: Medium — the same dropped-return habit applies to
  the sibling `setreuid`/`setregid` fix tracked separately.

### setreuid/setregid results were ignored during self-heal

- **Bug**: `super_escalate()` reset the real uid/gid with `setreuid(0, 0)` and
  `setregid(0, 0)` without checking the results, then proceeded to `super_fix()`
  as if fully root even when the reset failed.
- **Root cause**: the reset was assumed to always succeed on the euid==0 path.
- **Prevention**: `drop_saved_ids()` checks each return and aborts with a
  diagnostic. Both calls now go through `sys_setreuid`/`sys_setregid`, mocked in
  `tests/mock_syscall.c`, with regression tests `escalate_setreuid_failure` and
  `escalate_setregid_failure` asserting the early failure and that no chown is
  attempted. docs/qa.md §Domain-Specific Pitfalls documents the rule.
- **Recurrence likelihood**: Medium — the failure paths are rarely exercised in
  normal development.

### Out-of-range PIDs from a pidfile wrapped when narrowed

- **Bug**: `parse_pid()` accepted any `strtol` result below `ERANGE` and
  `readpid()` returned `(pid_t) pid`. A pidfile containing `0`, a negative
  value, or a value above `INT_MAX` (wrapping on 32-bit) reached callers that
  treated `pid >= 0` as valid and called `kill(0, ...)`, signalling the whole
  process group.
- **Root cause**: the parser only guarded against `ERANGE` and non-numeric
  tails, not against a zero/negative/oversized value, and the narrowing cast
  was unchecked.
- **Prevention**: `parse_pid()` now rejects `pid <= 0 || pid > INT_MAX`;
  regression tests `readpid_zero_rejected`, `readpid_negative_rejected` and
  `readpid_overflow_rejected` in `tests/test_proc.c`, plus a docs/qa.md
  §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — pidfile content is otherwise trusted.

### Symlink in a root-writable appdir redirected pre-chroot creates

- **Bug**: the GUI pre-chroot helpers (`mkparent`, `mkdir_parents_own`,
  `io_touch`, `ensure_guest_home`, `prepare_cookie_dirs`) created guest
  directories and touched files by absolute path with no `O_NOFOLLOW`. A symlink
  planted at any component of the path (the appdir and its ancestors are
  root-writable and caller-influenced) made the privileged `mkdir`/`chown`/
  `open` operate outside the sandbox root.
- **Root cause**: lexical containment under the root path string was assumed to
  be sufficient; path-based metadata operations follow symlinks.
- **Prevention**: added fd-relative `io_mkdir_own_at()`/`io_touch_at()`
  (`src/util/io_at.c`) that walk from an `O_NOFOLLOW|O_DIRECTORY` fd on the
  sandbox root, rejecting symlinks and non-directories at every component, and
  rewired `src/sandbox/gui.c` to them; the path-based variants were removed.
  New syscall wrappers `sys_openat`/`sys_mkdirat`/`sys_fstatat`/`sys_fchownat`,
  with `sys_fchownat` mocked for the chown-failure path. Covered by
  `mkdir_own_at_nested`, `touch_at_creates_file`,
  `mkdir_own_at_rejects_symlink`, `touch_at_rejects_symlink`,
  `touch_at_rejects_symlink_leaf`, `setup_gui_home_symlink_rejected`, and a
  docs/qa.md §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — any future privileged create under a
  caller-influenced path repeats the escape if done by path string.

### Samefs loopback triggered for a sibling or separately-mounted appdir

- **Bug**: the ext4-loopback relocation fired whenever the lowerdir and the
  appdir's grandparent shared an `st_dev`, so `-l /srv/lower -a /srv/app`
  (sibling dirs on one filesystem) and `-l /srv/lower -a /srv/lower/app` where
  `/srv/lower/app` is its own mount both relocated the appdir to
  `/srv/app-loop/...`, moving the user's path and creating a loopback image
  that was never needed.
- **Root cause**: `same_fs_detect()` compared the lowerdir device with
  `grandparent(upperdir)` and treated a matching device as sufficient. It never
  checked whether the appdir was actually inside the lowerdir tree, and it
  compared the wrong path (the grandparent, not the appdir's closest existing
  ancestor), so an appdir on a distinct mount under the lower tree still
  matched the lowerdir's filesystem indirectly.
- **Prevention**: detection now resolves the appdir's closest existing
  ancestor, requires it to be the lowerdir or under it (`path_is_parent`) for
  the same-filesystem rule, and additionally forces a loopback when a
  non-ancestor appdir is itself on an overlayfs (overlay-on-overlay cannot
  nest). lifecycle.md §Samefs / Ext4 Loopback Flow rule 1 records the
  ancestry + device + overlayfs rule. Covered by
  `test_same_fs_detect_lower_not_parent`,
  `test_same_fs_detect_lower_parent_same_fs` and the `e2e/appdir.sh`
  sibling / own-mount / empty-lower / full-lower cases.
- **Recurrence likelihood**: Medium — the loopback path still runs to
  completion, so the extra relocation looks like normal behaviour.

### cgroup controller padding overflowed the 256-byte callers

- **Bug**: `build_subtree_value` wrote two bytes per input byte (a space and a
  `+`) but guarded with `len + 2 >= 256`, bounding the *input* size. A
  `cgroup.controllers` line of 253 bytes produced up to 508 bytes and overran
  the callers' `char val[256]` (`src/sandbox/cgroup.c`,
  `src/sandbox/cgroup_setup.c`) on the stack.
- **Root cause**: the bound was written against the input length instead of the
  worst-case expanded output length, and the helper had no capacity argument.
- **Prevention**: `build_subtree_value` now takes a `cap` and rejects when
  `2 * len + 2 > cap`; both call sites pass `sizeof(val)`. Regression tests
  `build_subtree_all_spaces_fits` and `build_subtree_overflow_rejected`, plus a
  docs/qa.md §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — expand-then-write helpers are an easy
  place to bound the wrong length.

### io_read returned unterminated buffers read as C strings

- **Bug**: `io_read` filled a `buf_t` with no trailing NUL, but callers passed
  `b.data` to string scanners (`has_real_ns_in_content`, `strchr`, `strstr`) that
  read until a NUL byte. A file whose content filled the allocation drove those
  scans past `b->len` into uninitialized heap.
- **Root cause**: the read path documented a length-delimited contract
  (`b->len`), while its consumers assumed a C string; two callers papered over
  it locally with their own grow-and-terminate.
- **Prevention**: `read_file` now grows by one and writes `b->data[b->len] =
  '\0'` before returning, so every `io_read` result is a valid C string; the
  redundant local terminators in `pid_zombie`/`env_read_pid_environ` are gone.
  Assertions added to `read_small` and `read_empty`, plus a docs/qa.md
  §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — mixing length-delimited and C-string
  contracts on the same buffer is easy to repeat.

### errno read after a successful loop-stat call

- **Bug**: `scan_loop_devices` guarded with `sys_loop_get_stat(fd) == 0 ||
  errno == ENXIO` and then re-read `errno == ENXIO` to call the device free. A
  stale `ENXIO` from an earlier failing call made a busy device look free.
- **Root cause**: `errno` was consulted regardless of whether the call had
  failed; it is only defined after a failure.
- **Prevention**: branch on the return value first — `sys_loop_get_stat(fd) < 0
  && errno == ENXIO`. Regression test `scan_loop_devices_stale_errno` sets a
  successful return with `errno = ENXIO` and asserts the device is not claimed,
  plus a docs/qa.md §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — sticky `errno` reads are easy to write.

### Netlink ACK decoded without validating the reply length

- **Bug**: `netlink_send_ack` cast the recv buffer to `struct nlmsghdr` and read
  `nlmsg_type` and `nlmsgerr.error` without checking how many bytes arrived, and
  the `nft` drain helper `nft_ack_errno` did the same. A short or leftover reply
  read uninitialized stack.
- **Root cause**: the reply is length-delimited (`alen`) but the code treated
  the buffer as if a full header/error were always present.
- **Prevention**: `netlink_ack_result(ack, alen)` rejects `alen < NLMSG_HDRLEN`,
  requires `NLMSG_OK`, and checks `nlmsg_len` covers `struct nlmsgerr` before
  reading `.error`; `nft_ack_errno` takes the length and validates likewise.
  Tests `ack_result_short`, `ack_result_truncated_error`, `ack_result_ok`,
  `ack_result_err`, `ack_result_non_error`, plus a docs/qa.md
  §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — kernel replies are attacker-adjacent input
  and the cast is easy to trust.

### gzip header walker advanced past the buffer end

- **Bug**: `skip_fextra` added `2 + XLEN` with no bound, `skip_string` did
  `(*o)++` even when it stopped at the end without finding a NUL, and the FHCRC
  skip added 2 unchecked. A crafted gzip member drove the header offset past
  `in_len` so the following reads ran out of bounds.
- **Root cause**: each optional-field skip advanced the offset independently and
  only some checked the two-byte prefix, not the full field length.
- **Prevention**: all advances go through one `advance(o, n, in_len)` helper;
  `skip_string` consumes the NUL only when found and returns -1 otherwise.
  Regression tests `decompress_gzip_extra_truncated`,
  `decompress_gzip_name_unterminated`, `decompress_gzip_hcrc_truncated`, plus a
  docs/qa.md §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — an offset-based parser adds a new unchecked
  step for every field.

### Address prefix of 0 or below shifted by 32 or more

- **Bug**: `fill_addr_attrs` guarded only `prefix >= 31` before computing the
  broadcast mask `1u << (32 - prefix)`. `prefix == 0` shifted by 32 and a
  negative prefix shifted by more than the type width — both undefined
  behaviour, reachable from a parsed address prefix.
- **Root cause**: the guard covered the high end of the prefix range but not
  zero or negatives.
- **Prevention**: the guard is now `prefix <= 0 || prefix >= 31`, skipping the
  broadcast attribute for the unusable range. Regression tests
  `fill_addr_zero_prefix` and `fill_addr_negative_prefix`, plus a docs/qa.md
  §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — shift widths derived from parsed input.

### Child exit status read uninitialized when the wait reported no child

- **Bug**: `pidwait` returns 0 on `ECHILD` without writing its `status` out
  parameter. Callers declared `int status;` and decoded it anyway
  (`exit_code_from_status(status)` in `src/core/cage.c:207`, `st == 0` in
  `src/core/prep.c:100`), and `src/core/super.c:71` read `status` after a bare
  `waitpid`. An uninitialized stack read decided the exit code.
- **Root cause**: the wait helper's "nothing to report" success path was treated
  identically to a real wait result.
- **Prevention**: initialise `status = 0`, check the wait return before
  decoding, and retry on `EINTR` (`pidwait` loops; `super.c` uses `wait_exit_ok`).
  `test_pidwait_echild` asserts `pidwait` leaves a sentinel status untouched,
  plus a docs/qa.md §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — a success return without a status write is
  easy to decode blindly.

### resolve_script reported success for a truncated path

- **Bug**: `resolve_script` checked the untruncated `script` with `access()` but
  copied it into a caller buffer with `snprintf(buf, size, "%s", script)` and
  ignored the return. A script path longer than `size` left a truncated prefix in
  `buf` while the function returned success, so the caller exec'd the wrong path.
- **Root cause**: the truncation result of `snprintf` was discarded, and the
  existence check ran against a different string than the one copied.
- **Prevention**: reject when `snprintf` returns negative or `>= size`.
  Regression test `resolve_script_truncated`, plus a docs/qa.md
  §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — silent `snprintf` truncation.

### appdir join failures were silently ignored

- **Bug**: `join()` in `src/sandbox/appdir.c` returned without writing `dst` when
  the joined path did not fit, and `set_dir()` could not see the failure and
  still returned 0. `resolve_appdir` then returned success with empty
  `upperdir`/`workdir`, which the overlay mount used.
- **Root cause**: a `void` builder encoded failure as "output unchanged", which
  the caller could not distinguish from a valid empty result.
- **Prevention**: `join()` returns -1 on overflow, `set_dir()` propagates it, and
  `resolve_appdir()` checks each call. Regression test `join_overflow_rejected`,
  plus a docs/qa.md §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — silent builder failure.

### XDG paths truncated into fixed buffers

- **Bug**: `gui_xdg.c` formatted `guest_xrd` and the Wayland socket name with
  `snprintf` into fixed buffers and ignored the return. A long
  `XDG_RUNTIME_DIR` (or Wayland name) truncated silently, and the truncated
  prefix was used as an mkdir/bind target.
- **Root cause**: `snprintf` truncation is silent and its return value was
  discarded on user-influenced values.
- **Prevention**: a `fits(n, sz)` helper guards every copy; `fill_xdg`,
  `ensure_wayland_target` and `pulse_target` fail on truncation. Regression test
  `setup_gui_xdg_truncated`, plus a docs/qa.md §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — fixed-buffer copies of env values.

### nft batch appended into a fixed buffer unchecked

- **Bug**: `src/net/nft.c` `nft_batch_msg` copied a message into the fixed
  768-byte `struct nft_batch` with no capacity check, and the builders could not
  report a failure.
- **Root cause**: the append helper had no bound and returned `void`, so an
  oversized batch would silently overflow the stack buffer.
- **Prevention**: `nft_batch_msg`/`nft_batch_end` bound-check against
  `sizeof(b->data)` and the builders return `int`; `nft_build`/`nft_setup_family`
  propagate the failure. Defensive bound noted in docs/qa.md
  §Domain-Specific Pitfalls (not reachable by current static content).
- **Recurrence likelihood**: Medium — unbounded append into a fixed buffer.

### netlink_nest_start wrote an nlattr without a capacity check

- **Bug**: `src/net/netlink.c` `netlink_nest_start` wrote an `nlattr` header at
  `buf + *off` with no `cap` argument (its sibling `netlink_put_string` enforced
  `cap`), and `fill_veth_peer` then `memset` an `ifinfomsg` with no bound, so a
  small buffer overflowed.
- **Root cause**: an attribute-writing helper lacked the capacity contract its
  siblings had.
- **Prevention**: `netlink_nest_start` takes `cap`, returns `-1` when the header
  does not fit and writes the start offset to an out-param; `rtnl.c` callers
  propagate it and the `ifinfomsg` write moved behind `put_ifinfomsg_zero`
  (capacity-checked). Regression test `fill_create_veth_nest_no_room`, plus a
  docs/qa.md §Domain-Specific Pitfalls entry.
- **Recurrence likelihood**: Medium — sibling helpers with inconsistent `cap`.

### cgroup.procs was created with O_CREAT and the write result was unchecked

- **Bug**: `write_pid` (`src/sandbox/cgroup.c`) opened `cgroup.procs` with
  `O_WRONLY | O_CREAT, 0644` and wrote the PID with an unchecked `dprintf`, so
  both the enter and cleanup paths reported success when the write failed or
  was short; `write_procs` in `src/sandbox/cgroup_setup.c` duplicated the same
  unchecked open/write pair.
- **Root cause**: the code treated a kernel-managed file as an ordinary file to
  be created. On a real cgroupfs the file exists, so the bug is invisible in
  production until a path or mount is wrong: the missing file is silently
  created as a regular one and the process is never actually placed in the
  cgroup. `dprintf`'s return value was discarded, so a short write looked like
  success too.
- **Prevention**: `write_pid` opens with `O_WRONLY` only, formats into a stack
  buffer, checks `snprintf` and `write`, and returns `-1` on any failure;
  `write_procs` calls `write_pid` instead of duplicating the logic. Covered by
  `write_pid_missing_rejected` and `write_pid_success` (which now seeds the
  file first); all other cgroup tests seed `cgroup.procs` explicitly, matching
  what the kernel provides. Pitfall recorded in docs/qa.md
  §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — `O_CREAT` is the reflex for "write a
  file", and unchecked write returns recur with every new writer.

### X11 socket ACLs were written to an O_PATH fd and always failed

- **Bug**: `acl_set_fd` opened the socket with `O_PATH|O_NOFOLLOW` and called
  `fsetxattr`. Any socket that actually needed an ACL (owned by root or by a
  different user) made GUI setup abort with `cage: GUI setup failed`; sockets
  already writable by the guest took the skip path, so the failure was
  invisible in the common same-user case.
- **Root cause**: `fsetxattr` returns `EBADF` on an `O_PATH` fd, and unix
  sockets cannot be opened any other way (`O_RDONLY`/`O_RDWR` return `ENXIO`).
  The fd-based ACL fix (985b18d) therefore removed the TOCTOU but also removed
  every real ACL write; the unit tests mock `sys_fsetxattr`, so only the e2e
  suite noticed.
- **Prevention**: the target is still opened `O_PATH|O_NOFOLLOW`, but the xattr
  is written through `/proc/self/fd/<fd>` with `sys_setxattr`, which resolves
  to the pinned inode. `e2e/gui_x11.sh` g4 and g6 cover the other-owner and
  root-owned socket cases; pitfall recorded in docs/qa.md §Domain-Specific
  Pitfalls.
- **Recurrence likelihood**: Medium — `O_PATH` accepts only a narrow set of
  syscalls and the mistake is invisible when a mock replaces the real call.

### Init-script-created users and groups were resolved before the script ran

- **Bug**: `cage -i <script> -u carol` ran the command as `nobody` and
  `-g gonly`/`-g 4500:gcomp` ignored groups created by the init script.
- **Root cause**: user resolution moved into `do_child_setup`, which runs
  before `enter_cgroup_and_init` after the commit that moved the init script
  after chroot/cgroup/net setup. The guest `user_t` was eagerly resolved and
  cached before `adduser`/`addgroup` had modified the overlay, and the same
  instance was then used by `child_run`. The lifecycle spec requires a fresh
  instance after `--initscript`.
- **Prevention**: `reread_guest_user` builds a new `user_t` rooted at `/` and
  eagerly resolves it after the init script completes; `child_run` uses that
  instance. Covered by `e2e/identity.sh` n4/n5/n6; pitfall recorded in
  docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — moving setup steps across the init
  script boundary silently revives the stale cache.

### Nested guest HOME chowned every created parent to the guest

- **Bug**: `cage -G -e HOME=/a/b/c` created `/a` and `/a/b` owned by the guest
  uid instead of root (e2e pc12), letting the guest rename or replace its own
  ancestors.
- **Root cause**: the fd-relative refactor (a0a0365) replaced `mkparent`
  (intermediates `0:0`, leaf `uid:gid`) with `io_mkdir_own_at`, which chowns
  every component it creates. The same change also affected bind-mount parent
  creation.
- **Prevention**: `io_mkdir_parents_at` restores the split ownership
  (intermediates `0:0 0755`, leaf `uid:gid 0755`) and is used by
  `ensure_guest_home` and `ensure_mount_target`; `prepare_cookie_dirs` keeps
  `io_mkdir_own_at` for `.config`/`.config/pulse`. Covered by
  `mkdir_parents_at_chowns_only_leaf` and `e2e/pulse.sh` pc12; pitfall
  recorded in docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — two mkdir helpers with different
  ownership rules are easy to confuse.

### `killpid` swallowed SIGTERM and wait failures

- **Bug**: `cage kill` removed the pidfile and reported success even when
  `SIGTERM` failed with `EPERM`/`EINVAL` or the graceful wait failed, leaving
  the sandbox process alive while the caller believed it had stopped.
- **Root cause**: `killpid` called `kill_gracefully(pid)` for its side effect
  and discarded the return, then unconditionally returned 0.
- **Prevention**: `killpid` checks `kill_gracefully` and returns -1 on failure
  (only `ESRCH` stays success); `kill_gracefully` signals through the mockable
  `sys_kill` seam and `test_killpid_kill_fails` forces `EPERM`. Pitfall recorded
  in docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — teardown helpers whose errors look
  harmless are the ones most often called without checking the return.

### `-N` parent hung forever when the child died before the net handshake

- **Bug**: with `-N`, a child that exited before `do_child_net` (unshare,
  mount or init-script failure) left the parent blocked in
  `sys_futex_wait(net_futex, 0)` forever, so `cage` never reported the failure.
- **Root cause**: the parent-side net wait used an untimed futex wait, unlike
  `wait_root_ready` which polls the child with `waitid(WNOHANG|WNOWAIT)` and a
  20 ms timeout.
- **Prevention**: both handshakes share one timed `wait_flag` loop that peeks
  at the child each iteration and returns on child death; the parent then skips
  the veth work and `finish_child` reaps the child and reports the status.
  Pitfall recorded in docs/qa.md §Domain-Specific Pitfalls and the rule in
  docs/lifecycle.md §Network Namespace Setup.
- **Recurrence likelihood**: Medium — new cross-process handshakes tend to be
  written as a bare futex wait.

### Computed pidfile leaked when `create_pidfile` failed

- **Bug**: `spawn_init` allocated `free_pidfile` via `do_early_setup`, then
  returned on `create_pidfile < 0` without freeing it; `spawn_cage` also
  returned without freeing it when `spawn_init` failed.
- **Root cause**: the early-return path bypassed `spawn_cleanup`, and the
  allocation was not owned by any single cleanup point.
- **Prevention**: `spawn_st_init` NULLs `pidfile`/`free_pidfile`,
  `spawn_init` frees and NULLs it before the `create_pidfile` failure return,
  and `spawn_cage` frees it defensively; `spawn_cleanup` is deliberately not
  used because it would unlink another instance's pidfile. Pitfall recorded in
  docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — early returns between an allocation and
  the cleanup block reopen the leak.

### Double free of free_pidfile on spawn failure paths

- **Bug**: `spawn_init` allocated `free_pidfile` via `do_early_setup`, freed
  it on `create_pidfile` failure before `goto fail` (and `fail` calls
  `spawn_cleanup` which also freed it), and `spawn_cage` also freed it after
  `spawn_init` returned failure. A segfault occurred on the error path when
  custom `-w` outside the derived loopdir triggered a fatal error in
  samefs setup.
- **Root cause**: multiple cleanup points (the early-return path, the shared
  `spawn_cleanup`, and the caller `spawn_cage`) each freed `free_pidfile`
  without nulling or coordinating ownership. Any failure path between
  allocation and full ownership transfer risked double frees.
- **Prevention**: `spawn_cleanup` now NULLs `free_pidfile` and
  `env.host_xdg_runtime_dir` after freeing; `spawn_init` NULLs `free_pidfile`
  on the `create_pidfile` error path; callers avoid double-free by treating
  ownership as transferred to the cleanup path. Pitfall recorded in
  docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — early returns between allocation and
  shared cleanup blocks commonly reintroduce double frees in complex
  teardown flows.


### `io_rmtree` could recurse into a truncated child path

- **Bug**: a directory entry whose `"%s/%s"` child path did not fit the
  4096-byte stack buffer was silently truncated, and `io_rmtree` recursed
  into the truncated path — potentially deleting a different entry — while
  ignoring recursive failures.
- **Root cause**: `rmtree_dir` used `snprintf` without checking the result and
  `io_rmtree` discarded the recursion return value.
- **Prevention**: `rmtree_dir` detects truncation, reports it and returns -1;
  `io_rmtree` propagates that failure before `rmdir`. Pitfall recorded in
  docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — path-joining `snprintf` calls in
  recursive walks are easy to leave unchecked.

### Failed ACL grant on a GUI socket was silently ignored

- **Bug**: `mount_file_acl` returned 0 after `acl_guest_set` failed, so a
  bind-mounted X11/Pulse socket stayed inaccessible to the guest while cage
  reported a successful GUI setup.
- **Root cause**: `acl_guest_set` was called for its side effect and its return
  value was discarded.
- **Prevention**: `mount_file_acl` propagates the ACL result and logs the
  target; `mount_file_acl_propagates_failure` forces `sys_setxattr` failure and
  asserts -1. Pitfall recorded in docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — ACL/chown grants get treated as
  best-effort even when the guest depends on them.

### Interrupted ext4 image build was reused as if complete

- **Bug**: if cage was killed while writing `${loopdir}.img`, the next run saw
  `EEXIST`, accepted the truncated image and tried to mount it, failing with an
  unrelated loopback error.
- **Root cause**: the image was written in place and creation treated
  `EEXIST` as success without validating the existing file.
- **Prevention**: `build_ext4_img` creates `${img}.tmp.<pid>` and renames it
  into place; `setup_ext4_img` reuses an existing image only when
  `ext4_image_valid()` confirms the exact size and ext4 superblock magic, and
  otherwise rebuilds it. `test_ext4_image_valid` covers valid, missing and
  truncated images. Pitfall recorded in docs/qa.md §Domain-Specific Pitfalls
  and the flow in docs/lifecycle.md §Samefs / Ext4 Loopback Flow.
- **Recurrence likelihood**: Medium — in-place artifact creation with
  `EEXIST`-as-success recurs whenever a persistent cache is added.

### Double free of free_pidfile on spawn failure paths

- **Bug**: `spawn_init` allocated `free_pidfile` via `do_early_setup`, freed
  it on `create_pidfile` failure before `goto fail` (and `fail` calls
  `spawn_cleanup` which also freed it), and `spawn_cage` also freed it after
  `spawn_init` returned failure. A segfault occurred on the error path when
  custom `-w` outside the derived loopdir triggered a fatal error in
  samefs setup.
- **Root cause**: multiple cleanup points (the early-return path, the shared
  `spawn_cleanup`, and the caller `spawn_cage`) each freed `free_pidfile`
  without nulling or coordinating ownership. Any failure path between
  allocation and full ownership transfer risked double frees.
- **Prevention**: `spawn_cleanup` now NULLs `free_pidfile` and
  `env.host_xdg_runtime_dir` after freeing; `spawn_init` NULLs `free_pidfile`
  on the `create_pidfile` error path; callers avoid double-free by treating
  ownership as transferred to the cleanup path. Pitfall recorded in
  docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — early returns between allocation and
  shared cleanup blocks commonly reintroduce double frees in complex
  teardown flows.

### -N veth IDs were never reused after a session exited

- **Bug**: with `-N`, parallel sessions got sequential veth IDs (rp0=0,
  rp1=1), but after killing rp0 the next session rp2 received id 2 instead of
  reusing id 0, so its sandbox IP skipped the freed `/31` block. The
  `e2e/network.sh` "reuse freed ID after parallel" check failed.
- **Root cause**: `netlink_alloc_veth_id` took the maximum existing
  `cage-vethN` and returned `max + 1`; it never looked for gaps below the
  maximum, so a freed low ID was unreachable.
- **Prevention**: `netlink_lowest_free_veth_id` records every existing ID in a
  bitmap and returns the lowest free index (fails when all
  `CAGE_VETH_MAX_ID + 1` slots are occupied). Unit tests cover a gap
  (`veth0`+`veth2` → 1), a non-matching prefix, and full exhaustion; the e2e
  reuse case verifies rp2 takes rp0's address. Pitfall recorded in
  docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — extending past the maximum is the
  obvious allocator implementation, and gaps are invisible until churn.

### Pidfile removed before the target process was confirmed dead

- **Bug**: `cage -k` unlinked the pidfile before waiting for the sandbox to
  exit, so `e2e_wait_session_gone` saw the pidfile disappear while the old
  netns (and its veth) was still alive. In the `-N` reuse e2e a new session
  could then pick a different veth ID, and a concurrent allocation could reuse
  an ID still held by the dying session.
- **Root cause**: `kill_pidfile` called `io_unlink_quiet` before
  `stop_target`, making pidfile absence mean "kill started", not "process
  gone".
- **Prevention**: `kill_pidfile` now closes the locked fd, stops/waits for the
  process, and only then unlinks the pidfile. The e2e also waits for
  `cage-veth*` links to drain before the reuse assertions. Pitfall recorded in
  docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — bookkeeping is commonly removed before
  the potentially slow shutdown completes.

### GUI setup failed after creating a nested XDG directory

- **Bug**: `-G` sessions with a default PulseAudio socket failed with
  `cage: GUI setup failed` (pulse e2e, acl pulse check). `io_mkdir_own()`
  creates missing parents through `mkdir_chown()`, but that helper never
  invalidated the `io_exists()` cache. The parent was cached as missing during
  the pre-create probe, so creating `.../run/user/1000/pulse` re-checked
  `.../run/user/1000`, saw the stale "missing", and called `mkdir()` again,
  which failed with `EEXIST`.
- **Root cause**: create helpers mutated the filesystem without invalidating the
  per-process existence cache; only `io_mkdir`/`io_touch`/`io_unlink` did.
- **Prevention**: `mkdir_chown`, `io_mkdtmp` and `io_symlink` now invalidate the
  created path. Added `test_mkdir_own_after_missing_cache` in `tests/test_io.c`
  and recorded the pitfall in docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: High — any new create helper must pair with cache
  invalidation, and the failure only appears with nested creation.

### cgroup namespace unshared before fork leaked cage-<pid> on every -C run

- **Bug**: `e2e/cgroup.sh` found `/sys/fs/cgroup/cage-<pid>` after each `-C`
  run, one more per run. Cleanup also wrote the PID back into
  `cage-<pid>/cgroup.procs` (a no-op) instead of the parent cgroup.
- **Root cause**: the parent unshared `CLONE_NEWCGROUP` in `ns_setup()`, so its
  cgroup namespace root became `cage-<pid>`; `${path}/cage-<pid>` was no longer
  visible and cleanup early-returned. The earlier cleanup fix had also changed
  the `cgroup.procs` path from `root` to `cgdir`.
- **Prevention**: `CLONE_NEWCGROUP` is now folded into the child's single
  `CLONE_NEWNS` unshare before cgroupfs is mounted, so the parent stays in the
  host cgroup namespace. Cleanup writes the parent PID to
  `${root}/cgroup.procs` again. Unit tests assert the PID lands in the parent
  `cgroup.procs` and a nested `cage-parent` is removed; e2e cgroup leak check
  passes. Pitfall recorded in docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — namespace ownership across the fork is
  easy to move to the wrong side, and cgroupfs then hides the cleanup target.

### -N broke the host's IPv6 connectivity

- **Bug**: after `cage -N` the host lost its router-learned IPv6 default
  routes (evidence `.tmp/ip6/before` vs `.tmp/ip6/after`): the `default via
  fe80::...` entries vanished while the on-link `/64` routes remained, so
  host IPv6 egress stopped until the next unsolicited RA.
- **Root cause**: cage enabled IPv6 forwarding with
  `conf/all/forwarding=1`. That calls `rt6_purge_dflt_routers()`, which
  deletes RA-learned default routes on every interface whose
  `accept_ra != 2`. The `all/accept_ra` writes added alongside did not
  propagate to existing interfaces, so the purge removed the host routes.
- **Prevention**: enable forwarding through the per-interface
  `force_forwarding` knob (`all` + `default`), which leaves `cnf.forwarding`
  and RA processing untouched. On kernels without `force_forwarding`, raise
  `accept_ra` to 2 on every interface before enabling global forwarding and
  abort if any raise fails. `e2e/network.sh` now asserts `-N` leaves
  `all/forwarding=0` and sets both force_forwarding knobs; pitfall recorded in
  docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: High — kernel forwarding semantics are
  non-obvious and a global `all/forwarding` write looks like the natural fix.

### same_fs flake: fixed sleep and stacked tmpfs mount

- **Bug**: in full-suite runs `e2e/same_fs.sh` intermittently failed three of
  its four checks ("same-fs loopback message", "ext4 loop mounted mid-run",
  "appdir relocated onto the loopback") with an empty `/tmp/d1.log`, while the
  same suite passed standalone and on repeat.
- **Root cause**: the test waited a fixed 2s for asynchronous setup, so under
  load cage had not reached the same-fs path before the log, mount and appdir
  were inspected. A single `umount /var/lib/cage` also leaves a hidden tmpfs
  layer behind if the fixture ever stacks; the same-fs condition then never
  triggers (two tmpfs entries were observed after a failure) and cleanup's
  unconditional mount adds yet another layer, poisoning later runs.
- **Prevention**: poll up to 10s for `/var/lib/cage-loop/app-d1` instead of
  sleeping; unmount every `/var/lib/cage` layer in a loop and fail fast if the
  path stays mounted; unmount before restoring the tmpfs. Full
  `e2e/_all.sh` passes with `same_fs` 4/0. Pitfall recorded in docs/qa.md
  §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — fixed sleeps and single-layer unmounts
  look correct until the suite runs under load or after an interrupted run.

### unprivileged no-sudo path segfaulted instead of failing cleanly

- **Bug**: `e2e/unprivileged.sh` failed four checks. With cage not setuid and
  sudo absent, an unprivileged run exited 139 (SIGSEGV) instead of rc 1, so
  stderr was empty and the expected `sudo` hint never appeared. The sudo
  repair case also returned 127.
- **Root cause**: `try_sudo_reexec()` used `vfork()`. Its child closed stdio,
  failed `execlp("sudo", ...)`, and called `_exit(127)`; on resume the parent
  jumped into corrupted startup code and segfaulted before printing the
  failure. The static-PIE musl build made the shared-stack failure visible.
- **Prevention**: the sudo child now uses `sys_fork()`; the parent reliably
  reports `cage: sudo --fix failed`. `e2e/unprivileged.sh` passes 8/0, and the
  pitfall is recorded in docs/qa.md §Domain-Specific Pitfalls.
- **Recurrence likelihood**: Medium — vfork looks like the natural optimization
  before exec, and mocked unit tests do not exercise exec failure.

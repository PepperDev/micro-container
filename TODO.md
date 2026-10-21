# TODO

## Backlog

- [ ] Mount recursive overlays for non-system partitions (see [docs/lifecycle.md §Container Lifecycle](docs/lifecycle.md#container-lifecycle))
- [ ] Give the e2e docker host an IPv6 upstream so the `-N` IPv6 internet check in `e2e/network.sh` passes (`wget -qO- http://api6.ipify.org`). The `-N` sandbox now has IPv6 addressing, a default route, IPv6 forwarding and NAT66, but the e2e daemon runs in a QEMU slirp VM with no IPv6 forwarding (`docker run --net=host alpine wget -qO- http://api6.ipify.org` fails).

## Correctness

- [ ] `src/core/proc.c:295` — `if (!pid_zombie(pid)) kill_gracefully(pid);` discards the return, so `killpid` reports success when SIGTERM or the wait failed with anything other than `ESRCH`. Fix: propagate the error instead of falling through to `return 0`.

## Reliability

- [ ] `src/core/cage.c:63` — `if (*net_futex != 1) sys_futex_wait(net_futex, 0);` waits forever with no timeout and no child-death check, so with `-N` any child that exits before `do_child_net` (`unshare` failure, mount failure, init-script failure) hangs the parent permanently, unlike `wait_root_ready` (`src/core/cage.c:191`) which polls `waitid(WNOHANG|WNOWAIT)`. Fix: use the same timed-futex plus child-death loop.
- [ ] `src/core/cage.c:251` — `st->pidfd = create_pidfile(st->pidfile); if (st->pidfd < 0) return -1;` returns without freeing `st->free_pidfile` (allocated when `-p` is absent), and `spawn_cage` has the same leak. Fix: free `st->free_pidfile` on those paths, without reusing `spawn_cleanup`, which would also unlink the other instance's pidfile.
- [ ] `src/util/io.c:78` — `snprintf(child, sizeof(child), "%s/%s", path, e->d_name)` into a fixed `char child[4096]` with no truncation check, so `io_rmtree` can recurse into a truncated path and delete the wrong entry. Fix: fail (or use a dynamic buffer) when the result truncates.
- [ ] `src/sandbox/gui.c:63` — `acl_guest_set(absp, ...)` discards its return, so a failed ACL grant on a bind-mounted X11/Pulse socket leaves it inaccessible while the run proceeds as if it succeeded. Fix: check the return and fail `mount_file_acl()`.
- [ ] `src/sandbox/ext4fix.c:192` — `if (ext4_create_img(img) != 0 && errno != EEXIST)` accepts any pre-existing image even though the image is written in place with `O_CREAT|O_EXCL`, so an interrupted run's truncated image is silently reused. Fix: build the image in a temporary file beside the target and `rename()` it over, or validate an existing image before honouring `EEXIST`.

## Structure

- [ ] `src/sandbox/nssetup.c:5` — the lower `sandbox` layer includes `core/proc.h` and `core/prep.h` and calls `pidfork`, `do_child_net`, `do_init`, `child_setup_gui`, `child_root_ready`, `child_run`, inverting the declared `core -> sandbox` direction. Fix: move the child-side entry points into `sandbox` (or a dedicated child-entry unit) so `nssetup.c` no longer includes `core/*`.
- [ ] `src/core/prep.c:41` — one translation unit owns five jobs: init-script resolution/execution, GUI context assembly and child GUI entry points, the child net and root-ready handshakes, the child launch shim, and parent-side early/root prep with pidfile resolution. Fix: split the child-side entry points into their own unit (which also removes the `sandbox -> core` include) and leave `prep.c` for parent orchestration.
- [ ] `src/core/launch.c:43` (also `src/core/prep.c:82`, `src/core/prep.c:87`, `src/sandbox/nssetup.c:76`, `src/sandbox/nssetup.c:81`, `src/sandbox/nssetup.c:110`) — every `get_home`/`get_uid` call site casts away `const` because the `user` getters are non-const lazy resolvers, so the type system can no longer stop mutation of borrowed objects. Fix: make the getters take `const user_t *` with a mutable resolver cache and drop the casts.
- [ ] `src/cli/args.h:26` — `char *inst_env;` is transient per-run state smuggled through the parsed `cage_config_t`, written and freed by `spawn_existing` (`src/core/reuse.c:59`) and consumed by `child_gui_env_only` (`src/core/prep.c:209`) as if it were config. Fix: pass the environ blob as an explicit parameter and delete the field.
- [ ] `src/core/prep.c:249` — `cfg->pidfile = pf;` writes a computed value back into the caller's config although it is already returned through `pi->pidfile`, and nothing reads it afterwards, which forces a non-const `cage_config_t *` through `do_early_setup` and `spawn_cage`. Fix: delete the assignment and make those parameters `const cage_config_t *`.
- [ ] `src/core/launch.c:43` — `build_env_array` is a pass-through wrapper around `fill_env` (whose only caller is `build_env_array`), and `shell_from_env_shell` (`src/core/launch.c:65`) is a single-use one-liner. Fix: inline all three into their call sites.
- [ ] `src/core/launch.c:88` — `setup_command` mutates `*argv` plus two out-parameters through `char ***login_argv` / `char **login_argv0` while `try_dash_cmd` partially fills them and can fail after allocating, mixing ownership transfer with control flow across three pointer levels. Fix: return one small result struct from a single function.
- [ ] `src/core/cage.c:220` — `if (st->child >= 0) { kill(st->child, SIGTERM); pidwait(st->child, NULL); }` is unreachable: `st->child` is only assigned on the success path, while `spawn_cleanup` runs on the `goto fail` path where it is still -1. Fix: delete the branch.
- [ ] `src/sandbox/vol.c:31` called from `src/sandbox/mount.c:86` — the mount unit calls `parse_vol(&v, cfg->volumes[i])` inside the I/O path, so one module both parses CLI input and performs mounts. Fix: parse the volume specs once during config validation and pass `vol_t` entries to `mount_volumes()`.
- [ ] `src/sandbox/nssetup.h:11` — `const user_t *user;` in `struct run_ctx` is always initialised to NULL and never read, while `do_child_setup` builds its own `user_t`. Fix: remove the field and its initialiser.
- [ ] `src/util/syscall.c:3` with `src/net/net.c:3`, `src/net/rtnl.c:2`, `src/net/nft.c:3` — the layering is circular: `util/syscall.c` includes `net/netlink.h` and delegates to it, while the net layer includes `util/syscall.h` and calls `sys_netlink_*`. Fix: move the `sys_netlink_*`/`sys_netfilter_*` seam out of `util` into the net layer (or a test-only shim).
- [ ] `src/pkg/modload.c:41` with `src/pkg/modload.c:102`, `src/pkg/modload.c:117`, `src/pkg/resolve.c:41` — compression-format dispatch is spread across four sites keyed on magic return codes 1/2/3 (`decompress_magic`, `load_decompressed`, `decompress_xzstd_to_fd`, `valid_ko_suffix`), so adding a format means editing a branch in each. Fix: one table of `{magic bytes, suffix, decompressor}` rows that all four sites iterate.

## Duplication and generalization

- [ ] `src/pkg/modprobe.c:16` and `src/pkg/resolve.c:31` — `skip_to_name` is a byte-identical static function defined twice in two translation units. Fix: keep one copy in a shared internal header and call it from both.
- [ ] `src/pkg/modprobe.c:110`, `src/pkg/resolve.c:162`, `src/pkg/resolve.c:170` — `"/lib/modules"` is spelled by three weak-alias getters (two in the same file) and each caller re-implements the same `mem_cat(base, ..., "/modules.dep" / "modules.builtin", ...)` assembly. Fix: one module-base constant plus one shared `build_modules_path()` helper.
- [ ] `src/net/net.c:29` and `src/net/rtnl.c:280` — `link_dump_find` and `netlink_dump_find_ifname` are the same fill/send/recv-dump/find sequence differing only in parameter spelling, and `src/net/rtnl.c:306` repeats the prologue a third time. Fix: keep one `netlink_dump_find_ifname` and factor the prologue for `netlink_alloc_veth_id`.
- [ ] `src/net/net.c:13` — `HOST_PFX` and `MAX_VETH_ID` are never referenced, while the same values are re-spelled as literals at the decision sites: `if (used >= 126)` (`src/net/rtnl.c:320`), `"cage-veth%d"`/`"cage-veth"` (`src/core/cage.c:50`, `src/core/cage.c:92`), and `"eth0"` (`src/core/cage.c:51`, which must stay in sync with `src/net/net.c:143`). Fix: export the macros from `net.h` and use them at every site, or delete them.
- [ ] `src/pkg/modprobe.c:35` and `src/pkg/resolve.c:52` — two different functions both named `name_matches` implement module-name matching with divergent semantics: modprobe folds `-` to `_`, resolve requires an exact `memcmp` plus a `.ko` suffix, so `modprobe_load("foo-bar")` and `resolve_module` disagree on the same module. Fix: one shared matcher with a documented `-`/`_` rule and suffix policy.
- [ ] `src/net/netlink.c:125`, `src/net/rtnl.c:17`, `src/net/nft.c:51` — the nlattr-append step is written three times with inconsistent bounds-checking contracts. Fix: one bounds-checked `nl_put_attr(buf, cap, off, type, data, len)` plus string/u32/nest wrappers in `netlink.c`, used by `rtnl.c` and `nft.c`.
- [ ] `src/user/user_resolve.c:15` — the nobody id `65534` is hard-coded four times (`u->uid = 65534` and `u->passwd_gid = 65534` in both `resolve_passwd_nodb` and `resolve_by_name`). Fix: one named constant in `user_priv.h`.
- [ ] `src/net/nft.c:164` — `"cage-nat"` appears three times and `"postrouting"` twice as raw literals across `nft_create_table`, `nft_create_chain` and `nft_add_masq_rule`. Fix: `NFT_TABLE_NAME`/`NFT_CHAIN_NAME` macros at the top of `nft.c`.
- [ ] `src/pkg/modprobe.h:17` vs `src/pkg/modload.h:4` — `int load_module_path(const char *path);` is declared in both headers. Fix: drop the duplicate from `modprobe.h`.
- [ ] `src/net/netlink.c:13` and `src/net/nft.c:226` — `netlink_open` and `netfilter_open` are near-identical, differing only in the protocol constant. Fix: one `nl_open(int protocol)` with a one-line wrapper each.
- [ ] `src/net/netlink.c:51` — `netlink_recv` and `netlink_recv_nb` are identical except for `MSG_DONTWAIT`. Fix: one `netlink_recv_flags(..., int flags)` with two thin wrappers.
- [ ] `src/net/net.c:44` — `addr_add`, `default_route_add`, `link_up`, `addr6_add` and `default_route6_add` are five copies of the same fill/send/recv skeleton differing only in which fill function they call. Fix: one `nl_transact()` helper each caller reduces to a single call.
- [ ] `src/net/net.c:164` and `src/net/net.c:177` — `__net_enable_ip_forward` and `__net_enable_ip6_forward` are identical except for the proc path. Fix: one `write_proc_sys(const char *path)` with two aliases.
- [ ] `src/pkg/modload.c:56` and `src/pkg/modload.c:102` — `buf_write_to_fd` and `pipe_to_fd` contain the same write-all loop. Fix: extract one `write_all(int fd, const char *buf, size_t len)`.
- [ ] `src/pkg/modload.c:155` and `src/pkg/modload.c:195` — the `finit_module` + save-errno + close + report + return block is duplicated verbatim in `load_decompressed` and `load_module_path`. Fix: extract a `finish_finit(int fd)` helper.
- [ ] `src/user/user.c:74` and `src/user/user.c:94` — `ensure_primary_gid_in_groups` hand-rolls the membership loop that already exists as `gid_already_in_list` a few lines below. Fix: call the helper.
- [ ] `src/user/user_resolve.c:22` and `src/user/user_resolve.c:42` — the uid-0 fallback (`root` / `/root` / gid 0) is written identically in `resolve_passwd_nodb` and `resolve_by_uid`. Fix: extract a `set_root_fallback()` helper.
- [ ] `src/pkg/modprobe.c:169` — `load_deps` frees the caller-owned `mod_path` on its error path but leaves it to the caller on success, so ownership of the same argument depends on the outcome. Fix: never free `mod_path` there and document that only `deps` is consumed.
- [ ] `src/user/user_parse.c:32` — `parse_one_token` silently returns a zeroed token when its `malloc` fails, a state indistinguishable from a successfully parsed numeric token, which `get_gid` then honours as an explicit primary gid of 0. Fix: propagate allocation failure out of `parse_one_token`/`append_tokens` so `user_new` fails instead.

## Dead code

- [ ] `src/user/user.c:131` — `get_rootfs` is a public accessor with no production caller; every production site reads `u->rootfs` directly and only `tests/test_user.c:456` calls it. Fix: remove it, or replace the direct field reads with it.
- [ ] `src/sandbox/ext4fix.c:177` — `make_relocated_dirs` is never called by production (only `tests/test_mount.c`), as its own suppression comment notes. Fix: wire it into the relocation path or delete it with its tests.
- [ ] `src/pkg/modprobe.c:80` — `scan_modules` is production-dead: production reads the static `scan_buf` directly and only `tests/test_modprobe.c` calls it. Fix: drop it from the public header or make it the single production entry point.
- [ ] `src/pkg/modprobe.c:210` — `modprobe_cache_clear_builtin` has no production caller and exists only as a test-isolation seam. Fix: keep it behind a documented test seam or remove it.
- [ ] `src/pkg/resolve.c:154` — `resolve_cache_clear` has no production caller and is used only to reset `cached_deps` between tests. Fix: same as above.
- [ ] `tests/test_ext4.c:14` — `crc16_build`/`ext4_crc16` reimplement a CRC16 that exists nowhere in `src/`, and `test_crc16_empty`/`test_crc16_known` assert against that test-local copy rather than production code. Fix: delete them, or point them at real production logic.

## Tests

- [ ] `tests/test_super.c:13` — `test_fix_succeeds_as_root` is `if (geteuid() != 0) return; return;` with zero assertions and cannot fail. Fix: exercise `super_fix` with mocked syscalls or delete it.
- [ ] `tests/test_super.c:74` — `ASSERT(r == 0 || r == 1)` accepts every possible boolean result of `is_suid_binary()` and verifies nothing. Fix: mock the `stat`/`S_ISUID` probe and assert the exact value.
- [ ] `tests/test_cgroup.c:17` — `test_cgroup_cleanup_empty` calls `cgroup_cleanup(0, 0)` with no assertion. Fix: assert on the return value or on an observable mock side effect.
- [ ] `tests/test_cgroup.c:38` — `test_cgroup_cleanup_nonexistent` likewise has no assertion. Fix: assert the expected return and that no host path was touched.
- [ ] `tests/test_cgroup.c:24` — `ASSERT(r >= -1)` is always true and is immediately subsumed by `ASSERT(r >= 0)`. Fix: drop it and assert the documented range explicitly.
- [ ] `tests/test_io.c:445` — `ASSERT(r == 0 || r == -1)` on `io_chown` accepts both documented outcomes. Fix: assert based on an explicit mock of the chown syscall.
- [ ] `tests/test_io.c:503` — `test_umask_restore` asserts `after == 0077` on a value the test itself set through the test-local `io_umask` wrapper (`tests/test_io.c:12`), so it validates the wrapper, not production. Fix: delete it or assert a production umask-bearing path.
- [ ] `tests/test_io.c:105` — the `test_samefs_*` cases exercise only the test-local `io_samefs` helper (`tests/test_io.c:105`), never production `same_fs_detect`. Fix: point them at production or remove them.
- [ ] `tests/test_loop.c:247` — `ASSERT(r == -1 || r >= 0)` is a tautology for any `int`. Fix: assert the documented outcome directly.
- [ ] `tests/test_reuse.c:28` and `tests/test_reuse.c:259` — `ASSERT(r == 0 || r == 1)` cannot fail. Fix: assert the exact expected status or the observed side effects.
- [ ] `tests/test_net.c:56` and `tests/test_net.c:118` — `ASSERT(r == 0 || r == -1)` after setting `_mock_net_ip_forward_ret = -1` accepts both outcomes. Fix: assert the exact mocked result.
- [ ] `tests/test_net.c:64` — `test_netfilter_open_real_call` asserts `r == 0 || r == -1` but the call is intercepted by the mock `sys_netfilter_open` (`tests/mock_syscall.c:185`), so production never runs. Fix: retarget it at production or rename it to match what it actually covers.
- [ ] `tests/test_proc.c:325` — `test_pidexists_unexpected_errno` asserts `r == 0 || r == -1` while calling the real `pidexists(1)` on PID 1. Fix: force the errno condition through a mock and assert one result.
- [ ] `tests/test_modprobe.c:909` — `test_load_dep_nonexistent` calls `load_dep("does_not_exist")` with no assertion. Fix: assert the return value under a mocked loader.
- [ ] `tests/test_user.c:469` — `test_free_null` calls `user_free(NULL)` with no assertion. Fix: remove it or assert the no-crash invariant explicitly.
- [ ] `tests/test_gui.c:47` — `test_setup_gui_no_host_home` is a line-for-line duplicate of `test_setup_gui_not_graphical` (`tests/test_gui.c:17`). Fix: delete one and keep only the cases that exercise a distinct branch.
- [ ] `tests/test_loop.c:56` — the loop suite creates and unlinks real device nodes `/dev/loop-control` and `/dev/loop0`, and early-returns when unprivileged so the whole suite passes vacuously, violating CONTRIBUTING §Testing. Fix: inject a mockable loop-control seam in `src/util/syscall.c` and assert unconditionally.
- [ ] `tests/test_cgroup.c:22` — `test_cgroup_get_type` calls production `cgroup_get_type`, which reads the host `/proc/filesystems` unmocked. Fix: route the filesystem probe through a weak-alias mock and assert both v1 and v2 paths.
- [ ] `tests/test_cgroup.c:280` — `read_proc_cgroups` reads the real host `/proc/cgroups` and the test only asserts `n >= 0`. Fix: route the reader through a mockable accessor and feed fixed content.
- [ ] `tests/test_cgroup.c:319` — several cgroup tests gate on `if (geteuid() == 0) return;` or `if (n <= 0) return;`, producing vacuous passes and leaking the temp root in `test_pre_unshare_no_controllers` (`tests/test_cgroup.c:576`) and `test_pre_unshare_mkdir_fails` (`tests/test_cgroup.c:609`). Fix: use the `_at` variants with a temp root and assert unconditionally.
- [ ] `tests/test_mount.c:648` — `same_fs_detect("/tmp", "/var", NULL)` plus `ASSERT_NE(r, 1)` depends on the host partition layout. Fix: rely on `_mock_same_fs_ret` alone.
- [ ] `tests/test_reuse.c:17` — `spawn_existing` executes the real `/bin/true` and reads real `/proc/<pid>/root`, asserted only loosely. Fix: mock the process-spawn/namespace syscalls and assert the exact return.
- [ ] `tests/test_reuse.c:238` — `env_read_pid_environ(getpid())` reads the real `/proc/<pid>/environ` and only asserts non-NULL. Fix: read from a fixture path via a mockable opener and assert the parsed map.
- [ ] `tests/test_modprobe.c:886` — `check_module_status("overlay")` asserts `r == 1`, assuming the host already has overlay loaded in `/proc/modules`. Fix: mock the modules-file reader and assert on crafted content.
- [ ] `tests/test_modprobe.c:930` — the test opens the real `/dev/null` to obtain an fd. Fix: use `memfd_create` per CONTRIBUTING §Testing.
- [ ] `tests/test_proc.c:37` — the `wait_zombie` helper reads `/proc/<pid>/stat`, a documented machine-state dependency. Fix: mock the `/proc` reader used by the `killpid`/`pid_zombie` tests.
- [ ] `tests/test_setup.c:572` — `mount_cgroup_v1(root)` depends on the host `/proc/cgroups` contents being available. Fix: mock the cgroup-fs probe.
- [ ] `tests/test_super.c:6` — the `super_*` tests gate on `geteuid() == 0` and reach into `/proc/self/exe` and a real `sudo`, so results depend on host privilege and installed tools. Fix: mock the `super_*` syscall surface via weak aliases.
- [ ] `tests/test_env.c:149` — `test_gui_envs_with_x11` creates and removes the shared host directory `/tmp/.X11-unix` (and the setup is dead, since the X11 scan lives in `src/sandbox/gui_x11.c`). Fix: drop the host directory manipulation and test `x11_resolve_display` with a mocked filesystem.
- [ ] `tests/test_gui.c:650` (also `tests/test_mount.c:620`, `tests/test_env.c:274`) — mock control variables are saved and restored by hand instead of calling `mock_sys_reset()`, so a failed assertion longjmps past the restore and leaks the setting into later tests. Fix: reset at test entry.
- [ ] `tests/test_util.c:42` — `test_rmtree` duplicates production `io_rmtree` (`src/util/io.c:90`), and `io_rmdir`/`io_truncate` (`tests/test_util.c:17`) are test-only reimplementations. Fix: call the production helpers or document why the copies must exist.
- [ ] Untested translation units — `src/net/nft.c`, `src/sandbox/cg_mount.c`, `src/sandbox/cgroup_rmdir.c`, `src/sandbox/gui_x11.c`, `src/sandbox/gui_xdg.c`, `src/ext4/sb.c`, `src/ext4/util.c` and `src/user/user_pwd.c` have no test referencing their public functions. Fix: add focused tests for each, starting with `src/net/nft.c` (newest and entirely uncovered).

## Performance

- [ ] `src/sandbox/setup.c:83` (also `src/sandbox/setup.c:164`) — `io_read` grows its buffer without any size cap while reading a container-rootfs-controlled file, so a huge `/etc/resolv.conf` drives `buf_grow` until `mem_alloc` aborts the child. Fix: enforce a limit before reading (read at most N bytes and fail closed beyond it), keeping the bound ahead of the allocation.

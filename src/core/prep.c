#include "prep.h"
#include "launch.h"
#include "proc.h"
#include "net/net.h"
#include "pkg/modprobe.h"
#include "sandbox/mount.h"
#include "sandbox/cgroup.h"
#include "sandbox/gui.h"
#include "util/env.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <limits.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/mman.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

// cppcheck-suppress staticFunction
const char *find_envvar(const cage_config_t *cfg, const char *key)
{
  size_t kl = strlen(key);
  for (size_t i = 0; i < cfg->envvar_count; i++) {
    const char *e = cfg->envvars[i];
    if (strncmp(e, key, kl) == 0 && e[kl] == '=') {
      const char *v = e + kl + 1;
      return v[0] ? v : NULL;
    }
  }
  return NULL;
}

// cppcheck-suppress staticFunction
int resolve_script(const char *script, const char *root, char *buf, size_t size)
{
  if (access(script, R_OK) != 0) {
    char *path = mem_path(root, script);
    if (!path)
      return -1;
    int ok = access(path, R_OK) == 0;
    mem_free(path);
    if (!ok)
      return -1;
  }
  snprintf(buf, size, "%s", script);
  return 0;
}

static void put_home_user_shell(env_t *env, const char *home, const char *user, const char *shell)
{
  env_map_put(&env->map, "HOME", home ? home : "/root");
  env_map_put(&env->map, "USER", user ? user : "root");
  env_map_put(&env->map, "SHELL", shell);
}

static void exec_script(const char *sh, const char *script, char **env)
{
  execve(sh, (char *[]) { (char *)sh, (char *)script, NULL }, env);
  _exit(127);
}

static void jail_root(const char *root)
{
  if (chdir(root) != 0 || sys_chroot(".") != 0 || chdir("/") != 0)
    _exit(127);
}

static void init_child(const char *root, env_t *env, const char *script, const user_t *user)
{
  jail_root(root);
  umask(0022);
  setgroups(0, NULL);
  const char *sh;
  if (env->guest_shell_override)
    sh = get_shell((user_t *) user);
  else
    sh = env_map_get(&env->map, "SHELL");
  if (!sh)
    sh = "/bin/sh";
  const char *home = get_home((user_t *) user);
  const char *uname = get_name((user_t *) user);
  put_home_user_shell(env, home, uname, sh);
  exec_script(sh, script, env_map_build(&env->map, NULL));
}

static int run_init(const char *script, const char *root, env_t *env, const user_t *user)
{
  pid_t pid = pidfork();
  if (pid < 0)
    return -1;
  if (pid == 0)
    init_child(root, env, script, user);
  int st;
  pidwait(pid, &st);
  return st == 0 ? 0 : -1;
}

void do_init(const char *script, const char *root, env_t *env, const user_t *user)
{
  char path[PATH_MAX];
  if (resolve_script(script, root, path, sizeof(path)) != 0) {
    fprintf(stderr, "cage: init script not found\n");
    _exit(1);
  }
  if (run_init(path, root, env, user) != 0) {
    fprintf(stderr, "cage: init script failed\n");
    _exit(1);
  }
}

static void fill_guest_env(struct gui_ctx *gc, const cage_config_t *cfg)
{
  gc->guest_wl = find_envvar(cfg, "WAYLAND_DISPLAY");
  gc->guest_ps = find_envvar(cfg, "PULSE_SERVER");
  gc->guest_pc = find_envvar(cfg, "PULSE_COOKIE");
  gc->guest_xrd = find_envvar(cfg, "XDG_RUNTIME_DIR");
  gc->guest_xch = find_envvar(cfg, "XDG_CONFIG_HOME");
  gc->guest_display = find_envvar(cfg, "DISPLAY");
}

static struct gui_ctx fill_gui_ctx(const cage_config_t *cfg, const user_t *user, const char *root, const env_t *env)
{
  group_list_t gl = get_groups((user_t *) user);
  struct gui_ctx gc = {
    root, get_uid((user_t *) user), get_gid((user_t *) user), gl.groups, gl.count,
    getenv("WAYLAND_DISPLAY"), getenv("PULSE_SERVER"), getenv("PULSE_COOKIE"),
    getenv("XDG_CONFIG_HOME"), env->host_xdg_runtime_dir, env->host_home,
    getenv("DISPLAY"),
    0, 0, 0, 0, 0, 0, 0, 0, 0, -1,
  };
  fill_guest_env(&gc, cfg);
  return gc;
}

static void maybe_set_xdg_runtime(const struct gui_ctx *gc, env_t *env, uid_t uid)
{
  if (gc->xrd_created && !gc->guest_xrd_env) {
    char buf[64];
    snprintf(buf, sizeof(buf), "/run/user/%u", uid);
    env_map_put(&env->map, "XDG_RUNTIME_DIR", buf);
  }
}

static void fixup_home(env_t *env, const user_t *user)
{
  if (env_map_get(&env->map, "HOME"))
    return;
  const char *h = get_home((user_t *) user);
  env_map_put(&env->map, "HOME", h);
}

static void resolve_host_home(env_t *env)
{
  const char *h = getenv("SUDO_HOME");
  env->host_home = (char *)(h ? h : getenv("HOME"));
}

static void resolve_host_paths(env_t *env, uid_t ruid)
{
  if (!env->host_xdg_runtime_dir) {
    char buf[64];
    env->host_xdg_runtime_dir = mem_strdup(get_host_xdg_dir(buf, sizeof(buf), ruid));
  }
  if (!env->host_home)
    resolve_host_home(env);
}

static void apply_gui_env(const struct gui_ctx *gc, env_t *env, const user_t *user, uid_t ruid)
{
  const char *disp = gc->guest_display ? gc->guest_display : gc->host_display;
  env_set_gui_envs(env, disp, gc->guest_x11num, gc->host_wl, ruid);
  maybe_set_xdg_runtime(gc, env, get_uid((user_t *) user));
}

static struct gui_ctx begin_gui_ctx(const cage_config_t *cfg, const user_t *user, const char *root, env_t *env,
                                    uid_t ruid)
{
  resolve_host_paths(env, ruid);
  fixup_home(env, user);
  struct gui_ctx gc = fill_gui_ctx(cfg, user, root, env);
  gc.xrd_created = 0;
  gc.guest_xrd_env = !!find_envvar(cfg, "XDG_RUNTIME_DIR");
  return gc;
}

void child_setup_gui(const cage_config_t *cfg, const user_t *user, const char *root, env_t *env, uid_t ruid)
{
  if (!cfg->graphical)
    return;
  struct gui_ctx gc = begin_gui_ctx(cfg, user, root, env, ruid);
  if (setup_gui_mounts(&gc, env_map_get(&env->map, "HOME")) != 0) {
    fprintf(stderr, "cage: GUI setup failed\n");
    _exit(1);
  }
  apply_gui_env(&gc, env, user, ruid);
}

void child_gui_env_only(const cage_config_t *cfg, const user_t *user, const char *root, env_t *env, uid_t ruid)
{
  if (!cfg->graphical)
    return;
  if (cfg->inst_env) {
    fixup_home(env, user);
    env_inherit_gui(&env->map, cfg->inst_env);
    return;
  }
  struct gui_ctx gc = begin_gui_ctx(cfg, user, root, env, ruid);
  x11_resolve_display(&gc);
  xdg_detect_socks(&gc);
  apply_gui_env(&gc, env, user, ruid);
}

int do_child_net(int net_id, volatile int *net_futex)
{
  *net_futex = 1;
  sys_futex_wake(net_futex, 1);
  if (*net_futex != 2)
    sys_futex_wait(net_futex, 1);
  return net_setup_child(net_id);
}

void child_root_ready(volatile int *root_futex)
{
  *root_futex = 1;
  sys_futex_wake(root_futex, 1);
  munmap((void *)root_futex, 4096);
}

void child_run(const user_t *user, env_t *env, cage_config_t *cfg)
{
  int rc = launch_cage(user, env, cfg->currentdir, cfg->command);
  _exit(rc == 0 ? 0 : 1);
}

static char *resolve_pidfile(cage_config_t *cfg, char **free_out)
{
  if (cfg->pidfile) {
    *free_out = NULL;
    return cfg->pidfile;
  }
  char *pf = compute_pidfile(cfg->name, NULL);
  cfg->pidfile = pf;
  *free_out = pf;
  return pf;
}

int do_early_setup(cage_config_t *cfg, struct pidfile_info *pi)
{
  pi->pidfile = resolve_pidfile(cfg, &pi->free_pidfile);
  return pi->pidfile ? 0 : -1;
}

static char *alloc_root_template(void)
{
  return mem_strdup("/tmp/cage-XXXXXX");
}

static void unprepare(char **root)
{
  if (*root) {
    io_rmtree(*root);
    mem_free(*root);
    *root = NULL;
  }
}

static int early_prep(const cage_config_t *cfg, struct prep_result *pr)
{
  if (early_setup_appdir(cfg, &pr->dirs) != 0)
    return -1;
  if (pr->cg_fs_type != 2)
    return 0;
  return cgroup_pre_unshare(pr->cg_fs_type, pr->cg_pid) != 0 ? -1 : 0;
}

int do_root_prep(const cage_config_t *cfg, struct prep_result *pr, const struct run_ctx *rc, int root_private)
{
  if (early_prep(cfg, pr) != 0)
    return -1;
  if (!(pr->root = alloc_root_template()))
    return -1;
  if (prepare_mounts(pr, cfg, rc, root_private)) {
    unprepare(&pr->root);
    return -1;
  }
  return 0;
}

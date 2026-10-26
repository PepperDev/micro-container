#include "sandbox/child.h"
#include "core/launch.h"
#include "core/proc.h"
#include "core/prep.h"
#include "net/net.h"
#include "sandbox/gui.h"
#include "util/env.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
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

pid_t child_fork(void)
{
  pid_t pid = sys_fork();
  if (pid < 0)
    fprintf(stderr, "cage: unable to fork process: %s\n", strerror(errno));
  return pid;
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
    sh = get_shell(user);
  else
    sh = env_map_get(&env->map, "SHELL");
  if (!sh)
    sh = "/bin/sh";
  const char *home = get_home(user);
  const char *uname = get_name(user);
  put_home_user_shell(env, home, uname, sh);
  exec_script(sh, script, env_map_build(&env->map, NULL));
}

static int run_init(const char *script, const char *root, env_t *env, const user_t *user)
{
  pid_t pid = child_fork();
  if (pid < 0)
    return -1;
  if (pid == 0)
    init_child(root, env, script, user);
  int st = 0;
  if (pidwait(pid, &st) != 0)
    return -1;
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
  group_list_t gl = get_groups(user);
  struct gui_ctx gc = {
    root, get_uid(user), get_gid(user), gl.groups, gl.count,
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
  const char *h = get_home(user);
  env_map_put(&env->map, "HOME", h);
}

static int host_home_ok(const char *h)
{
  if (!h || h[0] != '/')
    return 0;
  if (mem_path_escapes(h))
    return 0;
  return io_isdir(h) == 0;
}

// cppcheck-suppress staticFunction
void resolve_host_home(env_t *env)
{
  const char *h = getenv("SUDO_HOME");
  if (!h)
    h = getenv("HOME");
  env->host_home = host_home_ok(h) ? (char *)h : NULL;
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
  maybe_set_xdg_runtime(gc, env, get_uid(user));
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
  struct gui_ctx gc = begin_gui_ctx(cfg, user, root, env, ruid);
  x11_resolve_display(&gc);
  xdg_detect_socks(&gc);
  apply_gui_env(&gc, env, user, ruid);
}

void child_gui_env_inherit(const cage_config_t *cfg, const user_t *user, env_t *env, const char *inst_env)
{
  if (!cfg->graphical)
    return;
  fixup_home(env, user);
  env_inherit_gui(&env->map, inst_env);
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

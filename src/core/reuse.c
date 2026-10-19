#include "reuse.h"
#include "proc.h"
#include "launch.h"
#include "prep.h"
#include "util/env.h"
#include "user/user.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/wait.h>

/* cppcheck-suppress staticFunction -- tests call it directly */
int reuse_setup_env(cage_config_t *cfg, const user_t *user, const char *root, env_t *env, uid_t ruid)
{
  if (parse_envs(env, cfg->envvars, cfg->envvar_count) != 0)
    return -1;
  child_gui_env_only(cfg, user, root, env, ruid);
  return 0;
}

static void reuse_child(cage_config_t *cfg, uid_t ruid)
{
  if (sys_chroot(".") != 0)
    _exit(1);
  env_t env = { 0 };
  const user_t *user = user_new(cfg->user, cfg->group, "/");
  if (!user)
    _exit(1);
  if (reuse_setup_env(cfg, user, "/", &env, ruid) != 0)
    _exit(1);
  int r = launch_cage(user, &env, cfg->currentdir, cfg->command);
  mem_free(env.host_xdg_runtime_dir);
  _exit(r != 0);
}

// cppcheck-suppress staticFunction
int wait_status(pid_t child)
{
  int status = 0;
  if (pidwait(child, &status) != 0)
    return 1;
  return exit_code_from_status(status);
}

int spawn_existing(cage_config_t *cfg, pid_t old_pid, uid_t ruid)
{
  char chroot_path[64];
  snprintf(chroot_path, sizeof(chroot_path), "/proc/%d/root", (int)old_pid);

  if (chdir(chroot_path) != 0)
    return -1;

  if (cfg->graphical)
    cfg->inst_env = env_read_pid_environ(old_pid);

  int rc = -1;
  if (join_namespaces(old_pid) != 0) {
    fprintf(stderr, "cage: setns failed\n");
    goto out;
  }
  pid_t child = pidfork();
  if (child < 0)
    goto out;
  if (child == 0)
    reuse_child(cfg, ruid);
  rc = wait_status(child);
 out:
  mem_free(cfg->inst_env);
  return rc;
}

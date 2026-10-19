#include "nssetup.h"
#include "mount.h"
#include "overlay.h"
#include "setup.h"
#include "core/proc.h"
#include "core/prep.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/env.h"
#include "util/syscall.h"
#include "pkg/modprobe.h"
#include "sandbox/cgroup.h"
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>
// cppcheck-suppress missingIncludeSystem
#include <sched.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/wait.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>

/* cppcheck-suppress staticFunction */
int ns_setup(const cage_config_t *cfg)
{
  unsigned long flags = CLONE_NEWPID | (cfg->unshare_cgroup ? CLONE_NEWCGROUP : 0);
  return sys_unshare(flags) != 0 ? -1 : 0;
}

/* cppcheck-suppress staticFunction */
int mount_ns_setup(int root_private)
{
  if (sys_unshare(CLONE_NEWNS) != 0)
    return -1;
  if (!root_private && sys_mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
    fprintf(stderr, "cage: make-private failed\n");
    return -1;
  }
  return 0;
}

static void inner_child_net(const struct run_ctx *rc)
{
  if (rc->net_id < 0)
    return;
  if (sys_unshare(CLONE_NEWNET) != 0 || do_child_net(rc->net_id, rc->net_futex) < 0)
    _exit(1);
}

static void eager_resolve_user(user_t *u)
{
  user_resolve(u);
  get_uid(u);
  get_gid(u);
  get_groups(u);
}

static void move_and_chroot(const char *root)
{
  if (chdir(root) != 0 || sys_mount(root, "/", NULL, MS_MOVE, NULL) != 0 || sys_chroot(".") != 0)
    _exit(1);
}

static void do_child_init(const char *root, const cage_config_t *cfg, const struct run_ctx *rc)
{
  const user_t *root_user = user_root_new(root);
  do_init(cfg->initscript, root, (env_t *) rc->env, root_user);
}

static user_t *do_child_setup(const char *root, const cage_config_t *cfg, const struct run_ctx *rc, int has_cgroup)
{
  env_t *env = (env_t *) rc->env;
  if (mount_all(root, cfg, has_cgroup) != 0)
    _exit(1);
  if (parse_envs(env, (char **)cfg->envvars, cfg->envvar_count) != 0)
    _exit(1);
  if (cfg->initscript)
    do_child_init(root, cfg, rc);
  user_t *u = user_new(cfg->user, cfg->group, root);
  if (!u)
    _exit(1);
  eager_resolve_user(u);
  child_setup_gui(cfg, u, root, env, rc->host_ruid);
  return u;
}

static void run_mount_inner(const struct prep_result *pr, const cage_config_t *cfg, const struct run_ctx *rc,
                            int root_private)
{
  if (mount_ns_setup(root_private) != 0 || prepare_overlay(pr->root, &pr->dirs) != 0)
    _exit(1);

  int has_cgroup = cfg->unshare_cgroup ? rc->cg_fs_type : -2;
  user_t const *u = do_child_setup(pr->root, cfg, rc, has_cgroup);
  move_and_chroot(pr->root);
  child_root_ready(rc->root_futex);

  if (cfg->unshare_cgroup && cgroup_enter("/", has_cgroup >= 2) != 0)
    _exit(1);
  inner_child_net(rc);
  child_run(u, (env_t *) rc->env, (cage_config_t *) cfg);
}

static int fork_mount_child(const struct prep_result *pr, const cage_config_t *cfg, const struct run_ctx *rc,
                            int root_private, pid_t *child)
{
  pid_t pid = pidfork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    run_mount_inner(pr, cfg, rc, root_private);
    _exit(1);
  }
  *child = pid;
  return 0;
}

int prepare_mounts(struct prep_result *pr, const cage_config_t *cfg, const struct run_ctx *rc, int root_private)
{
  if (!mkdtemp(pr->root)) {
    fprintf(stderr, "cage: mkdtemp failed\n");
    return -1;
  }
  if (ns_setup(cfg) != 0)
    return -1;
  return fork_mount_child(pr, cfg, rc, root_private, &pr->child);
}

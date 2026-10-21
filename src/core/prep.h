#ifndef CAGE_PREP_H
#define CAGE_PREP_H

#include "cli/args.h"
#include "util/env.h"
#include "user/user.h"
#include "sandbox/nssetup.h"
#include "sandbox/appdir.h"

struct prep_result {
  char *root;
  pid_t child;
  appdir_t dirs;
  int cg_fs_type;
  pid_t cg_pid;
};

struct pidfile_info {
  const char *pidfile;
  char *free_pidfile;
};

int do_early_setup(cage_config_t * cfg, struct pidfile_info *pi);
int do_root_prep(const cage_config_t * cfg, struct prep_result *pr, const struct run_ctx *rc, int root_private);
void child_run(const user_t * user, env_t * env, cage_config_t * cfg);
int do_child_net(int net_id, volatile int *net_futex);
void child_root_ready(volatile int *root_futex);
int resolve_script(const char *script, const char *root, char *buf, size_t size);
const char *find_envvar(const cage_config_t * cfg, const char *key);
void resolve_host_home(env_t * env);
void do_init(const char *script, const char *root, env_t * env, const user_t * user);
void child_setup_gui(const cage_config_t * cfg, const user_t * user, const char *root, env_t * env, uid_t ruid);
void child_gui_env_only(const cage_config_t * cfg, const user_t * user, const char *root, env_t * env, uid_t ruid);

#endif

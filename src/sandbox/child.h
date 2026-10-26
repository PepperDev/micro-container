#ifndef CAGE_CHILD_H
#define CAGE_CHILD_H

#include "cli/args.h"
#include "util/env.h"
#include "user/user.h"

pid_t child_fork(void);
void child_run(const user_t * user, env_t * env, cage_config_t * cfg);
int do_child_net(int net_id, volatile int *net_futex);
void child_root_ready(volatile int *root_futex);
void do_init(const char *script, const char *root, env_t * env, const user_t * user);
void child_setup_gui(const cage_config_t * cfg, const user_t * user, const char *root, env_t * env, uid_t ruid);
void child_gui_env_only(const cage_config_t * cfg, const user_t * user, const char *root, env_t * env, uid_t ruid);
void child_gui_env_inherit(const cage_config_t * cfg, const user_t * user, env_t * env, const char *inst_env);

#endif

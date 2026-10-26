#ifndef CAGE_REUSE_H
#define CAGE_REUSE_H

#include "cli/args.h"
#include "util/env.h"
#include "user/user.h"
// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

int spawn_existing(cage_config_t * cfg, pid_t old_pid, uid_t ruid);

struct reuse_req {
  cage_config_t *cfg;
  const user_t *user;
  const char *root;
  const char *inst_env;
  uid_t ruid;
};

int reuse_setup_env(env_t * env, const struct reuse_req *req);

int wait_status(pid_t child);

#endif

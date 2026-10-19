#ifndef CAGE_NSSETUP_H
#define CAGE_NSSETUP_H

// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>
#include "cli/args.h"
#include "user/user.h"
#include "util/env.h"

struct run_ctx {
  const user_t *user;
  const env_t *env;
  volatile int *net_futex;
  volatile int *root_futex;
  int net_id;
  uid_t host_ruid;
  int cg_fs_type;
};

struct prep_result;
int ns_setup(const cage_config_t * cfg);
int mount_ns_setup(int root_private);
int prepare_mounts(struct prep_result *pr, const cage_config_t * cfg, const struct run_ctx *rc, int root_private);

#endif

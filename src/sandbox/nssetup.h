#ifndef CAGE_NSSETUP_H
#define CAGE_NSSETUP_H

// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>
#include "cli/args.h"
#include "util/env.h"
#include "sandbox/appdir.h"

struct run_ctx {
  const env_t *env;
  volatile int *net_futex;
  volatile int *root_futex;
  int net_id;
  uid_t host_ruid;
  int cg_fs_type;
};

struct prep_result {
  char *root;
  pid_t child;
  appdir_t dirs;
  int cg_fs_type;
  pid_t cg_pid;
};

int ns_setup(void);
int mount_ns_setup(int root_private, int unshare_cgroup, int unshare_net);
int prepare_mounts(struct prep_result *pr, const cage_config_t * cfg, const struct run_ctx *rc, int root_private);

#endif

#ifndef CAGE_PREP_H
#define CAGE_PREP_H

#include "cli/args.h"
#include "util/env.h"
#include "user/user.h"
#include "sandbox/nssetup.h"
#include "sandbox/appdir.h"

struct pidfile_info {
  const char *pidfile;
  char *free_pidfile;
};

int do_early_setup(const cage_config_t * cfg, struct pidfile_info *pi);
int do_root_prep(const cage_config_t * cfg, struct prep_result *pr, const struct run_ctx *rc, int *root_private);
int resolve_script(const char *script, const char *root, char *buf, size_t size);
const char *find_envvar(const cage_config_t * cfg, const char *key);
void resolve_host_home(env_t * env);

#endif

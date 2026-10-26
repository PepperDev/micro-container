#ifndef CAGE_MOUNT_H
#define CAGE_MOUNT_H

// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>
#include "cli/args.h"
#include "user/user.h"
#include "util/env.h"
#include "sandbox/appdir.h"

int early_setup_appdir(const cage_config_t * cfg, appdir_t * dirs);
int mount_bind(const char *root, const char *sub, const char *src);

void strip_trailing_slash(char *p);
int mount_fs_type(const char *root, const char *sub, const char *type, unsigned long flags, const char *data);
int prepare_volumes(cage_config_t * cfg);
int mount_volumes(const char *root, const cage_config_t * cfg);
int mount_dev(const char *root);
int mount_all(const char *root, const cage_config_t * cfg, int has_cgroup);

#endif

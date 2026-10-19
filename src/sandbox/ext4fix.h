#ifndef CAGE_EXT4FIX_H
#define CAGE_EXT4FIX_H

#include "appdir.h"
// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

int handle_same_fs(const cage_config_t * cfg, appdir_t * dirs);

char *grandparent(const char *path);
int derive_loopdir(const char *appdir, char *loopdir, size_t size);
int path_under(const char *path, const char *dir);
int same_fs_detect(const char *lowerdir, const char *upperdir, dev_t * lower_dev);
int make_relocated_dirs(appdir_t * dirs, const char *mnt);

#endif

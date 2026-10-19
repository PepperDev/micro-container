#ifndef CAGE_APPDIR_H
#define CAGE_APPDIR_H

#include "cli/args.h"

typedef struct {
  char appdir[4096];
  char upperdir[4096];
  char workdir[4096];
  char lowerdir[4096];
  int dirs_created;
} appdir_t;

int resolve_appdir(const cage_config_t * cfg, appdir_t * d);

#endif

#ifndef CAGE_CAGE_H
#define CAGE_CAGE_H

#include "cli/args.h"
// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

int spawn_cage(cage_config_t * cfg, uid_t ruid);

#endif

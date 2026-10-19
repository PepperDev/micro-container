#ifndef CAGE_LAUNCH_H
#define CAGE_LAUNCH_H

#include "user/user.h"
#include "util/env.h"

int launch_cage(const user_t * user, env_t * env, const char *cwd, char **argv);

#endif

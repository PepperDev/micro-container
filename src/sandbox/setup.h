#ifndef CAGE_SETUP_H
#define CAGE_SETUP_H

// cppcheck-suppress missingIncludeSystem
#include <stddef.h>

#include "cli/args.h"

int mount_misc(const char *root, const cage_config_t * cfg);
int setup_resolv(const char *root, const cage_config_t * cfg);

int has_real_ns_in_content(const char *content);
int setup_run_dirs(const char *root);
int setup_devshm(const char *root);

#endif

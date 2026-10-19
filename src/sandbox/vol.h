#ifndef CAGE_VOL_H
#define CAGE_VOL_H

// cppcheck-suppress missingIncludeSystem
#include <stddef.h>

typedef struct {
  char host[4096];
  char target[4096];
} vol_t;

int parse_vol(vol_t * v, const char *spec);
int mount_user_volume(const vol_t * v, const char *root);

#endif

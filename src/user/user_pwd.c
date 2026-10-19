#include "user_priv.h"
// cppcheck-suppress missingIncludeSystem
#include <limits.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>

FILE *open_db(const char *rootfs, const char *file)
{
  if (!rootfs)
    return NULL;
  char path[PATH_MAX];
  size_t rl = strlen(rootfs);
  size_t fl = strlen(file);
  if (rl + 1 + fl + 1 > sizeof(path))
    return NULL;
  memcpy(path, rootfs, rl);
  if (rl > 1 && path[rl - 1] != '/')
    path[rl++] = '/';
  memcpy(path + rl, file, fl + 1);
  return fopen(path, "r");
}

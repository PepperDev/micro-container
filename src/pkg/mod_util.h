#ifndef CAGE_MOD_UTIL_H
#define CAGE_MOD_UTIL_H

#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>

#define MODULES_BASE "/lib/modules"

/* Module names compare with '-' and '_' treated as equal; callers decide
   whether a ".ko" suffix is allowed or required. */
static inline int module_name_eq(const char *name, const char *s, size_t len)
{
  size_t nlen = strlen(name);
  if (len != nlen)
    return 0;
  for (size_t i = 0; i < nlen; i++) {
    char a = name[i] == '-' ? '_' : name[i];
    char b = s[i] == '-' ? '_' : s[i];
    if (a != b)
      return 0;
  }
  return 1;
}

static inline char *build_modules_path(const char *base, const char *release, const char *rest, size_t rest_len)
{
  return mem_cat(base, strlen(base), "/", 1, release, strlen(release), "/", 1, rest, rest_len, MEM_CAT_END);
}

static inline const char *skip_to_name(const char *start, const char *end)
{
  const char *s = start;
  for (const char *q = start; q < end; q++) {
    if (*q == '/')
      s = q + 1;
  }
  return s;
}

#endif

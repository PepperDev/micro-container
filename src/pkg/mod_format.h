#ifndef CAGE_MOD_FORMAT_H
#define CAGE_MOD_FORMAT_H

// cppcheck-suppress missingIncludeSystem
#include <stddef.h>

struct mod_format {
  int code;
  const unsigned char *magic;
  size_t magic_len;
  const char *suffix;
  const char *filter;
};

const struct mod_format *mod_format_by_magic(const unsigned char *p, size_t n);
const struct mod_format *mod_format_by_code(int code);
int mod_format_suffix_ok(const char *s, size_t n);

#endif

#include "mod_format.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>

static const unsigned char gzip_magic[] = { 0x1F, 0x8B };
static const unsigned char xz_magic[] = { 0xFD, 0x37 };
static const unsigned char zstd_magic[] = { 0x28, 0xB5, 0x2F, 0xFD };

static const struct mod_format formats[] = {
  {1, gzip_magic, sizeof(gzip_magic), ".gz", NULL},
  {2, xz_magic, sizeof(xz_magic), ".xz", "xzcat"},
  {3, zstd_magic, sizeof(zstd_magic), ".zst", "zstdcat"},
};

const struct mod_format *mod_format_by_magic(const unsigned char *p, size_t n)
{
  for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
    if (n >= formats[i].magic_len && memcmp(p, formats[i].magic, formats[i].magic_len) == 0)
      return &formats[i];
  }
  return NULL;
}

const struct mod_format *mod_format_by_code(int code)
{
  for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++)
    if (formats[i].code == code)
      return &formats[i];
  return NULL;
}

int mod_format_suffix_ok(const char *s, size_t n)
{
  if (n == 0)
    return 1;
  for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
    const char *suffix = formats[i].suffix;
    size_t sl = strlen(suffix);
    if (n == sl && memcmp(s, suffix, sl) == 0)
      return 1;
  }
  return 0;
}

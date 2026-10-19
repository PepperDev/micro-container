#ifndef CAGE_DECOMPRESS_H
#define CAGE_DECOMPRESS_H

#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <stddef.h>

int decompress_gzip(const void *in, size_t in_len, buf_t * out);
int open_and_read(const char *path, buf_t * buf);

#endif

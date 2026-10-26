#ifndef CAGE_TEST_UTIL_H
#define CAGE_TEST_UTIL_H

#include "util/io.h"
// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

char *test_temp_root(void);
int io_truncate(const char *path, off_t size);

#endif

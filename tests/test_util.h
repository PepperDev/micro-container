#ifndef CAGE_TEST_UTIL_H
#define CAGE_TEST_UTIL_H

// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

int test_rmtree(const char *path);
char *test_temp_root(void);
int io_rmdir(const char *path);
int io_truncate(const char *path, off_t size);

#endif

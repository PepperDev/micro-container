#ifndef CAGE_LOOP_H
#define CAGE_LOOP_H

// cppcheck-suppress missingIncludeSystem
#include <stddef.h>

int find_free_loop(void);
int io_loop(char *dev, size_t devsz, const char *file);

int loop_control_alloc(void);
int try_open_loop_dev(int n);
int scan_loop_devices(void);

#endif

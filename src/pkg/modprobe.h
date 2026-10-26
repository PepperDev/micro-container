#ifndef CAGE_MODPROBE_H
#define CAGE_MODPROBE_H

int modprobe_load(const char *name);

// cppcheck-suppress missingIncludeSystem
#include <stddef.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
#include "util/mem.h"
int line_has_name(const char *line, size_t len, const char *name);
int scan_module_list(const char *data, size_t len, const char *name);
int decompress_magic(const unsigned char *p, size_t n);
int finit_module_fd(int fd, unsigned int flags);
int buf_write_to_fd(int fd, const buf_t * data);
int pipe_to_fd(FILE * f, int fd);
int load_decompressed(const char *path, int comp);
int open_module(const char *path, int *out_fd);
int check_module_status(const char *name);
int load_dep(const char *name);
void modprobe_cache_invalidate(void);

#endif

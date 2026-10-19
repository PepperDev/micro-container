#ifndef CAGE_IO_H
#define CAGE_IO_H

#include "mem.h"
// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

int io_exists(const char *path);
int io_isdir(const char *path);
int io_islink(const char *path);
int io_unlink(const char *path);
int io_unlink_quiet(const char *path);
int io_rmtree(const char *path);
int io_mkdir(const char *path);
int io_mkdir_own(const char *path, uid_t uid, gid_t gid, mode_t mode);
int io_mkdtmp(const char *path, int sticky);
int io_symlink(const char *target, const char *path);
int io_chown(const char *path, uid_t uid, gid_t gid);
int io_touch(const char *path);
int io_read(const char *path, buf_t * b);
int io_read_quiet(const char *path, buf_t * b);

#endif

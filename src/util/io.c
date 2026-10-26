#include "io.h"
#include "util/syscall.h"
#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <dirent.h>

int io_isdir(const char *path)
{
  struct stat st;
  if (stat(path, &st) != 0) {
    fprintf(stderr, "cage: unable to stat %s\n", path);
    return -1;
  }
  return S_ISDIR(st.st_mode) ? 0 : 1;
}

int io_islink(const char *path)
{
  struct stat st;
  if (lstat(path, &st) != 0) {
    if (errno == ENOENT)
      return 2;
    fprintf(stderr, "cage: unable to lstat %s\n", path);
    return -1;
  }
  return S_ISLNK(st.st_mode) ? 0 : 1;
}

int io_unlink(const char *path)
{
  if (unlink(path) != 0) {
    fprintf(stderr, "cage: unable to unlink %s\n", path);
    return -1;
  }
  io_cache_invalidate(path);
  return 0;
}

int io_unlink_quiet(const char *path)
{
  if (unlink(path) != 0 && errno != ENOENT)
    return -1;
  io_cache_invalidate(path);
  return 0;
}

static int is_dot_or_dotdot(const char *name)
{
  if (name[0] != '.')
    return 0;
  if (name[1] == '\0')
    return 1;
  return name[1] == '.' && name[2] == '\0';
}

static int rmtree_dir(DIR *d, const char *path)
{
  const struct dirent *e;
  int rc = 0;
  while ((e = readdir(d)) != NULL) {
    if (is_dot_or_dotdot(e->d_name))
      continue;
    char child[4096];
    int n = snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
    if (n < 0 || (size_t)n >= sizeof(child)) {
      fprintf(stderr, "cage: path too long: %s/%s\n", path, e->d_name);
      rc = -1;
      continue;
    }
    if (io_rmtree(child) != 0)
      rc = -1;
  }
  return rc;
}

static int unlink_path(const char *path)
{
  if (unlink(path) != 0 && errno != ENOENT) {
    fprintf(stderr, "cage: cannot remove %s\n", path);
    return -1;
  }
  return 0;
}

static int is_symlink_path(const char *path)
{
  struct stat sb;
  return lstat(path, &sb) == 0 && S_ISLNK(sb.st_mode);
}

static int unlink_cached(const char *path)
{
  int r = unlink_path(path);
  if (r == 0)
    io_cache_invalidate(path);
  return r;
}

int io_rmtree(const char *path)
{
  if (is_symlink_path(path))
    return unlink_cached(path);
  DIR *d = opendir(path);
  if (!d)
    return unlink_cached(path);
  int rc = rmtree_dir(d, path);
  closedir(d);
  if (rc != 0)
    return -1;
  if (rmdir(path) != 0 && errno != ENOENT) {
    fprintf(stderr, "cage: unable to rmdir %s\n", path);
    return -1;
  }
  io_cache_invalidate(path);
  return 0;
}

static int mkdirm(const char *path, mode_t mode)
{
  if (mkdir(path, mode) != 0) {
    fprintf(stderr, "cage: unable to mkdir %s\n", path);
    return -1;
  }
  return 0;
}

static int mkdir_parent(const char *path)
{
  const char *slash = strrchr(path, '/');
  if (!slash || slash == path)
    return 0;
  size_t len = slash - path;
  char parent[4096];
  if (len >= sizeof(parent)) {
    fprintf(stderr, "cage: path too long %s\n", path);
    return -1;
  }
  memcpy(parent, path, len);
  parent[len] = '\0';
  return io_mkdir(parent);
}

static int mkdir_check_existing(const char *path)
{
  int d = io_isdir(path);
  if (d < 0)
    return -1;
  if (d == 0)
    return 0;
  fprintf(stderr, "cage: %s exists but is not a directory\n", path);
  return -1;
}

static int mkdir_existing(const char *path)
{
  int r = mkdir_check_existing(path);
  if (r == 0)
    io_cache_invalidate(path);
  return r;
}

static int mkdir_deep(const char *path)
{
  if (mkdir_parent(path) != 0)
    return -1;
  int r = mkdirm(path, 0755);
  if (r == 0)
    io_cache_invalidate(path);
  return r;
}

int io_mkdir(const char *path)
{
  if (mkdir(path, 0755) == 0) {
    io_cache_invalidate(path);
    return 0;
  }
  if (errno == EEXIST)
    return mkdir_existing(path);
  if (errno != ENOENT)
    return -1;
  return mkdir_deep(path);
}

static int mkdir_chown(const char *path, uid_t uid, gid_t gid, mode_t mode)
{
  if (mkdir(path, mode) != 0)
    return -1;
  io_cache_invalidate(path);
  if ((uid != 0 || gid != 0) && sys_chown(path, uid, gid) != 0)
    return -1;
  return 0;
}

static int mkdir_parents(char *path, uid_t uid, gid_t gid)
{
  for (char *p = path + 1; *p; p++) {
    if (*p != '/')
      continue;
    *p = '\0';
    if (io_exists(path) == 0) {
      *p = '/';
      continue;
    }
    if (mkdir_chown(path, uid, gid, 0755) != 0)
      return -1;
    *p = '/';
  }
  return 0;
}

int io_mkdir_own(const char *path, uid_t uid, gid_t gid, mode_t mode)
{
  int e = io_exists(path);
  if (e < 0)
    return -1;
  if (e == 0)
    return 0;

  char *dup_path = mem_strdup(path);
  if (!dup_path)
    return -1;
  int r = mkdir_parents(dup_path, uid, gid);
  mem_free(dup_path);
  if (r != 0)
    return -1;

  return mkdir_chown(path, uid, gid, mode);
}

int io_mkdtmp(const char *path, int sticky)
{
  mode_t m = sticky ? 01777 : 0755;
  if (mkdir(path, m) != 0) {
    fprintf(stderr, "cage: unable to mkdir %s\n", path);
    return -1;
  }
  io_cache_invalidate(path);
  return 0;
}

int io_symlink(const char *target, const char *path)
{
  if (symlink(target, path) != 0) {
    fprintf(stderr, "cage: unable to symlink %s -> %s\n", target, path);
    return -1;
  }
  io_cache_invalidate(path);
  return 0;
}

int io_chown(const char *path, uid_t uid, gid_t gid)
{
  if (sys_chown(path, uid, gid) != 0) {
    fprintf(stderr, "cage: unable to chown %s\n", path);
    return -1;
  }
  return 0;
}

int io_touch(const char *path)
{
  int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0) {
    fprintf(stderr, "cage: unable to create %s\n", path);
    return -1;
  }
  close(fd);
  io_cache_invalidate(path);
  return 0;
}

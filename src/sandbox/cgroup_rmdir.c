#include "cgroup.h"
#include "../util/io.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <limits.h>
// cppcheck-suppress missingIncludeSystem
#include <dirent.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>

static int is_dot_or_dotdot(const struct dirent *e)
{
  return e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0'));
}

static void rmdir_children(const char *path, DIR *d)
{
  const struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (is_dot_or_dotdot(e))
      continue;
    char child[PATH_MAX];
    snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
    struct stat st;
    if (lstat(child, &st) != 0)
      continue;
    if (S_ISDIR(st.st_mode))
      cgroup_rmdir_recursive(child);
  }
}

int cgroup_rmdir_recursive(const char *path)
{
  DIR *d = opendir(path);
  if (!d)
    return errno == ENOENT ? 0 : -1;
  rmdir_children(path, d);
  closedir(d);
  if (rmdir(path) == 0) {
    io_cache_invalidate(path);
    return 0;
  }
  if (errno == ENOENT) {
    io_cache_invalidate(path);
    return 0;
  }
  return -1;
}

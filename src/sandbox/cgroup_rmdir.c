#include "cgroup.h"
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
    if (e->d_type == DT_DIR)
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
  return rmdir(path) == 0 || errno == ENOENT ? 0 : -1;
}

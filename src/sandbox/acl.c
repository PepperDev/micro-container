#include "acl.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>
// cppcheck-suppress missingIncludeSystem
#include <stdint.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>

struct acl_ent {
  // cppcheck-suppress unusedStructMember
  uint32_t v;
  uint16_t e[20];
};

static int match_grp(const struct stat *st, const gid_t *groups, size_t ngroups)
{
  for (size_t i = 0; i < ngroups; i++)
    if (st->st_gid == groups[i] && (st->st_mode & S_IWGRP))
      return 1;
  return 0;
}

static int acl_skip(const struct stat *st, uid_t uid, gid_t gid, const gid_t *groups, size_t ngroups)
{
  if (st->st_uid == uid && (st->st_mode & S_IWUSR))
    return 1;
  if (st->st_gid == gid && (st->st_mode & S_IWGRP))
    return 1;
  return match_grp(st, groups, ngroups);
}

static const struct acl_ent acl_tmpl = {
  2, {1, 6, 0, 0, 2, 6, 0, 0, 4, 6, 0, 0, 16, 6, 0, 0, 32, 6, 0, 0}
};

int acl_set(const char *path, uid_t uid, gid_t gid, const gid_t *groups, size_t ngroups)
{
  int fd = sys_open_rdonly(path, SYS_OPEN_NOFOLLOW);
  if (fd < 0)
    return -1;
  int r = acl_set_fd(fd, uid, gid, groups, ngroups);
  close(fd);
  return r;
}

static int fd_setxattr(int fd, const char *name, const void *value, size_t size)
{
  char path[64];
  int n = snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
  if (n <= 0 || (size_t)n >= sizeof(path))
    return -1;
  return sys_setxattr(path, name, value, size, 0);
}

// cppcheck-suppress staticFunction
int acl_set_fd(int fd, uid_t uid, gid_t gid, const gid_t *groups, size_t ngroups)
{
  struct stat st;
  int r = sys_fstat(fd, &st);
  if (r)
    return -1;
  if (!S_ISSOCK(st.st_mode))
    return 0;
  if (uid == 0 || acl_skip(&st, uid, gid, groups, ngroups))
    return 0;

  struct acl_ent a = acl_tmpl;
  memcpy(a.e + 6, &uid, 4);

  r = fd_setxattr(fd, "system.posix_acl_access", &a, sizeof(a));
  return r ? -1 : 0;
}

int acl_guest_set(const char *path, uid_t uid, gid_t gid, const gid_t *groups, size_t ngroups)
{
  return acl_set(path, uid, gid, groups, ngroups);
}

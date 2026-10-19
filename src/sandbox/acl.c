#include "acl.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
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
  struct stat st;
  int r = sys_stat(path, &st);
  if (r)
    return -1;
  if (!S_ISSOCK(st.st_mode))
    return 0;
  if (uid == 0 || acl_skip(&st, uid, gid, groups, ngroups))
    return 0;

  struct acl_ent a = acl_tmpl;
  memcpy(a.e + 6, &uid, 4);

  r = sys_setxattr(path, "system.posix_acl_access", &a, sizeof(a), 0);
  return r ? -1 : 0;
}

int acl_guest_set(const char *path, uid_t uid, gid_t gid, const gid_t *groups, size_t ngroups)
{
  return acl_set(path, uid, gid, groups, ngroups);
}

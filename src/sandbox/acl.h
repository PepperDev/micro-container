#ifndef CAGE_ACL_H
#define CAGE_ACL_H

// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

int acl_set(const char *path, uid_t uid, gid_t gid, const gid_t * groups, size_t ngroups);
int acl_set_fd(int fd, uid_t uid, gid_t gid, const gid_t * groups, size_t ngroups);
int acl_guest_set(const char *path, uid_t uid, gid_t gid, const gid_t * groups, size_t ngroups);

#endif

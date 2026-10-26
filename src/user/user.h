#ifndef CAGE_USER_H
#define CAGE_USER_H

// cppcheck-suppress missingIncludeSystem
#include <stddef.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

typedef struct user_t user_t;

typedef struct {
  const gid_t *groups;
  size_t count;
} group_list_t;

user_t *user_new(const char *user_str, const char *group_str, const char *rootfs);
user_t *user_root_new(const char *rootfs);
void user_resolve(user_t * u);
uid_t get_uid(const user_t * u);
const char *get_name(const user_t * u);
const char *get_home(const user_t * u);
const char *get_shell(const user_t * u);
gid_t get_gid(const user_t * u);
group_list_t get_groups(const user_t * u);
void user_free(user_t * u);

#endif

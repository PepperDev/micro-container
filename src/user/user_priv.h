#ifndef USER_PRIV_H
#define USER_PRIV_H

#include "user.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <pwd.h>

#define NOBODY_ID 65534

typedef struct {
  char *name;
  gid_t gid;
} group_token_t;

struct user_t {
  uid_t uid;
  int uid_filled;
  int name_given;
  char *name;

  group_token_t *group_tokens;
  size_t group_tokens_count;

  gid_t gid;
  int gid_filled;
  gid_t *groups;
  size_t groups_count;

  gid_t passwd_gid;
  char *home;
  char *shell;

  char *rootfs;

  int passwd_parsed;
  int group_parsed;
};

FILE *open_db(const char *rootfs, const char *file);
void resolve_passwd(user_t * u);
void resolve_groups(user_t * u);

#endif

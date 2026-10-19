#include "user_priv.h"
#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <pwd.h>
// cppcheck-suppress missingIncludeSystem
#include <grp.h>

static void resolve_passwd_nodb(user_t *u)
{
  if (!u->uid_filled) {
    u->uid = 65534;
    u->uid_filled = 1;
    u->passwd_gid = 65534;
    return;
  }
  if (u->uid != 0) {
    u->passwd_gid = u->uid;
  } else {
    u->name = mem_strdup("root");
    u->home = mem_strdup("/root");
    u->passwd_gid = 0;
  }
}

static void resolve_by_uid(user_t *u, FILE *pf)
{
  const struct passwd *pw;
  rewind(pf);
  while ((pw = fgetpwent(pf)) != NULL) {
    if (pw->pw_uid == u->uid) {
      u->name = mem_strdup(pw->pw_name);
      u->home = mem_strdup(pw->pw_dir);
      u->shell = mem_strdup(pw->pw_shell);
      u->passwd_gid = pw->pw_gid;
      return;
    }
  }
  if (u->uid == 0) {
    u->name = mem_strdup("root");
    u->home = mem_strdup("/root");
    u->passwd_gid = 0;
  } else {
    u->passwd_gid = u->uid;
  }
}

static void assign_from_passwd(user_t *u, const struct passwd *pw)
{
  u->uid = pw->pw_uid;
  u->passwd_gid = pw->pw_gid;
  u->home = mem_strdup(pw->pw_dir);
  u->shell = mem_strdup(pw->pw_shell);
  u->uid_filled = 1;
}

static void resolve_by_name(user_t *u, FILE *pf)
{
  const struct passwd *pw;
  rewind(pf);
  while ((pw = fgetpwent(pf)) != NULL)
    if (strcmp(pw->pw_name, u->name) == 0) {
      assign_from_passwd(u, pw);
      return;
    }
  rewind(pf);
  while ((pw = fgetpwent(pf)) != NULL)
    if (strcmp(pw->pw_name, "nobody") == 0) {
      assign_from_passwd(u, pw);
      return;
    }
  u->uid = 65534;
  u->passwd_gid = 65534;
  u->uid_filled = 1;
}

void resolve_passwd(user_t *u)
{
  if (u->passwd_parsed)
    return;
  FILE *pf = open_db(u->rootfs, "etc/passwd");
  if (!pf) {
    resolve_passwd_nodb(u);
  } else {
    if (u->uid_filled)
      resolve_by_uid(u, pf);
    else
      resolve_by_name(u, pf);
    fclose(pf);
  }
  u->passwd_parsed = 1;
}

static int has_named_tokens(const group_token_t *tokens, size_t count)
{
  for (size_t i = 0; i < count; i++)
    if (tokens[i].name)
      return 1;
  return 0;
}

static void resolve_named_in_group(user_t *u, FILE *gf)
{
  const struct group *gr;
  while ((gr = fgetgrent(gf)) != NULL) {
    for (size_t i = 0; i < u->group_tokens_count; i++) {
      if (!u->group_tokens[i].name)
        continue;
      if (strcmp(gr->gr_name, u->group_tokens[i].name) == 0) {
        u->group_tokens[i].gid = gr->gr_gid;
        free(u->group_tokens[i].name);
        u->group_tokens[i].name = NULL;
        break;
      }
    }
  }
}

static int member_of_group(const char *name, size_t namelen, char **members)
{
  if (!members)
    return 0;
  for (size_t i = 0; members[i]; i++) {
    if (strncmp(members[i], name, namelen) == 0 && members[i][namelen] == '\0')
      return 1;
  }
  return 0;
}

static void append_group_token(user_t *u, gid_t gid)
{
  group_token_t *ng = realloc(u->group_tokens,
                              (u->group_tokens_count + 1) * sizeof(group_token_t));
  if (!ng)
    return;
  u->group_tokens = ng;
  u->group_tokens[u->group_tokens_count].name = NULL;
  u->group_tokens[u->group_tokens_count].gid = gid;
  u->group_tokens_count++;
}

static void resolve_groups_enum(user_t *u)
{
  resolve_passwd(u);
  u->gid = u->passwd_gid;
  u->gid_filled = 1;
  FILE *gf = open_db(u->rootfs, "etc/group");
  if (!gf) {
    u->group_parsed = 1;
    return;
  }
  struct group *gr;
  size_t namelen = strlen(u->name);
  while ((gr = fgetgrent(gf)) != NULL) {
    if (member_of_group(u->name, namelen, gr->gr_mem))
      append_group_token(u, gr->gr_gid);
  }
  fclose(gf);
  u->group_parsed = 1;
}

static void resolve_groups_by_tokens(user_t *u)
{
  FILE *gf = open_db(u->rootfs, "etc/group");
  if (!gf) {
    u->group_parsed = 1;
    return;
  }
  resolve_named_in_group(u, gf);
  fclose(gf);
  u->group_parsed = 1;
  for (size_t i = 0; i < u->group_tokens_count; i++) {
    if (u->group_tokens[i].name == NULL) {
      u->gid = u->group_tokens[i].gid;
      u->gid_filled = 1;
      return;
    }
  }
}

static int should_enumerate_groups(const user_t *u)
{
  return u->group_tokens_count == 0 && !u->gid_filled && u->name_given && u->name;
}

void resolve_groups(user_t *u)
{
  if (u->group_parsed)
    return;
  if (should_enumerate_groups(u)) {
    resolve_groups_enum(u);
    return;
  }
  if (!has_named_tokens(u->group_tokens, u->group_tokens_count)) {
    u->group_parsed = 1;
    return;
  }
  resolve_groups_by_tokens(u);
}

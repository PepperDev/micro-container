#include "user_priv.h"
#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>

uid_t get_uid(user_t *u)
{
  if (u->uid_filled)
    return u->uid;
  resolve_passwd(u);
  return u->uid;
}

void user_resolve(user_t *u)
{
  resolve_passwd(u);
}

const char *get_name(user_t *u)
{
  if (u->name)
    return u->name;
  if (!u->passwd_parsed)
    resolve_passwd(u);
  return u->name;
}

const char *get_home(user_t *u)
{
  if (!u->passwd_parsed)
    resolve_passwd(u);
  return u->home ? u->home : "/home/.user";
}

const char *get_shell(user_t *u)
{
  if (!u->passwd_parsed)
    resolve_passwd(u);
  return u->shell ? u->shell : "/bin/sh";
}

static void get_gid_from_passwd(user_t *u)
{
  uid_t uid = get_uid(u);
  if (!u->passwd_parsed)
    resolve_passwd(u);
  if (uid != 0)
    u->gid = u->passwd_gid;
  else
    u->gid = uid;
  u->gid_filled = 1;
}

gid_t get_gid(user_t *u)
{
  if (u->gid_filled)
    return u->gid;
  if (u->group_tokens_count > 0 && u->group_tokens[0].name == NULL) {
    u->gid = u->group_tokens[0].gid;
    u->gid_filled = 1;
    return u->gid;
  }
  resolve_groups(u);
  if (u->gid_filled)
    return u->gid;
  get_gid_from_passwd(u);
  return u->gid;
}

static void ensure_primary_gid_in_groups(user_t *u)
{
  if (u->groups) {
    size_t i;
    for (i = 0; i < u->groups_count; i++)
      if (u->groups[i] == u->gid)
        return;
    gid_t *ng = realloc(u->groups, (u->groups_count + 1) * sizeof(gid_t));
    if (ng) {
      u->groups = ng;
      u->groups[u->groups_count] = u->gid;
      u->groups_count++;
    }
    return;
  }
  u->groups = malloc(sizeof(gid_t));
  if (u->groups) {
    u->groups[0] = u->gid;
    u->groups_count = 1;
  }
}

static int gid_already_in_list(const gid_t *list, size_t count, gid_t g)
{
  for (size_t i = 0; i < count; i++)
    if (list[i] == g)
      return 1;
  return 0;
}

static void insert_gid_sorted(user_t *u, gid_t g)
{
  size_t ins = 0;
  while (ins < u->groups_count && u->groups[ins] < g)
    ins++;
  gid_t *ng = realloc(u->groups, (u->groups_count + 1) * sizeof(gid_t));
  if (!ng)
    return;
  u->groups = ng;
  memmove(u->groups + ins + 1, u->groups + ins, (u->groups_count - ins) * sizeof(gid_t));
  u->groups[ins] = g;
  u->groups_count++;
}

static void append_remaining_gids(user_t *u)
{
  for (size_t i = 0; i < u->group_tokens_count; i++) {
    if (u->group_tokens[i].name != NULL)
      continue;
    gid_t g = u->group_tokens[i].gid;
    if (g == u->gid)
      continue;
    if (gid_already_in_list(u->groups, u->groups_count, g))
      continue;
    insert_gid_sorted(u, g);
  }
}

// cppcheck-suppress unusedFunction
const char *get_rootfs(const user_t *u)
{
  return u->rootfs;
}

group_list_t get_groups(user_t *u)
{
  resolve_groups(u);
  get_gid(u);
  ensure_primary_gid_in_groups(u);
  append_remaining_gids(u);
  group_list_t gl = { u->groups, u->groups_count };
  return gl;
}

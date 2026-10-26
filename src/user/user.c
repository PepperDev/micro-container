#include "user_priv.h"
#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>

static user_t *mutable_user(const user_t *u)
{
  return (user_t *) u;
}

uid_t get_uid(const user_t *u)
{
  user_t *m = mutable_user(u);
  if (m->uid_filled)
    return m->uid;
  resolve_passwd(m);
  return m->uid;
}

void user_resolve(user_t *u)
{
  resolve_passwd(u);
}

const char *get_name(const user_t *u)
{
  user_t *m = mutable_user(u);
  if (m->name)
    return m->name;
  if (!m->passwd_parsed)
    resolve_passwd(m);
  return m->name;
}

const char *get_home(const user_t *u)
{
  user_t *m = mutable_user(u);
  if (!m->passwd_parsed)
    resolve_passwd(m);
  return m->home ? m->home : "/home/.user";
}

const char *get_shell(const user_t *u)
{
  user_t *m = mutable_user(u);
  if (!m->passwd_parsed)
    resolve_passwd(m);
  return m->shell ? m->shell : "/bin/sh";
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

gid_t get_gid(const user_t *u)
{
  user_t *m = mutable_user(u);
  if (m->gid_filled)
    return m->gid;
  if (m->group_tokens_count > 0 && m->group_tokens[0].name == NULL) {
    m->gid = m->group_tokens[0].gid;
    m->gid_filled = 1;
    return m->gid;
  }
  resolve_groups(m);
  if (m->gid_filled)
    return m->gid;
  get_gid_from_passwd(m);
  return m->gid;
}

static int gid_already_in_list(const gid_t *list, size_t count, gid_t g)
{
  for (size_t i = 0; i < count; i++)
    if (list[i] == g)
      return 1;
  return 0;
}

static void ensure_primary_gid_in_groups(user_t *u)
{
  if (u->groups) {
    if (gid_already_in_list(u->groups, u->groups_count, u->gid))
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

group_list_t get_groups(const user_t *u)
{
  user_t *m = mutable_user(u);
  resolve_groups(m);
  get_gid(m);
  ensure_primary_gid_in_groups(m);
  append_remaining_gids(m);
  group_list_t gl = { m->groups, m->groups_count };
  return gl;
}

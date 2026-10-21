#include "user_priv.h"
#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <stdint.h>

static int all_digits(const char *s)
{
  if (!s || *s == '\0')
    return 0;
  for (; *s; s++)
    if (*s < '0' || *s > '9')
      return 0;
  return 1;
}

static int parse_id(const char *s, unsigned long *out)
{
  char *end;
  errno = 0;
  unsigned long v = strtoul(s, &end, 10);
  if (errno == ERANGE || *end != '\0' || v > UINT32_MAX)
    return -1;
  *out = v;
  return 0;
}

static int count_tokens(const char *s)
{
  if (!s || *s == '\0')
    return 0;
  int n = 1;
  for (const char *p = s; *p; p++)
    if (*p == ':')
      n++;
  return n;
}

static int parse_one_token(group_token_t *t, const char *s, size_t len)
{
  char b[256];
  t->name = NULL;
  t->gid = 0;
  if (len == 0 || len >= 256)
    return -1;
  memcpy(b, s, len);
  b[len] = '\0';
  if (all_digits(b)) {
    unsigned long gid;
    if (parse_id(b, &gid) != 0)
      return -1;
    t->gid = (gid_t) gid;
    return 0;
  }
  t->name = malloc(len + 1);
  if (!t->name)
    return -1;
  memcpy(t->name, s, len);
  t->name[len] = '\0';
  return 0;
}

static const char *skip_colon(const char *p)
{
  while (*p && *p != ':')
    p++;
  return p;
}

static int fill_tokens(group_token_t *t, size_t *n, const char *s)
{
  for (const char *a = s;;) {
    const char *end = skip_colon(a);
    size_t len = (size_t)(end - a);
    if (len == 0 || parse_one_token(&t[*n], a, len) != 0)
      return -1;
    (*n)++;
    if (!*end)
      return 0;
    a = end + 1;
  }
}

static int append_tokens(group_token_t **t, size_t *n, const char *s)
{
  int nt = count_tokens(s);
  if (nt == 0)
    return 0;
  group_token_t *ng = realloc(*t, (*n + nt) * sizeof(group_token_t));
  if (!ng)
    return -1;
  *t = ng;
  return fill_tokens(*t, n, s);
}

static int store_identity(user_t *u, const char *ub)
{
  if (all_digits(ub)) {
    unsigned long uid;
    if (parse_id(ub, &uid) != 0)
      return -1;
    u->uid_filled = 1;
    u->uid = (uid_t) uid;
  } else {
    u->name = mem_strdup(ub);
    u->name_given = 1;
  }
  return 0;
}

static int parse_user_identity(user_t *u, const char *s, const char *c)
{
  size_t ulen = c ? (size_t)(c - s) : strlen(s);
  if (ulen == 0 || ulen >= 256)
    return -1;
  char ub[256];
  memcpy(ub, s, ulen);
  ub[ulen] = '\0';
  return store_identity(u, ub);
}

static int parse_user_body(user_t *u, const char *s, const char *g)
{
  if (!s || *s == '\0') {
    u->uid_filled = 1;
    u->uid = 0;
    return append_tokens(&u->group_tokens, &u->group_tokens_count, g);
  }
  const char *colon = strchr(s, ':');
  if (parse_user_identity(u, s, colon) != 0)
    return -1;
  if (append_tokens(&u->group_tokens, &u->group_tokens_count, g) != 0)
    return -1;
  if (colon)
    return append_tokens(&u->group_tokens, &u->group_tokens_count, colon + 1);
  return 0;
}

user_t *user_new(const char *s, const char *g, const char *r)
{
  user_t *u = calloc(1, sizeof(struct user_t));
  if (!u)
    return NULL;
  if (r)
    u->rootfs = mem_strdup(r);
  if (parse_user_body(u, s, g) != 0) {
    user_free(u);
    free(u);
    return NULL;
  }
  return u;
}

user_t *user_root_new(const char *r)
{
  user_t *u = calloc(1, sizeof(struct user_t));
  if (!u)
    return NULL;
  u->uid = 0;
  u->uid_filled = 1;
  u->gid = 0;
  u->gid_filled = 1;
  if (r)
    u->rootfs = mem_strdup(r);
  return u;
}

void user_free(user_t *u)
{
  if (!u)
    return;
  free(u->name);
  free(u->home);
  free(u->shell);
  free(u->groups);
  free(u->rootfs);
  if (u->group_tokens) {
    for (size_t i = 0; i < u->group_tokens_count; i++)
      free(u->group_tokens[i].name);
    free(u->group_tokens);
  }
  memset(u, 0, sizeof(*u));
}

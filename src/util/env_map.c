#include "env.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>

#define MAP_INIT_CAP 16

static uint32_t fnv1a(const char *s)
{
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (unsigned char)*s++;
    h *= 16777619u;
  }
  return h;
}

static int alloc_map(env_map_t *m, size_t nc)
{
  m->hashes = calloc(nc, sizeof(uint32_t));
  m->keys = calloc(nc, sizeof(char *));
  m->vals = calloc(nc, sizeof(char *));
  m->used = calloc(nc, sizeof(int));
  if (!m->hashes || !m->keys || !m->vals || !m->used) {
    free(m->hashes);
    free(m->keys);
    free(m->vals);
    free(m->used);
    return 0;
  }
  m->cap = nc;
  m->len = 0;
  return 1;
}

static void map_rehash(const env_map_t *src, env_map_t *dst)
{
  for (size_t i = 0; i < src->cap; i++) {
    if (!src->used[i])
      continue;
    size_t idx = src->hashes[i] % dst->cap;
    while (dst->used[idx])
      idx = (idx + 1) % dst->cap;
    dst->hashes[idx] = src->hashes[i];
    dst->keys[idx] = src->keys[i];
    dst->vals[idx] = src->vals[i];
    dst->used[idx] = 1;
  }
}

static int map_resize(env_map_t *m, size_t nc)
{
  env_map_t n;
  if (!alloc_map(&n, nc))
    return 0;
  map_rehash(m, &n);
  n.len = m->len;
  free(m->hashes);
  free(m->keys);
  free(m->vals);
  free(m->used);
  *m = n;
  return 1;
}

void env_map_init(env_map_t *m)
{
  memset(m, 0, sizeof(*m));
  map_resize(m, MAP_INIT_CAP);
}

void env_map_free(env_map_t *m)
{
  for (size_t i = 0; i < m->cap; i++)
    if (m->used[i]) {
      free(m->keys[i]);
      free(m->vals[i]);
    }
  free(m->hashes);
  free(m->keys);
  free(m->vals);
  free(m->used);
  memset(m, 0, sizeof(*m));
}

static void maybe_grow(env_map_t *m)
{
  if (m->len >= m->cap * 7 / 10)
    map_resize(m, m->cap + (m->cap >= 64 ? m->cap : 64));
}

static int map_lookup(const env_map_t *m, uint32_t h, const char *k, size_t *out)
{
  size_t i = h % m->cap;
  while (m->used[i]) {
    if (m->hashes[i] == h && !strcmp(m->keys[i], k)) {
      *out = i;
      return 1;
    }
    i = (i + 1) % m->cap;
  }
  *out = i;
  return 0;
}

void env_map_put(env_map_t *m, const char *k, const char *v)
{
  if (!m->hashes)
    return;
  maybe_grow(m);
  size_t i;
  uint32_t h = fnv1a(k);
  if (map_lookup(m, h, k, &i)) {
    free(m->vals[i]);
    m->vals[i] = strdup(v);
    return;
  }
  m->hashes[i] = h;
  m->keys[i] = strdup(k);
  m->vals[i] = strdup(v);
  m->used[i] = 1;
  m->len++;
}

const char *env_map_get(const env_map_t *m, const char *k)
{
  if (!m->hashes || !m->cap)
    return NULL;
  size_t i;
  return map_lookup(m, fnv1a(k), k, &i) ? m->vals[i] : NULL;
}

static char *build_kv(const char *k, const char *v)
{
  size_t kl = strlen(k), vl = strlen(v);
  char *s = malloc(kl + vl + 2);
  if (!s)
    return NULL;
  memcpy(s, k, kl);
  s[kl] = '=';
  memcpy(s + kl + 1, v, vl + 1);
  return s;
}

char **env_map_build(const env_map_t *m, size_t *out)
{
  char **a = malloc((m->len + 1) * sizeof(char *));
  if (!a)
    return NULL;
  size_t j = 0;
  for (size_t i = 0; i < m->cap; i++) {
    if (!m->used[i])
      continue;
    a[j] = build_kv(m->keys[i], m->vals[i]);
    j++;
  }
  a[j] = NULL;
  if (out)
    *out = j;
  return a;
}

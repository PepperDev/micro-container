#include "io.h"
#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

#define IO_CACHE_MAX 32

struct io_cache_ent {
  char *path;
  int res;
};

static struct io_cache_ent io_cache[IO_CACHE_MAX];

static int io_cache_lookup(const char *path)
{
  for (int i = 0; i < IO_CACHE_MAX; i++) {
    if (io_cache[i].path && strcmp(io_cache[i].path, path) == 0)
      return io_cache[i].res;
  }
  return 2;
}

static int io_cache_find(const char *path)
{
  for (int i = 0; i < IO_CACHE_MAX; i++) {
    if (io_cache[i].path && strcmp(io_cache[i].path, path) == 0)
      return i;
  }
  return -1;
}

static int io_cache_empty(void)
{
  for (int i = 0; i < IO_CACHE_MAX; i++) {
    if (!io_cache[i].path)
      return i;
  }
  return -1;
}

static void io_cache_store(const char *path, int res)
{
  int i = io_cache_find(path);
  if (i >= 0) {
    io_cache[i].res = res;
    return;
  }
  i = io_cache_empty();
  if (i < 0) {
    /* evict oldest simple */
    mem_free(io_cache[0].path);
    i = 0;
  }
  io_cache[i].path = mem_strdup(path);
  io_cache[i].res = res;
}

int io_exists(const char *path)
{
  int c = io_cache_lookup(path);
  if (c != 2)
    return c;
  int r = access(path, F_OK);
  int res;
  if (r == 0)
    res = 0;
  else if (errno == ENOENT || errno == EACCES)
    res = 1;
  else {
    fprintf(stderr, "cage: unable to access %s\n", path);
    res = -1;
  }
  if (res >= 0)
    io_cache_store(path, res);
  return res;
}

static int is_prefix_dir(const char *prefix, const char *path)
{
  size_t n = strlen(prefix);
  return n < strlen(path) && strncmp(prefix, path, n) == 0 && path[n] == '/';
}

static int path_is_related(const char *a, const char *b)
{
  return strcmp(a, b) == 0 || is_prefix_dir(a, b) || is_prefix_dir(b, a);
}

void io_cache_invalidate(const char *path)
{
  if (!path)
    return;
  for (int i = 0; i < IO_CACHE_MAX; i++) {
    if (!io_cache[i].path || !path_is_related(io_cache[i].path, path))
      continue;
    mem_free(io_cache[i].path);
    io_cache[i].path = NULL;
    io_cache[i].res = 0;
  }
}

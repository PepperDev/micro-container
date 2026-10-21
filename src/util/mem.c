#include "mem.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdarg.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

#define MEM_ERR "cage: unable to allocate memory\n"
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void *mem_alloc(size_t size)
{
  void *p = malloc(size);
  if (!p) {
    fprintf(stderr, MEM_ERR);
    _exit(1);
  }
  return p;
}

void *mem_realloc(void *ptr, size_t size)
{
  void *p = realloc(ptr, size);
  if (!p) {
    fprintf(stderr, MEM_ERR);
    _exit(1);
  }
  return p;
}

void mem_free(void *ptr)
{
  free(ptr);
}

char *mem_strdup(const char *s)
{
  size_t n = strlen(s) + 1;
  char *p = mem_alloc(n);
  memcpy(p, s, n);
  return p;
}

char *mem_cat(const void *s1, size_t n1, ...)
{
  buf_t buf = BUF_INIT;
  buf_append(&buf, s1, n1);
  va_list ap;
  va_start(ap, n1);
  const void *p;
  while ((p = va_arg(ap, const void *)) != NULL) {
    size_t n = va_arg(ap, size_t);
    buf_append(&buf, p, n);
  }
  va_end(ap);
  buf_append_c(&buf, '\0');
  return buf_done(&buf);
}

char *mem_path(const char *base, const char *append)
{
  size_t blen = strlen(base);
  size_t alen = strlen(append);

  while (blen > 0 && base[blen - 1] == '/')
    blen--;
  while (alen > 0 && append[0] == '/') {
    append++;
    alen--;
  }

  char *result = mem_alloc(blen + 1 + alen + 1);
  memcpy(result, base, blen);
  result[blen] = '/';
  memcpy(result + blen + 1, append, alen);
  result[blen + 1 + alen] = '\0';
  return result;
}

int mem_path_escapes(const char *rel)
{
  const char *p = rel;
  for (;;) {
    const char *slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - p) : strlen(p);
    if (len == 2 && strncmp(p, "..", 2) == 0)
      return 1;
    if (!slash)
      return 0;
    p = slash + 1;
  }
}

int buf_grow(buf_t *b, size_t need)
{
  if (b->len + need <= b->cap)
    return 0;

  size_t new_cap = b->cap + MAX(MAX(b->cap, (size_t)64), b->len + need - b->cap);
  new_cap = (new_cap + 63) & ~(size_t)63;

  char *new_data = mem_realloc(b->data, new_cap);
  b->data = new_data;
  b->cap = new_cap;
  return 0;
}

/* cppcheck-suppress staticFunction */
int buf_append(buf_t *b, const void *data, size_t len)
{
  buf_grow(b, len);
  memcpy(b->data + b->len, data, len);
  b->len += len;
  return 0;
}

int buf_append_c(buf_t *b, char c)
{
  buf_grow(b, 1);
  b->data[b->len++] = c;
  return 0;
}

char *buf_done(buf_t *b)
{
  char *data = b->data;
  b->cap = 0;
  b->len = 0;
  b->data = NULL;
  return data;
}

void buf_free(buf_t *b)
{
  free(b->data);
  b->cap = 0;
  b->len = 0;
  b->data = NULL;
}

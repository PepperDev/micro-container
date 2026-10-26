#include "io.h"
#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

#define CHUNK 4096

static void read_error(const char *op, const char *path, int quiet)
{
  if (!quiet)
    fprintf(stderr, "cage: unable to %s %s\n", op, path);
}

static size_t read_chunk_size(const buf_t *b, size_t cap)
{
  if (!cap || b->len > cap || cap - b->len >= CHUNK)
    return CHUNK;
  return cap - b->len + 1;
}

static int append_read(buf_t *b, ssize_t n, const char *path, int quiet, size_t cap)
{
  b->len += (size_t)n;
  if (!cap || b->len <= cap)
    return 0;
  if (!quiet)
    fprintf(stderr, "cage: %s too large\n", path);
  return -1;
}

static int read_loop(int fd, buf_t *b, const char *path, int quiet, size_t cap)
{
  for (;;) {
    size_t chunk = read_chunk_size(b, cap);
    if (buf_grow(b, chunk) != 0)
      return -1;
    ssize_t n = read(fd, b->data + b->len, chunk);
    if (n < 0) {
      read_error("read", path, quiet);
      return -1;
    }
    if (n == 0)
      return 0;
    if (append_read(b, n, path, quiet, cap) != 0)
      return -1;
  }
}

static int buf_terminate(buf_t *b)
{
  if (buf_grow(b, 1) != 0)
    return -1;
  b->data[b->len] = '\0';
  return 0;
}

static int read_file(const char *path, buf_t *b, int quiet, size_t cap)
{
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    read_error("open", path, quiet);
    return -1;
  }

  int r = read_loop(fd, b, path, quiet, cap);
  if (r == 0)
    r = buf_terminate(b);
  close(fd);
  return r;
}

int io_read(const char *path, buf_t *b)
{
  return read_file(path, b, 0, 0);
}

int io_read_cap(const char *path, buf_t *b, size_t cap)
{
  return read_file(path, b, 0, cap);
}

int io_read_quiet(const char *path, buf_t *b)
{
  return read_file(path, b, 1, 0);
}

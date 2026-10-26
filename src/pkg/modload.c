#include "modload.h"
#include "modprobe.h"
#include "compress/decompress.h"
#include "mod_format.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/mman.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/wait.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/module.h>

// Added in Linux 5.17; the release musl toolchains ship 5.15 headers.
#ifndef MODULE_INIT_COMPRESSED_FILE
#define MODULE_INIT_COMPRESSED_FILE 4
#endif

// cppcheck-suppress staticFunction
int finit_module_fd(int fd, unsigned int flags)
{
  long r = sys_finit_module(fd, "", flags);
  if (r != 0) {
    if (errno == EEXIST)
      return 0;
    return -1;
  }
  return 0;
}

// cppcheck-suppress staticFunction
int decompress_magic(const unsigned char *p, size_t n)
{
  const struct mod_format *f = mod_format_by_magic(p, n);
  return f ? f->code : 0;
}

static int write_all(int fd, const char *buf, size_t len)
{
  size_t off = 0;
  while (off < len) {
    ssize_t n = write(fd, buf + off, len - off);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    off += (size_t)n;
  }
  return 0;
}

// cppcheck-suppress staticFunction
int buf_write_to_fd(int fd, const buf_t *data)
{
  if (write_all(fd, data->data, data->len) != 0)
    return -1;
  if (lseek(fd, 0, SEEK_SET) == -1)
    return -1;
  return 0;
}

static int decompress_gzip_to_fd(const char *path, int *out_fd)
{
  buf_t compressed = BUF_INIT;
  if (open_and_read(path, &compressed) != 0)
    return -1;
  buf_t data = BUF_INIT;
  int r = decompress_gzip(compressed.data, compressed.len, &data);
  buf_free(&compressed);
  if (r != 0)
    return -1;

  int fd = memfd_create("cage-mod", 0);
  if (fd < 0) {
    buf_free(&data);
    return -1;
  }
  r = buf_write_to_fd(fd, &data);
  buf_free(&data);
  if (r != 0) {
    close(fd);
    return -1;
  }
  *out_fd = fd;
  return 0;
}

// cppcheck-suppress staticFunction
int pipe_to_fd(FILE *f, int fd)
{
  char buf[8192];
  size_t rbytes;
  while ((rbytes = fread(buf, 1, sizeof(buf), f)) > 0)
    if (write_all(fd, buf, rbytes) != 0)
      return -1;
  return 0;
}

static void exec_filter(const char *prog, const char *path, int write_fd)
{
  if (dup2(write_fd, STDOUT_FILENO) < 0)
    _exit(127);
  close(write_fd);
  execlp(prog, prog, path, (char *)NULL);
  _exit(127);
}

static FILE *open_read_end(int fd, pid_t pid)
{
  FILE *r = fdopen(fd, "r");
  if (!r) {
    close(fd);
    waitpid(pid, NULL, 0);
  }
  return r;
}

static int spawn_filter(const char *prog, const char *path, FILE **out, pid_t *out_pid)
{
  int pfd[2];
  if (pipe(pfd) != 0)
    return -1;
  pid_t pid = fork();
  if (pid < 0) {
    close(pfd[0]);
    close(pfd[1]);
    return -1;
  }
  if (pid == 0) {
    close(pfd[0]);
    exec_filter(prog, path, pfd[1]);
  }
  close(pfd[1]);
  FILE *r = open_read_end(pfd[0], pid);
  if (!r)
    return -1;
  *out = r;
  *out_pid = pid;
  return 0;
}

static int child_ok(pid_t pid)
{
  int status = 0;
  if (waitpid(pid, &status, 0) < 0)
    return 0;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int decompress_xzstd_to_fd(int fd, const char *path, const char *filter)
{
  FILE *zf;
  pid_t pid;
  if (spawn_filter(filter, path, &zf, &pid) != 0)
    return -1;
  int r = pipe_to_fd(zf, fd);
  fclose(zf);
  if (!child_ok(pid) || r != 0)
    return -1;
  return lseek(fd, 0, SEEK_SET) == -1 ? -1 : 0;
}

// cppcheck-suppress staticFunction
int open_module(const char *path, int *out_fd)
{
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  *out_fd = fd;
  return 0;
}

static int finish_finit(int fd)
{
  int r = finit_module_fd(fd, 0);
  int saved = errno;
  close(fd);
  if (r != 0) {
    fprintf(stderr, "cage: finit_module failed: %s\n", strerror(saved));
    return -1;
  }
  return 0;
}

// cppcheck-suppress staticFunction
int load_decompressed(const char *path, int comp)
{
  const struct mod_format *f = mod_format_by_code(comp);
  if (!f)
    return -1;
  int fd;
  if (!f->filter) {
    if (decompress_gzip_to_fd(path, &fd) != 0)
      return -1;
  } else {
    fd = memfd_create("cage-mod", 0);
    if (fd < 0)
      return -1;
    if (decompress_xzstd_to_fd(fd, path, f->filter) != 0) {
      close(fd);
      return -1;
    }
  }
  return finish_finit(fd);
}

static int load_compressed_module(int fd, const char *path, int comp)
{
  lseek(fd, 0, SEEK_SET);
  int r = finit_module_fd(fd, MODULE_INIT_COMPRESSED_FILE);
  if (r == 0) {
    close(fd);
    return 0;
  }
  int saved = errno;
  close(fd);
  if (saved != EOPNOTSUPP) {
    fprintf(stderr, "cage: finit_module failed: %s\n", strerror(saved));
    return -1;
  }
  return load_decompressed(path, comp);
}

int load_module_path(const char *path)
{
  int fd;
  if (open_module(path, &fd) != 0)
    return -1;

  unsigned char magic[4];
  ssize_t n = read(fd, magic, 4);
  int comp = decompress_magic(magic, n > 0 ? (size_t)n : 0);

  if (comp)
    return load_compressed_module(fd, path, comp);

  lseek(fd, 0, SEEK_SET);
  return finish_finit(fd);
}

#include "modload.h"
#include "modprobe.h"
#include "compress/decompress.h"
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
int decompress_magic(const unsigned char *p)
{
  static const unsigned char gzip[] = { 0x1F, 0x8B };
  static const unsigned char xz[] = { 0xFD, 0x37 };
  static const unsigned char zstd[] = { 0x28, 0xB5, 0x2F, 0xFD };
  if (memcmp(p, gzip, 2) == 0)
    return 1;
  if (memcmp(p, xz, 2) == 0)
    return 2;
  if (memcmp(p, zstd, 4) == 0)
    return 3;
  return 0;
}

// cppcheck-suppress staticFunction
int buf_write_to_fd(int fd, const buf_t *data)
{
  ssize_t written = 0;
  while (written < (ssize_t) data->len) {
    ssize_t n = write(fd, data->data + written, data->len - (size_t)written);
    if (n < 0)
      return -1;
    written += n;
  }
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
  while ((rbytes = fread(buf, 1, sizeof(buf), f)) > 0) {
    size_t off = 0;
    while (off < rbytes) {
      ssize_t wb = write(fd, buf + off, rbytes - off);
      if (wb < 0)
        return -1;
      off += (size_t)wb;
    }
  }
  return 0;
}

static int decompress_xzstd_to_fd(int fd, const char *path, int comp)
{
  size_t plen = strlen(path);
  char cmd[plen + 12];
  const char *prog = (comp == 2) ? "xzcat" : "zstdcat";
  snprintf(cmd, sizeof(cmd), "%s \"%s\"", prog, path);
  FILE *zf = popen(cmd, "r");
  if (!zf)
    return -1;
  int r = pipe_to_fd(zf, fd);
  int pstat = pclose(zf);
  if (r != 0 || pstat != 0 || lseek(fd, 0, SEEK_SET) == -1)
    return -1;
  return 0;
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

// cppcheck-suppress staticFunction
int load_decompressed(const char *path, int comp)
{
  int fd;
  if (comp == 1) {
    if (decompress_gzip_to_fd(path, &fd) != 0)
      return -1;
  } else {
    fd = memfd_create("cage-mod", 0);
    if (fd < 0)
      return -1;
    if (decompress_xzstd_to_fd(fd, path, comp) != 0) {
      close(fd);
      return -1;
    }
  }
  int r = finit_module_fd(fd, 0);
  int saved = errno;
  close(fd);
  if (r != 0) {
    fprintf(stderr, "cage: finit_module failed: %s\n", strerror(saved));
    return -1;
  }
  return 0;
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
  int comp = (n >= 2) ? decompress_magic(magic) : 0;

  if (comp)
    return load_compressed_module(fd, path, comp);

  lseek(fd, 0, SEEK_SET);
  int r = finit_module_fd(fd, 0);
  int saved = errno;
  close(fd);
  if (r != 0) {
    fprintf(stderr, "cage: finit_module failed: %s\n", strerror(saved));
    return -1;
  }
  return 0;
}

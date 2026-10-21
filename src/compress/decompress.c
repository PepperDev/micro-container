#include "decompress.h"
#include "inflate.h"
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

#define CHUNK 4096

static int check_gzip_magic(const unsigned char *p, size_t in_len)
{
  if (in_len < 18)
    return -1;
  if (p[0] != 0x1F || p[1] != 0x8B)
    return -1;
  if (p[2] != 8)
    return -1;
  return 0;
}

static int advance(size_t *o, size_t n, size_t in_len)
{
  if (*o + n > in_len)
    return -1;
  *o += n;
  return 0;
}

static int skip_fextra(const unsigned char *p, size_t in_len, size_t *o)
{
  if (*o + 2 > in_len)
    return -1;
  size_t xlen = (unsigned)p[*o] | ((unsigned)p[*o + 1] << 8);
  return advance(o, 2 + xlen, in_len);
}

static int skip_string(const unsigned char *p, size_t in_len, size_t *o)
{
  while (*o < in_len && p[*o])
    (*o)++;
  if (*o >= in_len)
    return -1;
  (*o)++;
  return 0;
}

static int skip_gzip_tail(const unsigned char *p, size_t in_len, size_t *o, int flg)
{
  static const int str_flags[] = { 0x08, 0x10 };
  for (size_t i = 0; i < sizeof(str_flags) / sizeof(str_flags[0]); i++)
    if ((flg & str_flags[i]) && skip_string(p, in_len, o) != 0)
      return -1;
  if (flg & 0x02)
    return advance(o, 2, in_len);
  return 0;
}

static int skip_gzip_flags(const unsigned char *p, size_t in_len, size_t *o, int flg)
{
  if ((flg & 0x04) && skip_fextra(p, in_len, o) != 0)
    return -1;
  return skip_gzip_tail(p, in_len, o, flg);
}

static int skip_gzip_header(const unsigned char *p, size_t in_len, size_t *off)
{
  size_t o = 10;
  if (check_gzip_magic(p, in_len) != 0)
    return -1;
  if (skip_gzip_flags(p, in_len, &o, p[3]) != 0 || o >= in_len)
    return -1;
  *off = o;
  return 0;
}

static uint32_t read32le(const unsigned char *p)
{
  uint32_t v = 0;
  for (int i = 0; i < 4; i++)
    v |= (uint32_t) p[i] << (i * 8);
  return v;
}

static int check_gzip_trailer(const unsigned char *p, size_t in_len, size_t data_end, const buf_t *out)
{
  if (data_end + 8 > in_len)
    return -1;
  uint32_t expected_crc = read32le(p + data_end);
  uint32_t expected_len = read32le(p + data_end + 4);
  crc32_init();
  uint32_t actual_crc = crc32_update(0, out->data, out->len);
  if (actual_crc != expected_crc)
    return -1;
  if (expected_len != (uint32_t) out->len)
    return -1;
  return 0;
}

int decompress_gzip(const void *in, size_t in_len, buf_t *out)
{
  const unsigned char *p = (const unsigned char *)in;
  size_t off;
  if (skip_gzip_header(p, in_len, &off) != 0)
    return -1;
  if (off + 8 > in_len)
    return -1;
  struct br br;
  br_init(&br, p + off, in_len - off - 8);
  for (;;) {
    int r = inflate_block(&br, out);
    if (r < 0)
      return -1;
    if (r)
      break;
  }
  return check_gzip_trailer(p, in_len, in_len - 8, out);
}

int open_and_read(const char *path, buf_t *buf)
{
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  ssize_t n;
  do {
    if (buf_grow(buf, CHUNK) != 0) {
      close(fd);
      return -1;
    }
    n = read(fd, buf->data + buf->len, CHUNK);
    if (n < 0) {
      close(fd);
      return -1;
    }
    buf->len += n;
  } while (n == CHUNK);
  close(fd);
  return 0;
}

#include "csum.h"
#include "util.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/random.h>

/* cppcheck-suppress staticFunction */
uint32_t rd32(const unsigned char *p)
{
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

uint16_t rd16(const unsigned char *p)
{
  return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

void wr32(unsigned char *p, uint32_t v)
{
  p[0] = (unsigned char)(v);
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
}

void wr16(unsigned char *p, uint16_t v)
{
  p[0] = (unsigned char)(v);
  p[1] = (unsigned char)(v >> 8);
}

void wr32be(unsigned char *p, uint32_t v)
{
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)(v);
}

int gen_uuid(unsigned char *uuid)
{
  if (getrandom(uuid, 16, 0) != 16)
    return -1;
  uuid[6] = (uuid[6] & 0x0f) | 0x40;
  uuid[8] = (uuid[8] & 0x3f) | 0x80;
  return 0;
}

uint32_t grp_flex_first(uint32_t g)
{
  return (g / 16) * 16;
}

uint32_t grp_flex_base(uint32_t g)
{
  return (uint32_t) grp_flex_first(g) * 32768;
}

/* cppcheck-suppress staticFunction */
int is_power(uint32_t n, uint32_t b)
{
  if (n == 0)
    return 0;
  while (n % b == 0)
    n /= b;
  return n == 1;
}

int has_super(uint32_t g)
{
  if (g <= 1)
    return 1;
  return (g & 1) && (is_power(g, 3) || is_power(g, 5) || is_power(g, 7));
}

int write_at(int fd, const void *buf, size_t len, off_t off)
{
  return pwrite(fd, buf, len, off) == (ssize_t) len ? 0 : -1;
}

int write_zeroes(int fd, off_t off, size_t len)
{
  static const unsigned char z[4096];
  while (len) {
    size_t n = len < sizeof(z) ? len : sizeof(z);
    if (write_at(fd, z, n, off))
      return -1;
    off += (off_t) n;
    len -= n;
  }
  return 0;
}

void inode_set_csum(unsigned char *in, uint32_t ino, uint32_t seed)
{
  unsigned char ino_buf[4], gen_buf[4];
  wr32(ino_buf, ino);
  wr32(gen_buf, rd32(in + 0x64));
  wr16(in + 0x7C, 0);
  wr16(in + 0x82, 0);
  uint32_t c = crc32c_e2fs(seed, ino_buf, 4);
  c = crc32c_e2fs(c, gen_buf, 4);
  c = crc32c_e2fs(c, in, INODE_SIZE);
  wr16(in + 0x7C, (uint16_t) (c & 0xFFFF));
  wr16(in + 0x82, (uint16_t) (c >> 16));
}

void gdt_set_csum(unsigned char *desc, uint32_t g, uint32_t seed)
{
  unsigned char l[4];
  wr16(desc + G_CHECK, 0);
  wr32(l, g);
  uint32_t c = crc32c_e2fs(seed, l, 4);
  c = crc32c_e2fs(c, desc, DESC_SIZE);
  wr16(desc + G_CHECK, (uint16_t) (c & 0xFFFF));
}

void extent_set(unsigned char *p, uint32_t ee_block, uint16_t ee_len, uint64_t ee_start)
{
  wr16(p + 0x00, EXT4_EXTENT_MAGIC);
  wr16(p + 0x02, 1);
  wr16(p + 0x04, 4);
  wr16(p + 0x06, 0);
  wr32(p + 0x08, 0);
  wr32(p + 0x0C, ee_block);
  wr16(p + 0x10, ee_len);
  wr16(p + 0x12, (uint16_t) (ee_start >> 32));
  wr32(p + 0x14, (uint32_t) (ee_start & 0xFFFFFFFF));
}

void mark_bit(unsigned char *bm, uint32_t blk)
{
  bm[blk / 8] |= (unsigned char)(1u << (blk % 8));
}

uint32_t dir_csum(uint32_t seed, const unsigned char *blk, uint32_t ino, uint32_t gen, uint32_t blk_size)
{
  unsigned char ino_buf[4], gen_buf[4];
  wr32(ino_buf, ino);
  wr32(gen_buf, gen);
  uint32_t c = crc32c_e2fs(seed, ino_buf, 4);
  c = crc32c_e2fs(c, gen_buf, 4);
  c = crc32c_e2fs(c, blk, blk_size - 12);
  return c;
}

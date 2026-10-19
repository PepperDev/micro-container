#include "framework.h"
#include "test_util.h"
#include "util/mem.h"
#include "ext4/csum.h"
#include "ext4/ext4.h"
#include "ext4/util.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static void crc16_build(uint16_t *tab)
{
  for (uint32_t i = 0; i < 256; i++) {
    uint16_t c = (uint16_t) i;
    for (int j = 0; j < 8; j++)
      c = (c & 1) ? (uint16_t) (0x8408 ^ (c >> 1)) : (c >> 1);
    tab[i] = c;
  }
}

static uint16_t ext4_crc16_init(uint16_t crc, const void *data, size_t len)
{
  static uint16_t tab[256];
  static int init;
  if (!init) {
    crc16_build(tab);
    init = 1;
  }
  const unsigned char *p = (const unsigned char *)data;
  for (size_t i = 0; i < len; i++)
    crc = (uint16_t) ((crc >> 8) ^ tab[(crc ^ p[i]) & 0xFF]);
  return crc;
}

static uint16_t ext4_crc16(const void *data, size_t len)
{
  return ext4_crc16_init(0, data, len);
}

#define S_EXT4_MAGIC_OFFSET 0x438
#define S_EXT4_MAGIC        0xEF53

static int check_ext4_magic(const char *path)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  uint16_t magic;
  ssize_t n = pread(fd, &magic, sizeof(magic), S_EXT4_MAGIC_OFFSET);
  close(fd);

  if (n != (ssize_t) sizeof(magic))
    return -1;
  return magic == S_EXT4_MAGIC ? 0 : -1;
}

static void test_crc32c_empty(void)
{
  ASSERT_EQ(ext4_crc32c(0, "", 0), 0);
}

static void test_crc32c_known(void)
{
  const char *s = "123456789";
  ASSERT_EQ(ext4_crc32c(0, s, strlen(s)), 0xE3069283);
}

static void test_crc16_empty(void)
{
  ASSERT_EQ(ext4_crc16("", 0), 0);
}

static void test_crc16_known(void)
{
  const char *s = "123456789";
  ASSERT_EQ(ext4_crc16(s, strlen(s)), 0x2189);
}

static void test_ext4_create_img_success(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "ext4.img");

  ASSERT_EQ(ext4_create_img(path), 0);

  struct stat st;
  ASSERT_EQ(stat(path, &st), 0);
  ASSERT_EQ(st.st_size, 10737418240);

  ASSERT_EQ(check_ext4_magic(path), 0);

  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_ext4_backup_superblock(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "ext4.img");

  ASSERT_EQ(ext4_create_img(path), 0);

  int fd = open(path, O_RDONLY);
  ASSERT(fd >= 0);

  off_t base = (off_t) BLKS_PER_GRP * BLK_SIZE;
  unsigned char sb[1024];
  ASSERT_EQ(pread(fd, sb, sizeof(sb), base), (ssize_t) sizeof(sb));
  ASSERT_EQ(rd16(sb + 0x38), S_EXT4_MAGIC);
  ASSERT_EQ(rd16(sb + 0x5A), 1);
  ASSERT_EQ(rd32(sb + 0x3FC), crc32c_e2fs(~0, sb, 0x3FC));

  unsigned char gdt0[16], gdt1[16];
  ASSERT_EQ(pread(fd, gdt0, sizeof(gdt0), base + BLK_SIZE), (ssize_t) sizeof(gdt0));
  ASSERT_EQ(pread(fd, gdt1, sizeof(gdt1), (off_t) BLK_SIZE), (ssize_t) sizeof(gdt1));
  ASSERT_EQ(memcmp(gdt0, gdt1, sizeof(gdt0)), 0);

  close(fd);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

SUITE(ext4)
{
  RUN_TEST(ext4_create_img_success);
  RUN_TEST(ext4_backup_superblock);
  RUN_TEST(crc32c_empty);
  RUN_TEST(crc32c_known);
  RUN_TEST(crc16_empty);
  RUN_TEST(crc16_known);
}

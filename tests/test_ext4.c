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
  io_rmtree(root);
  mem_free(root);
}

static void test_ext4_image_valid(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "valid.img");

  ASSERT_EQ(ext4_image_valid(path), 0);

  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  ASSERT(fd >= 0);
  ASSERT_EQ(ftruncate(fd, (off_t) BLOCKS * BLK_SIZE), 0);
  unsigned char magic[2];
  wr16(magic, S_EXT4_MAGIC);
  ASSERT_EQ(pwrite(fd, magic, sizeof(magic), SB_OFF + 0x38), (ssize_t) sizeof(magic));
  close(fd);

  ASSERT_EQ(ext4_image_valid(path), 1);

  ASSERT_EQ(truncate(path, 4096), 0);
  ASSERT_EQ(ext4_image_valid(path), 0);

  mem_free(path);
  io_rmtree(root);
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
  io_rmtree(root);
  mem_free(root);
}

static void test_ext4_int_endian(void)
{
  unsigned char b[4];
  wr32(b, 0x12345678u);
  ASSERT_EQ(b[0], 0x78);
  ASSERT_EQ(b[1], 0x56);
  ASSERT_EQ(b[2], 0x34);
  ASSERT_EQ(b[3], 0x12);
  ASSERT_EQ(rd32(b), 0x12345678u);

  wr16(b, 0xABCDu);
  ASSERT_EQ(b[0], 0xCD);
  ASSERT_EQ(b[1], 0xAB);
  ASSERT_EQ(rd16(b), 0xABCDu);

  wr32be(b, 0x12345678u);
  ASSERT_EQ(b[0], 0x12);
  ASSERT_EQ(b[1], 0x34);
  ASSERT_EQ(b[2], 0x56);
  ASSERT_EQ(b[3], 0x78);
}

static void test_ext4_gen_uuid(void)
{
  unsigned char a[16] = { 0 }, b[16] = { 0 };
  ASSERT_EQ(gen_uuid(a), 0);
  ASSERT_EQ(gen_uuid(b), 0);
  ASSERT_EQ(a[6] & 0xF0, 0x40);
  ASSERT_EQ(a[8] & 0xC0, 0x80);
  ASSERT(memcmp(a, b, sizeof(a)) != 0);
}

static void test_ext4_grp_flex(void)
{
  ASSERT_EQ(grp_flex_first(0), 0);
  ASSERT_EQ(grp_flex_first(15), 0);
  ASSERT_EQ(grp_flex_first(16), 16);
  ASSERT_EQ(grp_flex_first(31), 16);
  ASSERT_EQ(grp_flex_base(16), 16u * 32768u);
}

static void test_ext4_power_and_super(void)
{
  ASSERT_EQ(is_power(0, 2), 0);
  ASSERT_EQ(is_power(1, 2), 1);
  ASSERT_EQ(is_power(8, 2), 1);
  ASSERT_EQ(is_power(9, 2), 0);
  ASSERT_EQ(is_power(27, 3), 1);

  ASSERT_EQ(has_super(0), 1);
  ASSERT_EQ(has_super(1), 1);
  ASSERT_EQ(has_super(2), 0);
  ASSERT_EQ(has_super(3), 1);
  ASSERT_EQ(has_super(5), 1);
  ASSERT_EQ(has_super(7), 1);
  ASSERT_EQ(has_super(9), 1);
  ASSERT_EQ(has_super(15), 0);
}

static void test_ext4_write_at_and_zeroes(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "io");
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);

  ASSERT_EQ(write_at(fd, "abc", 3, 5), 0);
  char buf[3] = { 0 };
  ASSERT_EQ(pread(fd, buf, sizeof(buf), 5), (ssize_t) sizeof(buf));
  ASSERT_EQ(memcmp(buf, "abc", 3), 0);

  char fill[8192];
  memset(fill, 'x', sizeof(fill));
  ASSERT_EQ(pwrite(fd, fill, sizeof(fill), 0), (ssize_t) sizeof(fill));
  ASSERT_EQ(write_zeroes(fd, 100, 5000), 0);
  char out[8192];
  ASSERT_EQ(pread(fd, out, sizeof(out), 0), (ssize_t) sizeof(out));
  ASSERT_EQ(memcmp(out, fill, 100), 0);
  for (size_t i = 100; i < 5100; i++)
    ASSERT_EQ(out[i], 0);
  ASSERT_EQ(memcmp(out + 5100, fill + 5100, sizeof(out) - 5100), 0);

  close(fd);
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

static void test_ext4_checksum_helpers(void)
{
  unsigned char inode[INODE_SIZE] = { 0 };
  wr32(inode + 0x64, 7);
  inode_set_csum(inode, 42, 9);
  ASSERT((rd16(inode + 0x7C) | (rd16(inode + 0x82) << 16)) != 0);

  unsigned char gdt[DESC_SIZE] = { 0 };
  gdt_set_csum(gdt, 3, 11);
  ASSERT(rd16(gdt + G_CHECK) != 0);

  unsigned char blk[BLK_SIZE] = { 0 };
  uint32_t c = dir_csum(5, blk, 12, 13, BLK_SIZE);
  ASSERT_EQ(c, dir_csum(5, blk, 12, 13, BLK_SIZE));
}

static void test_ext4_extent_and_bitmap(void)
{
  unsigned char ext[24] = { 0 };
  extent_set(ext, 0x11223344u, 3, 0x5566778899AABBCCull);
  ASSERT_EQ(rd16(ext + 0x00), EXT4_EXTENT_MAGIC);
  ASSERT_EQ(rd16(ext + 0x02), 1);
  ASSERT_EQ(rd32(ext + 0x0C), 0x11223344u);
  ASSERT_EQ(rd16(ext + 0x10), 3);
  ASSERT_EQ(rd16(ext + 0x12), 0x7788);
  ASSERT_EQ(rd32(ext + 0x14), 0x99AABBCC);

  unsigned char bm[2] = { 0 };
  mark_bit(bm, 0);
  mark_bit(bm, 9);
  ASSERT_EQ(bm[0], 0x01);
  ASSERT_EQ(bm[1], 0x02);
}

static void mkfs_fill(struct mkfs *m, uint32_t now, uint32_t seed, unsigned char uuid[16], unsigned char hash[16])
{
  memset(m, 0, sizeof(*m));
  m->fd = -1;
  m->now = now;
  m->seed = seed;
  for (size_t i = 0; i < 16; i++) {
    uuid[i] = (unsigned char)(i + 1);
    hash[i] = (unsigned char)(0xA0 + i);
  }
  m->uuid = uuid;
  m->hash_seed = hash;
}

static void test_ext4_write_superblock(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "sb");
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(ftruncate(fd, SB_OFF + 1024), 0);

  unsigned char uuid[16], hash[16];
  struct mkfs m;
  mkfs_fill(&m, 0x5F5E100u, 0x12345678u, uuid, hash);
  ASSERT_EQ(write_superblock(fd, &m, 500), 0);

  unsigned char sb[1024];
  ASSERT_EQ(pread(fd, sb, sizeof(sb), SB_OFF), (ssize_t) sizeof(sb));
  ASSERT_EQ(rd16(sb + 0x38), 0xEF53);
  ASSERT_EQ(rd32(sb + 0x00), INODES_PER_GRP * GROUPS);
  ASSERT_EQ(rd32(sb + 0x04), BLOCKS);
  ASSERT_EQ(rd32(sb + 0x248), 500);
  ASSERT_EQ(rd32(sb + 0x270), 0x12345678u);
  ASSERT_EQ(memcmp(sb + 0x68, uuid, 16), 0);
  ASSERT_EQ(memcmp(sb + 0xEC, hash, 16), 0);
  ASSERT_NE(rd32(sb + 0x3FC), 0);
  ASSERT_EQ(write_superblock(-1, &m, 500), -1);

  close(fd);
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

static void test_ext4_write_gdt(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "gdt");
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(ftruncate(fd, 3 * BLK_SIZE), 0);

  unsigned char gdt[2 * BLK_SIZE];
  ASSERT_EQ(write_gdt(fd, gdt, GROUPS - 1, 0), 0);

  unsigned char out[2 * BLK_SIZE];
  ASSERT_EQ(pread(fd, out, sizeof(out), BLK_SIZE), (ssize_t) sizeof(out));
  const unsigned char *d0 = out;
  const unsigned char *d1 = out + DESC_SIZE;
  const unsigned char *d16 = out + 16 * DESC_SIZE;
  ASSERT_EQ(rd32(d0 + G_BB_LO), RESV_GDT_BLKS);
  ASSERT_EQ(rd16(d0 + G_FREE_I), INODES_PER_GRP - 11);
  ASSERT_EQ(rd16(d0 + G_USED_D), 1);
  ASSERT_EQ(rd32(d1 + G_BB_LO), RESV_GDT_BLKS + 1);
  ASSERT_EQ(rd16(d1 + G_FLAGS), 0x0004);
  ASSERT_EQ(rd16(d1 + G_FREE_I), INODES_PER_GRP);
  ASSERT_EQ(rd16(d1 + G_USED_D), 0);
  ASSERT_EQ(rd32(d16 + G_BB_LO), grp_flex_base(16));
  ASSERT_EQ(rd16(d16 + G_FREE_B), BLKS_PER_GRP - FLEX_META);
  ASSERT_EQ(write_gdt(-1, gdt, GROUPS - 1, 0), -1);

  close(fd);
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

static void test_ext4_count_backup_blks(void)
{
  ASSERT_EQ(count_backup_blks(), 24);
}

static void test_ext4_write_csum_backups(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "csum");
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  off_t size = (off_t) (grp_ib_blk(GROUPS - 1) + 1) * BLK_SIZE;
  ASSERT_EQ(ftruncate(fd, size), 0);

  unsigned char uuid[16], hash[16];
  struct mkfs m;
  mkfs_fill(&m, 0x5F5E100u, 0x12345678u, uuid, hash);
  ASSERT_EQ(write_superblock(fd, &m, 500), 0);
  unsigned char gdt[2 * BLK_SIZE];
  ASSERT_EQ(write_gdt(fd, gdt, GROUPS - 1, 0), 0);
  ASSERT_EQ(write_csum_backups(fd, gdt, 0x55), 0);

  unsigned char d0[DESC_SIZE];
  ASSERT_EQ(pread(fd, d0, sizeof(d0), BLK_SIZE), (ssize_t) sizeof(d0));
  ASSERT_NE(rd16(d0 + G_CHECK), 0);
  ASSERT_NE(rd16(d0 + G_BB_C_L), 0);
  ASSERT_NE(rd16(d0 + G_IB_C_L), 0);

  off_t b1 = (off_t) BLKS_PER_GRP * BLK_SIZE;
  unsigned char bs[1024];
  ASSERT_EQ(pread(fd, bs, sizeof(bs), b1), (ssize_t) sizeof(bs));
  ASSERT_EQ(rd16(bs + 0x38), 0xEF53);
  unsigned char bd[DESC_SIZE];
  ASSERT_EQ(pread(fd, bd, sizeof(bd), b1 + BLK_SIZE), (ssize_t) sizeof(bd));
  ASSERT_EQ(memcmp(bd, d0, sizeof(bd)), 0);

  unsigned char gdt2[2 * BLK_SIZE];
  ASSERT_EQ(ftruncate(fd, 2 * BLK_SIZE), 0);
  ASSERT_EQ(write_csum_backups(fd, gdt2, 0x55), -1);

  close(fd);
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

SUITE(ext4)
{
  RUN_TEST(ext4_create_img_success);
  RUN_TEST(ext4_image_valid);
  RUN_TEST(ext4_backup_superblock);
  RUN_TEST(crc32c_empty);
  RUN_TEST(crc32c_known);
  RUN_TEST(ext4_int_endian);
  RUN_TEST(ext4_gen_uuid);
  RUN_TEST(ext4_grp_flex);
  RUN_TEST(ext4_power_and_super);
  RUN_TEST(ext4_write_at_and_zeroes);
  RUN_TEST(ext4_checksum_helpers);
  RUN_TEST(ext4_extent_and_bitmap);
  RUN_TEST(ext4_write_superblock);
  RUN_TEST(ext4_write_gdt);
  RUN_TEST(ext4_count_backup_blks);
  RUN_TEST(ext4_write_csum_backups);
}

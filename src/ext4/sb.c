#include "csum.h"
#include "util.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

#define EXT4_SUPER_MAGIC 0xEF53
#define RESIZE_INODE 0x0010u
#define ORPHAN_FILE 0x1000u
#define COMPAT (0x0000002cu | RESIZE_INODE | ORPHAN_FILE)
#define INCOMPAT 0x000022c2u
#define RO_COMPAT 0x0000046bu
#define EXT4_INODE_UNINIT 0x0004
#define EXT4_BLOCK_UNINIT 0x0002

static void fill_jbd2_extent(unsigned char *j, uint32_t jnl_start)
{
  wr16(j + 0, EXT4_EXTENT_MAGIC);
  wr16(j + 2, 1);
  wr16(j + 4, 4);
  wr16(j + 6, 0);
  wr32(j + 8, 0);
  wr32(j + 12, 0);
  wr16(j + 16, 16384);
  wr16(j + 18, (uint16_t) ((uint64_t) jnl_start >> 32));
  wr32(j + 20, (uint32_t) (jnl_start & 0xFFFFFFFF));
  wr32(j + 60, 0);
  wr32(j + 64, 16384u * BLK_SIZE);
}

static void fill_sb_jnl(unsigned char *sb, uint32_t jnl_start)
{
  unsigned char j[68];
  memset(j, 0, sizeof(j));
  fill_jbd2_extent(j, jnl_start);
  memcpy(sb + 0x10C, j, 68);
}

static void sb_write_u32s_a(unsigned char *sb, uint32_t used_blks)
{
  uint32_t const tbl[][2] = {
    {0x000, INODES_PER_GRP * GROUPS}, {0x004, BLOCKS},
    {0x008, 0}, {0x00C, BLOCKS - used_blks},
    {0x010, INODES_PER_GRP * GROUPS - 11}, {0x014, 0},
    {0x018, 2}, {0x01C, 2},
    {0x020, BLKS_PER_GRP}, {0x024, BLKS_PER_GRP},
    {0x028, INODES_PER_GRP}, {0x02C, 0},
    {0x248, used_blks},
  };
  for (size_t i = 0; i < sizeof tbl / sizeof *tbl; i++)
    wr32(sb + tbl[i][0], tbl[i][1]);
}

static void sb_write_u32s_b(unsigned char *sb, uint32_t now, uint32_t seed)
{
  uint32_t const tbl[][2] = {
    {0x030, now}, {0x034, 0},
    {0x040, now}, {0x04C, 1},
    {0x054, 11}, {0x05C, COMPAT},
    {0x060, INCOMPAT}, {0x064, RO_COMPAT},
    {0x0E0, 8}, {0x104, 0},
    {0x108, now}, {0x268, 0},
    {0x270, seed},
  };
  for (size_t i = 0; i < sizeof tbl / sizeof *tbl; i++)
    wr32(sb + tbl[i][0], tbl[i][1]);
}

static void sb_write_u16s(unsigned char *sb)
{
  uint16_t const tbl[][2] = {
    {0x036, 0xFFFF}, {0x038, EXT4_SUPER_MAGIC},
    {0x03A, 1}, {0x03C, 1},
    {0x03E, 0}, {0x050, 0},
    {0x052, 0}, {0x058, INODE_SIZE},
    {0x05A, 0},
    {0x0CE, RESERVED_GDT},
    {0x0FE, DESC_SIZE},
    {0x15C, 32}, {0x15E, 32},
  };
  for (size_t i = 0; i < sizeof tbl / sizeof *tbl; i++)
    wr16(sb + tbl[i][0], tbl[i][1]);
}

static void sb_set_scalars(unsigned char *sb, const unsigned char *hash_seed)
{
  memcpy(sb + 0xEC, hash_seed, 16);
  sb[0x0FC] = 1;
  sb[0x0FD] = 1;
  sb[0x174] = 4;
  sb[0x175] = 1;
  wr32(sb + 0x100, 0x000C);
  wr32(sb + 0x160, 0x0001);
  wr32(sb + 0x280, 12);
}

int write_superblock(int fd, const struct mkfs *m, uint32_t used_blks)
{
  unsigned char sb[1024];
  memset(sb, 0, sizeof(sb));

  sb_write_u32s_a(sb, used_blks);
  sb_write_u32s_b(sb, m->now, m->seed);
  sb_write_u16s(sb);
  memcpy(sb + 0x068, m->uuid, 16);
  sb_set_scalars(sb, m->hash_seed);

  uint32_t flex_meta = 2 * FLEX_GRP_SIZE + FLEX_GRP_SIZE * (INODES_PER_GRP * INODE_SIZE / BLK_SIZE);
  fill_sb_jnl(sb, grp_flex_first(GROUPS - 1) * BLKS_PER_GRP + flex_meta);
  wr32(sb + 0x3FC, crc32c_e2fs(~0, sb, 0x3FC));
  return write_at(fd, sb, 1024, SB_OFF) ? -1 : 0;
}

static void gdt_set_addrs(unsigned char *d, uint32_t g)
{
  wr32(d + G_BB_LO, grp_bb_blk(g));
  wr32(d + G_IB_LO, grp_ib_blk(g));
  wr32(d + G_IT_LO, grp_it_blk(g));
}

static uint32_t gdt_used_flex(uint32_t g, uint32_t fg, uint32_t fb)
{
  uint32_t data_start = (fg == 0) ? (RESV_GDT_BLKS + FLEX_META) : (fb + FLEX_META);
  uint32_t used = data_start - g * BLKS_PER_GRP;
  if (g == 0)
    used += 2;
  return used;
}

static uint32_t gdt_count_used(uint32_t g, uint32_t jnl_fg, uint32_t journal_blks)
{
  uint32_t fg = grp_flex_first(g);
  uint32_t used = 0;
  if (g == fg)
    used = gdt_used_flex(g, fg, grp_flex_base(g));
  if (has_super(g) && g != fg)
    used += 3 + RESERVED_GDT;
  if (g == jnl_fg)
    used += journal_blks;
  return used;
}

static void gdt_set_free_counts(unsigned char *d, uint32_t g, uint32_t used)
{
  uint32_t grp_blocks = (g < GROUPS - 1) ? BLKS_PER_GRP : (BLOCKS - g * BLKS_PER_GRP);
  wr16(d + G_FREE_B, (used < grp_blocks) ? (uint16_t) (grp_blocks - used) : 0);
  wr16(d + G_FREE_I, (g == 0) ? (uint16_t) (INODES_PER_GRP - 11)
       : (uint16_t) INODES_PER_GRP);
  wr16(d + G_USED_D, (g == 0) ? 1 : 0);
}

static void gdt_set_flags(unsigned char *d, uint32_t g, uint32_t used)
{
  uint16_t flags = 0;
  if (used == 0 && g != GROUPS - 1)
    flags |= EXT4_BLOCK_UNINIT;
  if (g > 0)
    flags |= EXT4_INODE_UNINIT;
  wr16(d + G_FLAGS, flags);
}

static void gdt_of_group(unsigned char *d, uint32_t g, uint32_t jnl_fg, uint32_t journal_blks)
{
  gdt_set_addrs(d, g);
  uint32_t used = gdt_count_used(g, jnl_fg, journal_blks);
  gdt_set_free_counts(d, g, used);
  gdt_set_flags(d, g, used);
}

int write_gdt(int fd, unsigned char *gdt, uint32_t jnl_fg, uint32_t journal_blks)
{
  memset(gdt, 0, (size_t)2 * BLK_SIZE);
  for (uint32_t g = 0; g < GROUPS; g++)
    gdt_of_group(gdt + g * DESC_SIZE, g, jnl_fg, journal_blks);
  return write_at(fd, gdt, (size_t)2 * BLK_SIZE, (off_t) 1 * BLK_SIZE);
}

static int csum_bb_one(unsigned char *d, int fd, uint32_t bb_blk, uint32_t seed)
{
  unsigned char bm[BLK_SIZE];
  if (pread(fd, bm, BLK_SIZE, (off_t) bb_blk * BLK_SIZE) != BLK_SIZE)
    return -1;
  uint32_t c = crc32c_e2fs(seed, bm, BLK_SIZE);
  wr16(d + G_BB_C_L, (uint16_t) (c & 0xFFFF));
  wr16(d + G_BB_C_H, (uint16_t) (c >> 16));
  return 0;
}

static int csum_ib_one(unsigned char *d, int fd, uint32_t ib_blk, uint32_t seed)
{
  unsigned char bm[BLK_SIZE];
  if (pread(fd, bm, BLK_SIZE, (off_t) ib_blk * BLK_SIZE) != BLK_SIZE)
    return -1;
  uint32_t c = crc32c_e2fs(seed, bm, INODES_PER_GRP / 8);
  wr16(d + G_IB_C_L, (uint16_t) (c & 0xFFFF));
  wr16(d + G_IB_C_H, (uint16_t) (c >> 16));
  return 0;
}

static int csum_one_group(unsigned char *d, int fd, uint32_t g, uint32_t seed)
{
  if (csum_bb_one(d, fd, grp_bb_blk(g), seed))
    return -1;
  if (csum_ib_one(d, fd, grp_ib_blk(g), seed))
    return -1;
  gdt_set_csum(d, g, seed);
  return 0;
}

static int write_one_backup(int fd, const unsigned char *sb, const unsigned char *gdt, uint32_t g)
{
  off_t base = (off_t) g * BLKS_PER_GRP * BLK_SIZE;
  unsigned char bs[1024];
  memcpy(bs, sb, 1024);
  wr16(bs + 0x05A, (uint16_t) g);
  wr32(bs + 0x3FC, crc32c_e2fs(~0, bs, 0x3FC));
  if (write_at(fd, bs, 1024, base))
    return -1;
  return write_at(fd, gdt, (size_t)2 * BLK_SIZE, base + BLK_SIZE);
}

static int write_backups(int fd, const unsigned char *gdt)
{
  unsigned char sb[1024];
  if (pread(fd, sb, 1024, SB_OFF) != 1024)
    return -1;
  for (uint32_t g = 1; g < GROUPS; g++) {
    if (!has_super(g))
      continue;
    if (write_one_backup(fd, sb, gdt, g))
      return -1;
  }
  return 0;
}

int write_csum_backups(int fd, unsigned char *gdt, uint32_t seed)
{
  for (uint32_t g = 0; g < GROUPS; g++) {
    if (csum_one_group(gdt + g * DESC_SIZE, fd, g, seed))
      return -1;
  }
  if (write_at(fd, gdt, (size_t)2 * BLK_SIZE, (off_t) 1 * BLK_SIZE))
    return -1;
  return write_backups(fd, gdt);
}

uint32_t count_backup_blks(void)
{
  uint32_t n = 0;
  for (uint32_t g = 1; g < GROUPS; g++)
    if (has_super(g))
      n += 3;
  return n;
}

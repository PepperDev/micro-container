#ifndef CAGE_EXT4_UTIL_H
#define CAGE_EXT4_UTIL_H

// cppcheck-suppress missingIncludeSystem
#include <stdint.h>
// cppcheck-suppress missingIncludeSystem
#include <stddef.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

#define BLK_SIZE 4096
#define INODES_PER_GRP 2048
#define DESC_SIZE 64
#define SB_OFF 1024
#define BLOCKS 2621440
#define GROUPS 80
#define BLKS_PER_GRP 32768
#define INODE_SIZE 256
#define FLEX_GRP_SIZE 16
#define IT_BLKS_PER_GRP (INODES_PER_GRP * INODE_SIZE / BLK_SIZE)
#define FLEX_META (2 * FLEX_GRP_SIZE + FLEX_GRP_SIZE * IT_BLKS_PER_GRP)
#define RESERVED_GDT 1024
#define RESV_GDT_BLKS (3 + RESERVED_GDT)

struct mkfs {
  int fd;
  uint32_t now;
  uint32_t seed;
  const unsigned char *uuid;
  const unsigned char *hash_seed;
};

uint32_t grp_flex_first(uint32_t g);
uint32_t grp_flex_base(uint32_t g);
static inline uint32_t grp_bb_blk(uint32_t g)
{
  uint32_t f = grp_flex_first(g);
  return (f == 0) ? (RESV_GDT_BLKS + g - f) : (grp_flex_base(g) + g - f);
}

static inline uint32_t grp_ib_blk(uint32_t g)
{
  uint32_t f = grp_flex_first(g);
  return (f == 0) ? (RESV_GDT_BLKS + FLEX_GRP_SIZE + g - f) : (grp_flex_base(g) + FLEX_GRP_SIZE + g - f);
}

static inline uint32_t grp_it_blk(uint32_t g)
{
  uint32_t f = grp_flex_first(g);
  return (f == 0) ? (RESV_GDT_BLKS + 2 * FLEX_GRP_SIZE + (g - f) * IT_BLKS_PER_GRP)
      : (grp_flex_base(g) + 2 * FLEX_GRP_SIZE + (g - f) * IT_BLKS_PER_GRP);
}

#define EXT4_EXTENT_MAGIC 0xF30A
#define EXT4_EXTENTS_FL 0x00080000u
#define JBD2_MAGIC 0xC03B3998u

#define G_BB_LO    0x00
#define G_IB_LO    0x04
#define G_IT_LO    0x08
#define G_FREE_B   0x0C
#define G_FREE_I   0x0E
#define G_USED_D   0x10
#define G_FLAGS    0x12
#define G_CHECK    0x1E
#define G_BB_C_L   0x18
#define G_IB_C_L   0x1A
#define G_BB_C_H   0x38
#define G_IB_C_H   0x3A

uint32_t rd32(const unsigned char *p);
uint16_t rd16(const unsigned char *p);
void wr32(unsigned char *p, uint32_t v);
void wr16(unsigned char *p, uint16_t v);
void wr32be(unsigned char *p, uint32_t v);
int gen_uuid(unsigned char *uuid);
int is_power(uint32_t n, uint32_t b);
int has_super(uint32_t g);
int write_at(int fd, const void *buf, size_t len, off_t off);
int write_zeroes(int fd, off_t off, size_t len);
void inode_set_csum(unsigned char *in, uint32_t ino, uint32_t seed);
void gdt_set_csum(unsigned char *desc, uint32_t g, uint32_t seed);
void extent_set(unsigned char *p, uint32_t ee_block, uint16_t ee_len, uint64_t ee_start);
void mark_bit(unsigned char *bm, uint32_t blk);
uint32_t dir_csum(uint32_t seed, const unsigned char *blk, uint32_t ino, uint32_t gen, uint32_t blk_size);

int write_superblock(int fd, const struct mkfs *m, uint32_t used_blks);
int write_gdt(int fd, unsigned char *gdt, uint32_t jnl_fg, uint32_t journal_blks);
int write_csum_backups(int fd, unsigned char *gdt, uint32_t seed);
uint32_t count_backup_blks(void);

#endif

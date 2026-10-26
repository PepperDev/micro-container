#include "ext4.h"
#include "csum.h"
#include "util.h"
#include "util/io.h"
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <time.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>

struct wi {
  uint32_t root_blk, dindir_blk, jnl_fg, jnl_start;
};

static void bb_mark_group0(unsigned char *bm, uint32_t root_blk, uint32_t dindir_blk)
{
  for (uint32_t b = 0; b < RESV_GDT_BLKS + FLEX_META; b++)
    mark_bit(bm, b);
  mark_bit(bm, root_blk);
  if (dindir_blk)
    mark_bit(bm, dindir_blk);
}

static void bb_mark_flex(unsigned char *bm, uint32_t g)
{
  uint32_t fb = grp_flex_base(g);
  for (uint32_t b = fb; b < fb + FLEX_META; b++)
    mark_bit(bm, b - fb);
}

static void bb_mark_meta(unsigned char *bm, uint32_t g, uint32_t root_blk, uint32_t dindir_blk)
{
  uint32_t fg = grp_flex_first(g);
  if (g == 0)
    bb_mark_group0(bm, root_blk, dindir_blk);
  else if (g == fg)
    bb_mark_flex(bm, g);
  if (has_super(g) && g != fg) {
    mark_bit(bm, 0), mark_bit(bm, 1), mark_bit(bm, 2);
    for (uint32_t b = 3; b < 3 + RESERVED_GDT; b++)
      mark_bit(bm, b);
  }
}

static void bb_mark_journal(unsigned char *bm, uint32_t g, uint32_t jnl_start, uint32_t journal_blks)
{
  for (uint32_t b = jnl_start; b < jnl_start + journal_blks; b++)
    mark_bit(bm, b - g * BLKS_PER_GRP);
}

static int write_block_bitmaps(const struct mkfs *m, const struct wi *w)
{
  for (uint32_t g = 0; g < GROUPS; g++) {
    unsigned char bm[BLK_SIZE];
    memset(bm, 0, sizeof(bm));
    bb_mark_meta(bm, g, w->root_blk, w->dindir_blk);
    if (g == w->jnl_fg)
      bb_mark_journal(bm, g, w->jnl_start, 16384);
    if (write_at(m->fd, bm, BLK_SIZE, (off_t) grp_bb_blk(g) * BLK_SIZE))
      return -1;
  }
  return 0;
}

static int write_inode_bitmaps(int fd)
{
  for (uint32_t g = 0; g < GROUPS; g++) {
    unsigned char bm[BLK_SIZE];
    memset(bm, 0, sizeof(bm));
    if (g == 0) {
      bm[0] = 0xFF;
      bm[1] = 0x03;
      mark_bit(bm, 11);
    }
    memset(bm + INODES_PER_GRP / 8, 0xFF, BLK_SIZE - INODES_PER_GRP / 8);
    if (write_at(fd, bm, BLK_SIZE, (off_t) grp_ib_blk(g) * BLK_SIZE))
      return -1;
  }
  return 0;
}

static void write_dot_entry(unsigned char *blk)
{
  wr32(blk + 0, 2);
  wr16(blk + 4, 12);
  blk[6] = 1;
  blk[7] = 2;
  blk[8] = '.';
}

static void write_dotdot_entry(unsigned char *blk)
{
  wr32(blk + 12, 2);
  wr16(blk + 16, (uint16_t) (BLK_SIZE - 24));
  blk[18] = 2;
  blk[19] = 2;
  blk[20] = '.';
  blk[21] = '.';
}

static int write_root_dir(int fd, uint32_t root_blk, uint32_t seed)
{
  unsigned char blk[BLK_SIZE];
  memset(blk, 0, sizeof(blk));
  write_dot_entry(blk);
  write_dotdot_entry(blk);
  uint32_t t = BLK_SIZE - 12;
  wr32(blk + t, 0);
  wr16(blk + t + 4, 12);
  wr16(blk + t + 6, 0xDE00);
  wr32(blk + t + 8, dir_csum(seed, blk, 2, 0, BLK_SIZE));
  return write_at(fd, blk, BLK_SIZE, (off_t) root_blk * BLK_SIZE);
}

static void fill_jbd2_sb(unsigned char *jsb, const unsigned char *uuid)
{
  wr32be(jsb + 0, JBD2_MAGIC);
  wr32be(jsb + 4, 4);
  wr32be(jsb + 8, 0);
  wr32be(jsb + 0x0C, BLK_SIZE);
  wr32be(jsb + 0x10, 16384);
  wr32be(jsb + 0x14, 1);
  wr32be(jsb + 0x18, 0);
  wr32be(jsb + 0x1C, 0);
  wr32be(jsb + 0x40, 1);
  memcpy(jsb + 0x30, uuid, 16);
}

static int write_journal(int fd, uint32_t jnl_start, const unsigned char *uuid)
{
  unsigned char jsb[BLK_SIZE];
  memset(jsb, 0, sizeof(jsb));
  fill_jbd2_sb(jsb, uuid);
  if (write_at(fd, jsb, BLK_SIZE, (off_t) jnl_start * BLK_SIZE))
    return -1;
  return write_zeroes(fd, (off_t) (jnl_start + 1) * BLK_SIZE, (size_t)16383 * BLK_SIZE);
}

static void fill_root_inode(unsigned char *in, uint32_t now, uint32_t root_blk)
{
  wr16(in + 0x00, 040755);
  wr32(in + 0x04, BLK_SIZE);
  wr32(in + 0x08, now);
  wr32(in + 0x0C, now);
  wr32(in + 0x10, now);
  wr16(in + 0x18, 0);
  wr16(in + 0x1A, 2);
  wr32(in + 0x1C, (BLK_SIZE / 512));
  wr32(in + 0x20, EXT4_EXTENTS_FL);
  extent_set(in + 0x28, 0, 1, root_blk);
  wr32(in + 0x64, 0);
  wr16(in + 0x80, 32);
}

static void fill_journal_inode(unsigned char *in, uint32_t now, uint32_t jnl_start)
{
  wr16(in + 0x00, 0100644);
  wr32(in + 0x04, 16384u * BLK_SIZE);
  wr32(in + 0x08, now);
  wr32(in + 0x0C, now);
  wr32(in + 0x10, now);
  wr16(in + 0x1A, 1);
  wr32(in + 0x1C, (16384u * BLK_SIZE / 512));
  wr32(in + 0x20, EXT4_EXTENTS_FL);
  extent_set(in + 0x28, 0, 16384, jnl_start);
  wr32(in + 0x64, 0);
  wr16(in + 0x80, 32);
}

static uint32_t resize_nblks(void)
{
  uint32_t n = 1 + RESERVED_GDT;
  for (uint32_t g = 1; g < GROUPS; g++)
    if (has_super(g))
      n += RESERVED_GDT;
  return n;
}

static void set_resize_isize(unsigned char *in)
{
  uint32_t apb = BLK_SIZE / 4;
  uint64_t isize = ((uint64_t) apb * apb + apb + 12) * BLK_SIZE;
  wr32(in + 0x04, (uint32_t) isize);
  wr32(in + 0x6C, (uint32_t) (isize >> 32));
}

static void fill_resize_inode(unsigned char *in, uint32_t now, uint32_t dindir_blk)
{
  wr16(in + 0x00, 0x8180);
  wr16(in + 0x1A, 1);
  wr32(in + 0x08, now);
  wr32(in + 0x0C, now);
  wr32(in + 0x10, now);
  wr32(in + 0x20, 0);
  set_resize_isize(in);
  wr32(in + 0x5C, dindir_blk);
  wr32(in + 0x1C, resize_nblks() * (BLK_SIZE / 512));
  wr16(in + 0x80, 32);
}

static int write_one_resize_gdt(int fd, uint32_t gdt_blk)
{
  unsigned char gdt_buf[BLK_SIZE];
  memset(gdt_buf, 0, sizeof(gdt_buf));
  uint32_t last = 0;
  for (uint32_t grp = 1; grp < GROUPS; grp++) {
    if (!has_super(grp))
      continue;
    wr32(gdt_buf + last * 4, gdt_blk + grp * BLKS_PER_GRP);
    last++;
  }
  return write_at(fd, gdt_buf, BLK_SIZE, (off_t) gdt_blk * BLK_SIZE);
}

static int write_resize_data(int fd, uint32_t dindir_blk)
{
  unsigned char dindir_buf[BLK_SIZE];
  uint32_t apb = BLK_SIZE / 4;

  memset(dindir_buf, 0, sizeof(dindir_buf));
  for (uint32_t rsv_off = 0, gdt_off = 2, gdt_blk = 3; rsv_off < RESERVED_GDT; rsv_off++, gdt_off++, gdt_blk++) {
    gdt_off %= apb;
    wr32(dindir_buf + gdt_off * 4, gdt_blk);
    if (write_one_resize_gdt(fd, gdt_blk))
      return -1;
  }
  return write_at(fd, dindir_buf, BLK_SIZE, (off_t) dindir_blk * BLK_SIZE);
}

static void fill_orphan_inode(unsigned char *in, uint32_t now)
{
  wr16(in + 0x00, 0x8180);
  wr16(in + 0x1A, 1);
  wr32(in + 0x08, now);
  wr32(in + 0x0C, now);
  wr32(in + 0x10, now);
  wr32(in + 0x20, 0);
  wr16(in + 0x80, 32);
}

static void set_inode_csums(unsigned char *it, uint32_t seed)
{
  for (uint32_t i = 0; i < INODES_PER_GRP; i++) {
    unsigned char *p = it + i * INODE_SIZE;
    if (rd16(p + 0x1A))
      inode_set_csum(p, i + 1, seed);
  }
}

static int write_inode_table(const struct mkfs *m, const struct wi *w)
{
  uint32_t now = m->now;
  size_t sz = (size_t)INODES_PER_GRP * INODE_SIZE;
  unsigned char *it = calloc(1, sz);
  if (!it)
    return -1;

  fill_root_inode(it + INODE_SIZE, now, w->root_blk);
  fill_resize_inode(it + 6 * INODE_SIZE, now, w->dindir_blk);
  fill_journal_inode(it + 7 * INODE_SIZE, now, w->jnl_start);
  fill_orphan_inode(it + 11 * INODE_SIZE, now);
  set_inode_csums(it, m->seed);

  int rc = write_at(m->fd, it, sz, (off_t) grp_it_blk(0) * BLK_SIZE);
  free(it);
  return rc;
}

static uint32_t compute_used_blks(void)
{
  uint32_t n = 5 * FLEX_META + 5 + RESERVED_GDT;
  for (uint32_t g = 1; g < GROUPS; g++)
    if (has_super(g))
      n += RESERVED_GDT;
  return n + count_backup_blks() + 16384;
}

static int do_writes(const struct mkfs *m, const struct wi *w)
{
  unsigned char gdt[2 * BLK_SIZE];
  return write_superblock(m->fd, m, compute_used_blks())
      | write_gdt(m->fd, gdt, w->jnl_fg, 16384)
      | write_block_bitmaps(m, w)
      | write_inode_bitmaps(m->fd)
      | write_root_dir(m->fd, w->root_blk, m->seed)
      | write_journal(m->fd, w->jnl_start, m->uuid)
      | write_inode_table(m, w)
      | write_resize_data(m->fd, w->dindir_blk)
      | write_csum_backups(m->fd, gdt, m->seed);
}

static int do_create_writes(const struct mkfs *m)
{
  uint32_t root_blk = RESV_GDT_BLKS + FLEX_META;
  struct wi wi = { root_blk, root_blk + 1, grp_flex_first(GROUPS - 1),
    grp_flex_first(GROUPS - 1) * BLKS_PER_GRP + FLEX_META
  };
  return do_writes(m, &wi);
}

int ext4_image_valid(const char *path)
{
  int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return 0;
  struct stat st;
  unsigned char m[2];
  int ok = fstat(fd, &st) == 0 && st.st_size == (off_t) BLOCKS * BLK_SIZE
      && pread(fd, m, sizeof(m), SB_OFF + 0x38) == (ssize_t) sizeof(m) && rd16(m) == 0xEF53;
  close(fd);
  return ok;
}

int ext4_create_img(const char *path)
{
  int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return -1;
  if (ftruncate(fd, (off_t) BLOCKS * BLK_SIZE))
    goto fail;

  unsigned char uuid[16], hash_seed[16];
  if (gen_uuid(uuid) || gen_uuid(hash_seed))
    goto fail;

  struct mkfs m = { fd, (uint32_t) time(NULL), crc32c_e2fs(~0, uuid, 16), uuid, hash_seed };

  if (do_create_writes(&m))
    goto fail;

  close(fd);
  return 0;
 fail:
  close(fd);
  io_unlink(path);
  return -1;
}

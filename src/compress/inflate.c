#include "inflate.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>

static const int LEN_BASE[] = {
  3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
  67, 83, 99, 115, 131, 163, 195, 227, 258
};

static const int LEN_EXTRA[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

static const int DIST_BASE[] = {
  1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

static const int DIST_EXTRA[] = {
  0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static const int CL_ORD[] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

static int decode_stored(struct br *br, buf_t *out)
{
  br_align(br);
  int len = br_bits(br, 16);
  (void)br_bits(br, 16);
  if (len < 0)
    return -1;
  for (int i = 0; i < len; i++) {
    int b = br_bits(br, 8);
    if (b < 0)
      return -1;
    if (buf_append_c(out, (char)b) != 0)
      return -1;
  }
  return 0;
}

static void fixed_huff(int *lit, int *dst)
{
  for (int i = 0; i < 144; i++)
    lit[i] = 8;
  for (int i = 144; i < 256; i++)
    lit[i] = 9;
  for (int i = 256; i < 280; i++)
    lit[i] = 7;
  for (int i = 280; i < 288; i++)
    lit[i] = 8;
  for (int i = 0; i < 32; i++)
    dst[i] = 5;
}

static int fill_rep(int *lit, int idx, int total, int rep, int val)
{
  for (int j = 0; j < rep; j++) {
    if (idx >= total)
      break;
    lit[idx++] = val;
  }
  return idx;
}

static int rep_len(struct br *br, int *lit, int idx, int total, int sym)
{
  if (sym == 16) {
    int raw = br_bits(br, 2);
    if (raw < 0 || idx < 1)
      return -1;
    return fill_rep(lit, idx, total, raw + 3, lit[idx - 1]);
  }
  int nbits = 7, base = 11;
  if (sym == 17) {
    nbits = 3;
    base = 3;
  }
  int raw = br_bits(br, nbits);
  if (raw < 0)
    return -1;
  return fill_rep(lit, idx, total, raw + base, 0);
}

static int decode_len(struct br *br, const struct huff *hc, int *lit, int total)
{
  int idx = 0;
  while (idx < total) {
    int sym = huff_decode(br, hc);
    if (sym < 0)
      return -1;
    if (sym < 16) {
      lit[idx++] = sym;
    } else {
      idx = rep_len(br, lit, idx, total, sym);
      if (idx < 0)
        return -1;
    }
  }
  return 0;
}

static int read_code_lengths(struct br *br, int *lit, int nl, int nd)
{
  int nc = br_bits(br, 4) + 4;
  int cl[19];
  memset(cl, 0, sizeof(cl));
  for (int i = 0; i < nc; i++) {
    int v = br_bits(br, 3);
    if (v < 0)
      return -1;
    cl[CL_ORD[i]] = v;
  }
  struct huff ct;
  huff_build(&ct, cl, 19);
  memset(lit, 0, 320 * sizeof(int));
  return decode_len(br, &ct, lit, nl + nd);
}

static int read_dynamic(struct br *br, int *lit, int *dst, struct huff *hl, struct huff *hd)
{
  int nl = br_bits(br, 5) + 257;
  int nd = br_bits(br, 5) + 1;
  if (read_code_lengths(br, lit, nl, nd) != 0)
    return -1;
  memcpy(dst, lit + nl, (size_t)nd * sizeof(int));
  huff_build(hl, lit, nl);
  huff_build(hd, dst, nd);
  return 0;
}

static int get_len(struct br *br, int sym)
{
  int slot = sym - 257;
  // cppcheck-suppress knownConditionTrueFalse
  if (slot < 0 || slot >= 29)
    return -1;
  int len = LEN_BASE[slot];
  int extra = LEN_EXTRA[slot];
  if (extra <= 0)
    return len;
  int e = br_bits(br, extra);
  if (e < 0)
    return -1;
  return len + e;
}

static int get_dist(struct br *br, const struct huff *hd)
{
  int sym = huff_decode(br, hd);
  if (sym < 0)
    return -1;
  if (sym >= 30)
    return 0;
  int d = DIST_BASE[sym];
  int extra = DIST_EXTRA[sym];
  if (extra <= 0)
    return d;
  int e = br_bits(br, extra);
  if (e < 0)
    return -1;
  return d + e;
}

static int copy_dist(buf_t *out, int length, int distance)
{
  if ((unsigned)(length - 3) > 255U)
    return -1;
  if (distance < 1 || (size_t)distance > out->len)
    return -1;
  if (buf_grow(out, (size_t)length) != 0)
    return -1;
  for (int i = 0; i < length; i++) {
    out->data[out->len] = out->data[out->len - (size_t)distance];
    out->len++;
  }
  return 0;
}

static int do_sym(struct br *br, buf_t *out, const struct huff *hd, int sym)
{
  if (sym < 256) {
    if (buf_append_c(out, (char)sym) != 0)
      return -1;
    return 0;
  }
  if (sym > 256) {
    int length = get_len(br, sym);
    if (length < 0)
      return -1;
    int distance = get_dist(br, hd);
    if (distance < 0)
      return -1;
    return copy_dist(out, length, distance);
  }
  return 1;
}

static int run_loop(struct br *br, buf_t *out, const struct huff *hl, const struct huff *hd, int last)
{
  for (;;) {
    int sym = huff_decode(br, hl);
    if (sym < 0)
      return -1;
    int ret = do_sym(br, out, hd, sym);
    if (ret < 0)
      return -1;
    if (ret > 0)
      return last;
  }
}

static int build_fixed(int *lit, int *dst, struct huff *hl, struct huff *hd)
{
  fixed_huff(lit, dst);
  huff_build(hl, lit, 288);
  huff_build(hd, dst, 32);
  return 0;
}

int inflate_block(struct br *br, buf_t *out)
{
  int last = br_bits(br, 1);
  int type = br_bits(br, 2);
  if ((unsigned)type > 2)
    return -1;
  if (type == 0) {
    if (decode_stored(br, out) != 0)
      return -1;
    return last;
  }
  int lit[320], dst[32];
  struct huff hl, hd;
  if (type == 1)
    build_fixed(lit, dst, &hl, &hd);
  else if (read_dynamic(br, lit, dst, &hl, &hd) != 0)
    return -1;
  return run_loop(br, out, &hl, &hd, last);
}

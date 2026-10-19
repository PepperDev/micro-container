#include "bitread.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>

static int br_need(struct br *br, int n)
{
  while (br->bits < n) {
    if (br->pos >= br->len)
      return -1;
    br->buf |= (uint64_t) br->data[br->pos++] << br->bits;
    br->bits += 8;
  }
  return 0;
}

static int br_peek(struct br *br, int n)
{
  if (br_need(br, n) != 0)
    return -1;
  return (int)(br->buf & ((1ULL << n) - 1));
}

static void br_drop(struct br *br, int n)
{
  br->buf >>= n;
  br->bits -= n;
}

int br_bits(struct br *br, int n)
{
  int v = br_peek(br, n);
  if (v < 0)
    return -1;
  br_drop(br, n);
  return v;
}

void br_align(struct br *br)
{
  int skip = br->bits & 7;
  if (skip > 0)
    br_drop(br, skip);
}

void br_init(struct br *br, const void *data, size_t len)
{
  br->data = (const unsigned char *)data;
  br->len = len;
  br->pos = 0;
  br->buf = 0;
  br->bits = 0;
}

static void huff_count(struct huff *h, const int *lengths, int nsym)
{
  for (int i = 0; i < nsym; i++) {
    int l = lengths[i];
    if (l > 0 && l <= 15)
      h->count[l]++;
  }
}

static void huff_codes(struct huff *h)
{
  int code = 0;
  for (int b = 1; b <= 15; b++) {
    h->first_code[b] = code;
    code = (code + h->count[b]) << 1;
  }
}

static void huff_place(struct huff *h, const int *lengths, int nsym)
{
  int slot[16];
  slot[0] = 0;
  h->first_idx[0] = 0;
  for (int b = 1; b <= 15; b++) {
    h->first_idx[b] = h->first_idx[b - 1] + h->count[b - 1];
    slot[b] = h->first_idx[b];
  }
  for (int i = 0; i < nsym; i++) {
    int l = lengths[i];
    if (l > 0 && l <= 15)
      h->symbols[slot[l]++] = i;
  }
}

void huff_build(struct huff *h, const int *lengths, int nsym)
{
  memset(h, 0, sizeof(*h));
  h->nsym = nsym;
  huff_count(h, lengths, nsym);
  huff_codes(h);
  huff_place(h, lengths, nsym);
}

int huff_decode(struct br *br, const struct huff *h)
{
  int code = 0;
  for (int b = 1; b <= 15; b++) {
    int bit = br_bits(br, 1);
    if (bit < 0)
      return -1;
    code = (code << 1) | bit;
    if (code < h->first_code[b])
      continue;
    if (code < h->first_code[b] + h->count[b])
      return h->symbols[h->first_idx[b] + (code - h->first_code[b])];
  }
  return -1;
}

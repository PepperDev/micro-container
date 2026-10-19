#ifndef CAGE_BITREAD_H
#define CAGE_BITREAD_H

#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <stddef.h>
// cppcheck-suppress missingIncludeSystem
#include <stdint.h>

struct br {
  const unsigned char *data;
  size_t len;
  size_t pos;
  uint64_t buf;
  int bits;
};

struct huff {
  int first_code[16];
  int first_idx[16];
  int count[16];
  int nsym;
  int symbols[320];
};

void br_init(struct br *br, const void *data, size_t len);
int br_bits(struct br *br, int n);
void br_align(struct br *br);
void huff_build(struct huff *h, const int *lengths, int nsym);
int huff_decode(struct br *br, const struct huff *h);

#endif

#ifndef CAGE_INFLATE_H
#define CAGE_INFLATE_H

#include "bitread.h"
// cppcheck-suppress missingIncludeSystem
#include <stdint.h>

int inflate_block(struct br *br, buf_t * out);
void crc32_init(void);
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

#endif

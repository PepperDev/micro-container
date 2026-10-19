#ifndef CAGE_EXT4_CSUM_H
#define CAGE_EXT4_CSUM_H

// cppcheck-suppress missingIncludeSystem
#include <stdint.h>
// cppcheck-suppress missingIncludeSystem
#include <stddef.h>

uint32_t ext4_crc32c(uint32_t crc, const void *data, size_t len);

/* Wrapper: adapt ext4_crc32c to match e2fsprogs' ext2fs_crc32c_le semantics */
static inline uint32_t crc32c_e2fs(uint32_t crc, const void *data, size_t len)
{
  return ~ext4_crc32c(~crc, data, len);
}

#endif

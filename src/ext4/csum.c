#include "csum.h"

#define CRC32C_POLY 0x82F63B78

static void crc32c_build(uint32_t *tab)
{
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int j = 0; j < 8; j++)
      c = (c & 1) ? (CRC32C_POLY ^ (c >> 1)) : (c >> 1);
    tab[i] = c;
  }
}

uint32_t ext4_crc32c(uint32_t crc, const void *data, size_t len)
{
  static uint32_t tab[256];
  static int init;
  if (!init) {
    crc32c_build(tab);
    init = 1;
  }
  const unsigned char *p = (const unsigned char *)data;
  crc = ~crc;
  for (size_t i = 0; i < len; i++)
    crc = tab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

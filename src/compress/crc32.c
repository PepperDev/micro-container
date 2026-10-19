#include "inflate.h"

static uint32_t crc32_tab[256];
static int crc32_ready;

void crc32_init(void)
{
  if (crc32_ready)
    return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int j = 0; j < 8; j++) {
      if (c & 1)
        c = 0xEDB88320 ^ (c >> 1);
      else
        c >>= 1;
    }
    crc32_tab[i] = c;
  }
  crc32_ready = 1;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
  const unsigned char *p = (const unsigned char *)data;
  crc = ~crc;
  for (size_t i = 0; i < len; i++)
    crc = crc32_tab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

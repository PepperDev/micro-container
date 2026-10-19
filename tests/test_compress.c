#include "framework.h"
#include "test_util.h"
#include "compress/inflate.h"
#include "compress/bitread.h"
#include "compress/decompress.h"
#include "util/mem.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* ----- CRC32 tests ----- */

static void test_crc32_init_idempotent(void)
{
  crc32_init();
  crc32_init();
}

static void test_crc32_empty(void)
{
  crc32_init();
  ASSERT_EQ(crc32_update(0, "", 0), 0x00000000U);
}

static void test_crc32_abc(void)
{
  crc32_init();
  ASSERT_EQ(crc32_update(0, "abc", 3), 0x352441C2U);
}

static void test_crc32_numbers(void)
{
  crc32_init();
  ASSERT_EQ(crc32_update(0, "123456789", 9), 0xCBF43926U);
}

/* ----- Bit reader tests ----- */

static void test_br_bits_single_byte(void)
{
  unsigned char data[] = { 0xAA };
  struct br br;
  br_init(&br, data, sizeof(data));
  ASSERT_EQ(br_bits(&br, 1), 0);
  ASSERT_EQ(br_bits(&br, 1), 1);
  ASSERT_EQ(br_bits(&br, 1), 0);
  ASSERT_EQ(br_bits(&br, 1), 1);
  ASSERT_EQ(br_bits(&br, 1), 0);
  ASSERT_EQ(br_bits(&br, 1), 1);
  ASSERT_EQ(br_bits(&br, 1), 0);
  ASSERT_EQ(br_bits(&br, 1), 1);
}

static void test_br_bits_multi_byte(void)
{
  unsigned char data[] = { 0x01, 0x02 };
  struct br br;
  br_init(&br, data, sizeof(data));
  ASSERT_EQ(br_bits(&br, 16), 0x0201);
}

static void test_br_align(void)
{
  unsigned char data[] = { 0xFF, 0xAA };
  struct br br;
  br_init(&br, data, sizeof(data));
  ASSERT_EQ(br_bits(&br, 3), 7);
  br_align(&br);
  ASSERT_EQ(br_bits(&br, 8), 0xAA);
}

static void test_br_align_already_aligned(void)
{
  unsigned char data[] = { 0xAB };
  struct br br;
  br_init(&br, data, sizeof(data));
  br_align(&br);
  ASSERT_EQ(br_bits(&br, 8), 0xAB);
}

static void test_br_bits_exhausted(void)
{
  unsigned char data[] = { 0xFF };
  struct br br;
  br_init(&br, data, sizeof(data));
  ASSERT_EQ(br_bits(&br, 9), -1);
}

/* ----- Huffman build/decode tests ----- */

static void test_huff_build_uniform(void)
{
  int lengths[] = { 2, 2, 2, 2 };
  struct huff h;
  huff_build(&h, lengths, 4);
  ASSERT_EQ(h.count[2], 4);
}

static void test_huff_build_zero_length(void)
{
  int lengths[] = { 0, 0, 0 };
  struct huff h;
  huff_build(&h, lengths, 3);
  ASSERT_EQ(h.nsym, 3);
}

static void test_huff_decode_two_1bit(void)
{
  int lengths[] = { 1, 1 };
  struct huff h;
  huff_build(&h, lengths, 2);
  unsigned char data[] = { 0x0C };
  struct br br;
  br_init(&br, data, sizeof(data));
  ASSERT_EQ(huff_decode(&br, &h), 0);
  ASSERT_EQ(huff_decode(&br, &h), 0);
  ASSERT_EQ(huff_decode(&br, &h), 1);
  ASSERT_EQ(huff_decode(&br, &h), 1);
  ASSERT_EQ(huff_decode(&br, &h), 0);
}

static void test_huff_decode_mixed_lengths(void)
{
  int lengths[] = { 1, 2, 2 };
  struct huff h;
  huff_build(&h, lengths, 3);
  ASSERT_EQ(h.count[1], 1);
  ASSERT_EQ(h.count[2], 2);
  ASSERT_EQ(h.symbols[h.first_idx[1]], 0);
  unsigned char data[] = { 0x1A };
  struct br br;
  br_init(&br, data, sizeof(data));
  ASSERT_EQ(huff_decode(&br, &h), 0);
  ASSERT_EQ(huff_decode(&br, &h), 1);
  ASSERT_EQ(huff_decode(&br, &h), 2);
  ASSERT_EQ(huff_decode(&br, &h), 0);
}

static void test_huff_decode_non_consecutive(void)
{
  int lengths[] = { 1, 2, 3, 3, 0, 2 };
  struct huff h;
  huff_build(&h, lengths, 6);
  ASSERT_EQ(h.count[2], 2);
  unsigned char data[] = { 0x06 };
  struct br br;
  br_init(&br, data, sizeof(data));
  ASSERT_EQ(huff_decode(&br, &h), 0);
  ASSERT_EQ(huff_decode(&br, &h), 5);
}

static void test_huff_decode_no_match(void)
{
  int lengths[] = { 1, 2 };
  struct huff h;
  huff_build(&h, lengths, 2);
  unsigned char data[] = { 0x06 };
  struct br br;
  br_init(&br, data, sizeof(data));
  ASSERT_EQ(huff_decode(&br, &h), 0);
  ASSERT_EQ(huff_decode(&br, &h), -1);
}

/* ----- Stored block inflate tests ----- */

static void test_inflate_stored_block(void)
{
  unsigned char deflate_data[] = {
    0x01,
    0x05, 0x00,
    0xFA, 0xFF,
    'H', 'e', 'l', 'l', 'o'
  };
  struct br br;
  br_init(&br, deflate_data, sizeof(deflate_data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 5);
  ASSERT_EQ(memcmp(out.data, "Hello", 5), 0);
  buf_free(&out);
}

static void test_inflate_multiple_stored(void)
{
  unsigned char deflate_data[] = {
    0x00,
    0x03, 0x00,
    0xFC, 0xFF,
    'a', 'b', 'c',
    0x01,
    0x02, 0x00,
    0xFD, 0xFF,
    'd', 'e'
  };
  struct br br;
  br_init(&br, deflate_data, sizeof(deflate_data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 3);
  r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 5);
  ASSERT_EQ(memcmp(out.data, "abcde", 5), 0);
  buf_free(&out);
}

static void test_inflate_fixed_block(void)
{
  unsigned char deflate_data[] = {
    0x03, 0x00
  };
  struct br br;
  br_init(&br, deflate_data, sizeof(deflate_data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 0);
  buf_free(&out);
}

static void test_inflate_fixed_hello(void)
{
  unsigned char data[] = { 0xF3, 0x48, 0xCD, 0xC9, 0xC9, 0x07, 0x00 };
  unsigned char exp[] = { 'H', 'e', 'l', 'l', 'o' };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 5);
  ASSERT_EQ(memcmp(out.data, exp, 5), 0);
  buf_free(&out);
}

static void test_inflate_fixed_a(void)
{
  unsigned char data[] = { 0x73, 0x04, 0x00 };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 1);
  ASSERT_EQ(out.data[0], 'A');
  buf_free(&out);
}

static void test_inflate_fixed_abc(void)
{
  unsigned char data[] = { 0x4B, 0x4C, 0x4A, 0x06, 0x00 };
  unsigned char exp[] = { 'a', 'b', 'c' };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 3);
  ASSERT_EQ(memcmp(out.data, exp, 3), 0);
  buf_free(&out);
}

static void test_inflate_fixed_backref_abc(void)
{
  unsigned char data[] = { 0x4B, 0x4C, 0x4A, 0x4E, 0x04, 0x23, 0x00 };
  unsigned char exp[] = { 'a', 'b', 'c', 'a', 'b', 'c', 'a', 'b', 'c' };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 9);
  ASSERT_EQ(memcmp(out.data, exp, 9), 0);
  buf_free(&out);
}

static void test_inflate_fixed_repeat_runs(void)
{
  unsigned char data[] = { 0x73, 0x74, 0x04, 0x02, 0x27, 0x10, 0x70, 0x06, 0x01, 0x00 };
  unsigned char exp[] = { 'A', 'A', 'A', 'A', 'A', 'B', 'B', 'B', 'B', 'B', 'C', 'C', 'C', 'C', 'C' };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 15);
  ASSERT_EQ(memcmp(out.data, exp, 15), 0);
  buf_free(&out);
}

static void test_inflate_invalid_type(void)
{
  unsigned char deflate_data[] = { 0x06 };
  struct br br;
  br_init(&br, deflate_data, sizeof(deflate_data));
  buf_t out = BUF_INIT;
  ASSERT_EQ(inflate_block(&br, &out), -1);
  buf_free(&out);
}

static void test_inflate_stored_truncated_len(void)
{
  unsigned char data[] = { 0x01, 0x64, 0x00, 0x9B, 0xFF };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  ASSERT_EQ(inflate_block(&br, &out), -1);
  buf_free(&out);
}

static void test_inflate_stored_truncated_data(void)
{
  unsigned char data[] = { 0x01, 0x0A, 0x00, 0xF5, 0xFF, 'H', 'i' };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  ASSERT_EQ(inflate_block(&br, &out), -1);
  buf_free(&out);
}

static void test_inflate_stored_not_final(void)
{
  unsigned char data[] = {
    0x00, 0x06, 0x00, 0xF9, 0xFF,
    'H', 'e', 'l', 'l', 'o', ' ',
    0x01, 0x06, 0x00, 0xF9, 0xFF,
    'W', 'o', 'r', 'l', 'd', '!'
  };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 6);
  r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 12);
  ASSERT_EQ(memcmp(out.data, "Hello World!", 12), 0);
  buf_free(&out);
}

static void test_inflate_stored_large(void)
{
  unsigned char data[] = {
    0x01, 0x64, 0x00, 0x9B, 0xFF,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41
  };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 100);
  for (size_t i = 0; i < out.len; i++)
    ASSERT_EQ(out.data[i], 'A');
  buf_free(&out);
}

static void test_inflate_stored_reuse(void)
{
  unsigned char data[] = {
    0x00, 0x05, 0x00, 0xFA, 0xFF, 'H', 'e', 'l', 'l', 'o',
    0x01, 0x07, 0x00, 0xF8, 0xFF, ' ', 'W', 'o', 'r', 'l', 'd', '!'
  };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 5);
  r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 12);
  ASSERT_EQ(memcmp(out.data, "Hello World!", 12), 0);
  buf_free(&out);
}

static void test_inflate_multi_stored_hello_world(void)
{
  unsigned char data[] = {
    0x00, 0x06, 0x00, 0xF9, 0xFF, 'H', 'e', 'l', 'l', 'o', ' ',
    0x01, 0x06, 0x00, 0xF9, 0xFF, 'W', 'o', 'r', 'l', 'd', '!'
  };
  unsigned char exp[] = "Hello World!";
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 6);
  r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 12);
  ASSERT_EQ(memcmp(out.data, exp, 12), 0);
  buf_free(&out);
}

static void test_inflate_br_exhausted_in_huff(void)
{
  unsigned char data[] = { 0x77, 0xFF };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  ASSERT_EQ(inflate_block(&br, &out), -1);
  buf_free(&out);
}

static void test_inflate_fixed_truncated_data(void)
{
  unsigned char data[] = { 0x03 };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  ASSERT_EQ(inflate_block(&br, &out), -1);
  buf_free(&out);
}

static void test_inflate_fixed_truncated_bit(void)
{
  unsigned char data[] = { 0x03, 0x00, 0x00 };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  int r = inflate_block(&br, &out);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(out.len, 0);
  buf_free(&out);
}

static void test_inflate_dynamic_truncated_hclen(void)
{
  unsigned char data[] = { 0x05, 0xC1 };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  ASSERT_EQ(inflate_block(&br, &out), -1);
  buf_free(&out);
}

static void test_inflate_dynamic_truncated_rep_len(void)
{
  unsigned char data[] = { 0x05, 0x00, 0x10, 0x00 };
  struct br br;
  br_init(&br, data, sizeof(data));
  buf_t out = BUF_INIT;
  ASSERT_EQ(inflate_block(&br, &out), -1);
  buf_free(&out);
}

/* ----- decompress_gzip tests ----- */

static void test_decompress_gzip_empty(void)
{
  unsigned char gzip_data[] = {
    0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x01, 0x00, 0x00, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  int r = decompress_gzip(gzip_data, sizeof(gzip_data), &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 0);
  buf_free(&out);
}

static void test_decompress_gzip_stored_a(void)
{
  unsigned char gzip_data[] = {
    0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x01, 0x01, 0x00, 0xFE, 0xFF, 'a',
    0x43, 0xBE, 0xB7, 0xE8, 0x01, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  int r = decompress_gzip(gzip_data, sizeof(gzip_data), &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 1);
  ASSERT_EQ(out.data[0], 'a');
  buf_free(&out);
}

static void test_decompress_gzip_invalid_magic(void)
{
  unsigned char bad[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  buf_t out = BUF_INIT;
  ASSERT_EQ(decompress_gzip(bad, sizeof(bad), &out), -1);
  buf_free(&out);
}

static void test_decompress_gzip_too_short(void)
{
  unsigned char short_data[] = { 0x1F, 0x8B };
  buf_t out = BUF_INIT;
  ASSERT_EQ(decompress_gzip(short_data, sizeof(short_data), &out), -1);
  buf_free(&out);
}

static void test_decompress_gzip_truncated(void)
{
  unsigned char gzip_data[] = {
    0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x01, 0x01, 0x00, 0xFE, 0xFF, 'a'
  };
  buf_t out = BUF_INIT;
  ASSERT_EQ(decompress_gzip(gzip_data, sizeof(gzip_data), &out), -1);
  buf_free(&out);
}

static void test_decompress_gzip_invalid_method(void)
{
  unsigned char data[] = {
    0x1F, 0x8B, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  ASSERT_EQ(decompress_gzip(data, sizeof(data), &out), -1);
  buf_free(&out);
}

static void test_decompress_gzip_extra(void)
{
  unsigned char data[] = {
    0x1F, 0x8B, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x01, 0x00, 0x58,
    0x01, 0x04, 0x00, 0xFB, 0xFF, 't', 'e', 's', 't',
    0x0C, 0x7E, 0x7F, 0xD8, 0x04, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  int r = decompress_gzip(data, sizeof(data), &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 4);
  ASSERT_EQ(memcmp(out.data, "test", 4), 0);
  buf_free(&out);
}

static void test_decompress_gzip_name(void)
{
  unsigned char data[] = {
    0x1F, 0x8B, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x66, 0x2E, 0x74, 0x78, 0x74, 0x00,
    0x01, 0x04, 0x00, 0xFB, 0xFF, 't', 'e', 's', 't',
    0x0C, 0x7E, 0x7F, 0xD8, 0x04, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  int r = decompress_gzip(data, sizeof(data), &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 4);
  ASSERT_EQ(memcmp(out.data, "test", 4), 0);
  buf_free(&out);
}

static void test_decompress_gzip_comment(void)
{
  unsigned char data[] = {
    0x1F, 0x8B, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x63, 0x6D, 0x74, 0x00,
    0x01, 0x04, 0x00, 0xFB, 0xFF, 't', 'e', 's', 't',
    0x0C, 0x7E, 0x7F, 0xD8, 0x04, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  int r = decompress_gzip(data, sizeof(data), &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 4);
  ASSERT_EQ(memcmp(out.data, "test", 4), 0);
  buf_free(&out);
}

static void test_decompress_gzip_hcrc(void)
{
  unsigned char data[] = {
    0x1F, 0x8B, 0x08, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x90, 0xC9,
    0x01, 0x04, 0x00, 0xFB, 0xFF, 't', 'e', 's', 't',
    0x0C, 0x7E, 0x7F, 0xD8, 0x04, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  int r = decompress_gzip(data, sizeof(data), &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 4);
  ASSERT_EQ(memcmp(out.data, "test", 4), 0);
  buf_free(&out);
}

static void test_decompress_gzip_hw(void)
{
  unsigned char data[] = {
    0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x01, 0x0C, 0x00, 0xF3, 0xFF,
    'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!',
    0xA3, 0x1C, 0x29, 0x1C, 0x0C, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  int r = decompress_gzip(data, sizeof(data), &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 12);
  ASSERT_EQ(memcmp(out.data, "Hello World!", 12), 0);
  buf_free(&out);
}

static void test_decompress_gzip_os3(void)
{
  unsigned char data[] = {
    0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x01, 0x04, 0x00, 0xFB, 0xFF, 't', 'e', 's', 't',
    0x0C, 0x7E, 0x7F, 0xD8, 0x04, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  int r = decompress_gzip(data, sizeof(data), &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, 4);
  ASSERT_EQ(memcmp(out.data, "test", 4), 0);
  buf_free(&out);
}

static void test_decompress_gzip_bad_crc(void)
{
  unsigned char data[] = {
    0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c',
    0xEF, 0xBE, 0xAD, 0xDE, 0x03, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  ASSERT_EQ(decompress_gzip(data, sizeof(data), &out), -1);
  buf_free(&out);
}

static void test_decompress_gzip_multi_member(void)
{
  unsigned char data[] = {
    0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x01, 0x05, 0x00, 0xFA, 0xFF, 'f', 'i', 'r', 's', 't',
    0x57, 0xEE, 0x71, 0x92, 0x05, 0x00, 0x00, 0x00,
    0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x01, 0x06, 0x00, 0xF9, 0xFF, 's', 'e', 'c', 'o', 'n', 'd',
    0x69, 0x11, 0x1F, 0xB6, 0x06, 0x00, 0x00, 0x00
  };
  buf_t out = BUF_INIT;
  ASSERT_EQ(decompress_gzip(data, sizeof(data), &out), -1);
  buf_free(&out);
}

static void test_decompress_xz_header(void)
{
  unsigned char xz_data[] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };
  buf_t out = BUF_INIT;
  ASSERT_EQ(decompress_gzip(xz_data, sizeof(xz_data), &out), -1);
  buf_free(&out);
}

static void test_decompress_zstd_header(void)
{
  unsigned char zstd_data[] = { 0x28, 0xB5, 0x2F, 0xFD, 0x00 };
  buf_t out = BUF_INIT;
  ASSERT_EQ(decompress_gzip(zstd_data, sizeof(zstd_data), &out), -1);
  buf_free(&out);
}

/* ----- open_and_read tests ----- */

static void test_open_and_read_nonexistent(void)
{
  buf_t out = BUF_INIT;
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent");
  ASSERT_EQ(open_and_read(path, &out), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
  buf_free(&out);
}

static void test_open_and_read_ok(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "compress-file");
  const char *content = "Hello compress test!";
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  ssize_t n = write(fd, content, strlen(content));
  ASSERT_EQ(n, (ssize_t) strlen(content));
  close(fd);

  buf_t out = BUF_INIT;
  int r = open_and_read(path, &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, strlen(content));
  ASSERT_EQ(memcmp(out.data, content, out.len), 0);
  buf_free(&out);

  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_open_and_read_large(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "compress-large");
  char buf_data[8192];
  memset(buf_data, 'X', sizeof(buf_data));
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  ssize_t n = write(fd, buf_data, sizeof(buf_data));
  ASSERT_EQ(n, (ssize_t) sizeof(buf_data));
  close(fd);

  buf_t out = BUF_INIT;
  int r = open_and_read(path, &out);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(out.len, sizeof(buf_data));
  for (size_t i = 0; i < out.len; i++)
    ASSERT_EQ(out.data[i], 'X');
  buf_free(&out);

  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

SUITE(compress)
{
  RUN_TEST(crc32_init_idempotent);
  RUN_TEST(crc32_empty);
  RUN_TEST(crc32_abc);
  RUN_TEST(crc32_numbers);
  RUN_TEST(br_bits_single_byte);
  RUN_TEST(br_bits_multi_byte);
  RUN_TEST(br_align);
  RUN_TEST(br_align_already_aligned);
  RUN_TEST(br_bits_exhausted);
  RUN_TEST(huff_build_uniform);
  RUN_TEST(huff_build_zero_length);
  RUN_TEST(huff_decode_two_1bit);
  RUN_TEST(huff_decode_mixed_lengths);
  RUN_TEST(huff_decode_non_consecutive);
  RUN_TEST(huff_decode_no_match);

  RUN_TEST(inflate_stored_block);
  RUN_TEST(inflate_multiple_stored);
  RUN_TEST(inflate_fixed_block);
  RUN_TEST(inflate_fixed_hello);
  RUN_TEST(inflate_fixed_a);
  RUN_TEST(inflate_fixed_abc);
  RUN_TEST(inflate_fixed_backref_abc);
  RUN_TEST(inflate_fixed_repeat_runs);
  RUN_TEST(inflate_invalid_type);
  RUN_TEST(inflate_stored_truncated_len);
  RUN_TEST(inflate_stored_truncated_data);
  RUN_TEST(inflate_stored_not_final);
  RUN_TEST(inflate_stored_large);
  RUN_TEST(inflate_stored_reuse);
  RUN_TEST(inflate_multi_stored_hello_world);
  RUN_TEST(inflate_br_exhausted_in_huff);
  RUN_TEST(inflate_fixed_truncated_data);
  RUN_TEST(inflate_fixed_truncated_bit);
  RUN_TEST(inflate_dynamic_truncated_hclen);
  RUN_TEST(inflate_dynamic_truncated_rep_len);

  RUN_TEST(decompress_gzip_empty);
  RUN_TEST(decompress_gzip_stored_a);
  RUN_TEST(decompress_gzip_invalid_magic);
  RUN_TEST(decompress_gzip_too_short);
  RUN_TEST(decompress_gzip_truncated);
  RUN_TEST(decompress_gzip_invalid_method);
  RUN_TEST(decompress_gzip_extra);
  RUN_TEST(decompress_gzip_name);
  RUN_TEST(decompress_gzip_comment);
  RUN_TEST(decompress_gzip_hcrc);
  RUN_TEST(decompress_gzip_hw);
  RUN_TEST(decompress_gzip_os3);
  RUN_TEST(decompress_gzip_bad_crc);
  RUN_TEST(decompress_gzip_multi_member);
  RUN_TEST(decompress_xz_header);
  RUN_TEST(decompress_zstd_header);

  RUN_TEST(open_and_read_nonexistent);
  RUN_TEST(open_and_read_ok);
  RUN_TEST(open_and_read_large);
}

#include "framework.h"
#include "util/mem.h"
#include <string.h>

static void test_mem_alloc_free(void)
{
  char *p = mem_alloc(64);
  ASSERT_NOT_NULL(p);
  mem_free(p);
}

static void test_mem_strdup(void)
{
  char *s = mem_strdup("hello");
  ASSERT_NOT_NULL(s);
  ASSERT_STR_EQ(s, "hello");
  mem_free(s);
}

static void test_mem_strdup_empty(void)
{
  char *s = mem_strdup("");
  ASSERT_NOT_NULL(s);
  ASSERT_STR_EQ(s, "");
  mem_free(s);
}

static void test_mem_cat_single(void)
{
  char *s = mem_cat("abc", 3, MEM_CAT_END);
  ASSERT_NOT_NULL(s);
  ASSERT_STR_EQ(s, "abc");
  mem_free(s);
}

static void test_mem_cat_multi(void)
{
  char *s = mem_cat("a", 1, "bc", 2, "def", 3, MEM_CAT_END);
  ASSERT_NOT_NULL(s);
  ASSERT_EQ(strlen(s), 6);
  ASSERT_STR_EQ(s, "abcdef");
  mem_free(s);
}

static void test_mem_path_simple(void)
{
  char *p = mem_path("/base", "sub");
  ASSERT_NOT_NULL(p);
  ASSERT_STR_EQ(p, "/base/sub");
  mem_free(p);
}

static void test_mem_path_trailing_slash(void)
{
  char *p = mem_path("/base/", "sub");
  ASSERT_NOT_NULL(p);
  ASSERT_STR_EQ(p, "/base/sub");
  mem_free(p);
}

static void test_mem_path_leading_slash_append(void)
{
  char *p = mem_path("/base", "/sub");
  ASSERT_NOT_NULL(p);
  ASSERT_STR_EQ(p, "/base/sub");
  mem_free(p);
}

static void test_buf_grow(void)
{
  buf_t b = BUF_INIT;
  ASSERT_EQ(buf_grow(&b, 100), 0);
  ASSERT(b.cap >= 100);
  buf_free(&b);
}

static void test_buf_append(void)
{
  buf_t b = BUF_INIT;
  ASSERT_EQ(buf_append(&b, "hello", 5), 0);
  ASSERT_EQ(b.len, 5);
  ASSERT_EQ(memcmp(b.data, "hello", 5), 0);
  buf_free(&b);
}

static void test_buf_append_c(void)
{
  buf_t b = BUF_INIT;
  ASSERT_EQ(buf_append_c(&b, 'x'), 0);
  ASSERT_EQ(b.len, 1);
  ASSERT_EQ(b.data[0], 'x');
  buf_free(&b);
}

static void test_buf_done(void)
{
  buf_t b = BUF_INIT;
  ASSERT_EQ(buf_append(&b, "test", 4), 0);
  char *d = buf_done(&b);
  ASSERT_NOT_NULL(d);
  ASSERT_EQ(memcmp(d, "test", 4), 0);
  ASSERT_EQ(b.data, NULL);
  mem_free(d);
}

SUITE(mem)
{
  RUN_TEST(mem_alloc_free);
  RUN_TEST(mem_strdup);
  RUN_TEST(mem_strdup_empty);
  RUN_TEST(mem_cat_single);
  RUN_TEST(mem_cat_multi);
  RUN_TEST(mem_path_simple);
  RUN_TEST(mem_path_trailing_slash);
  RUN_TEST(mem_path_leading_slash_append);
  RUN_TEST(buf_grow);
  RUN_TEST(buf_append);
  RUN_TEST(buf_append_c);
  RUN_TEST(buf_done);
}

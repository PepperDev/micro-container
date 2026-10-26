#include "framework.h"
#include "test_util.h"
#include "pkg/resolve.h"
#include "util/io.h"
#include "util/mem.h"
#include "mock.h"
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <fcntl.h>

static char *test_moddir_root;

static const char *get_moddir(void)
{
  if (!test_moddir_root)
    test_moddir_root = test_temp_root();
  return test_moddir_root;
}

/* Helper: write content into test_moddir_root/$(uname -r)/modules.dep */
static int dep_setup(const char *content)
{
  if (!test_moddir_root) {
    test_moddir_root = test_temp_root();
    if (!test_moddir_root)
      return -1;
  }
  struct utsname uts;
  if (uname(&uts) != 0)
    return -1;
  char dir[4096];
  snprintf(dir, sizeof(dir), "%s/%s", test_moddir_root, uts.release);
  if (io_mkdir(dir) != 0)
    return -1;
  char path[4160];
  snprintf(path, sizeof(path), "%s/modules.dep", dir);
  FILE *f = fopen(path, "w");
  if (!f)
    return -1;
  int r = fprintf(f, "%s", content);
  fclose(f);
  return r < 0 ? -1 : 0;
}

/* Helper: remove the temp modules directory tree */
static void dep_teardown(void)
{
  if (!test_moddir_root)
    return;
  struct utsname uts;
  if (uname(&uts) != 0)
    return;
  char path[4160];
  snprintf(path, sizeof(path), "%s/%s/modules.dep", test_moddir_root, uts.release);
  io_unlink(path);
  snprintf(path, sizeof(path), "%s/%s", test_moddir_root, uts.release);
  io_rmtree(path);
  io_rmtree(test_moddir_root);
  test_moddir_root = NULL;
  _mock_mod_dir = NULL;
}

static void test_resolve_nonexistent(void)
{
  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("nonexistent-module-xyz", &path, &deps, &nd);
  ASSERT_EQ(r, -1);
  ASSERT_NULL(path);
}

static void test_resolve_empty_name(void)
{
  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("", &path, &deps, &nd);
  ASSERT_EQ(r, -1);
}

static void test_resolve_with_deps(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko: kernel/fs/mbcache.ko kernel/fs/jbd2.ko\n"), 0);

  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("ext4", &path, &deps, &nd);
  ASSERT_EQ(r, 0);
  ASSERT_NOT_NULL(path);

  struct utsname uts;
  uname(&uts);
  char expected[4096];
  snprintf(expected, sizeof(expected), "/lib/modules/%s/kernel/fs/ext4.ko", uts.release);
  ASSERT_STR_EQ(path, expected);

  ASSERT_EQ(nd, 2);
  ASSERT_NOT_NULL(deps);
  ASSERT_STR_EQ(deps[0], "mbcache");
  ASSERT_STR_EQ(deps[1], "jbd2");

  mem_free(path);
  for (int i = 0; i < nd; i++)
    mem_free(deps[i]);
  mem_free(deps);
  dep_teardown();
}

static void test_resolve_no_deps(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/overlay.ko:\n"), 0);

  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("overlay", &path, &deps, &nd);
  ASSERT_EQ(r, 0);
  ASSERT_NOT_NULL(path);

  struct utsname uts;
  uname(&uts);
  char expected[4096];
  snprintf(expected, sizeof(expected), "/lib/modules/%s/kernel/fs/overlay.ko", uts.release);
  ASSERT_STR_EQ(path, expected);

  ASSERT_EQ(nd, 0);
  ASSERT_NULL(deps);

  mem_free(path);
  dep_teardown();
}

static void test_resolve_not_found_with_dep(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko: kernel/fs/mbcache.ko\n"), 0);

  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("overlay", &path, &deps, &nd);
  ASSERT_EQ(r, -1);
  ASSERT_NULL(path);

  dep_teardown();
}

static void test_resolve_multiple_lines(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko: kernel/fs/mbcache.ko kernel/fs/jbd2.ko\n"
                      "kernel/fs/overlay.ko:\n" "kernel/fs/vfscow.ko: kernel/fs/mbcache.ko\n"), 0);

  /* first entry, has deps */
  {
    char *path = NULL;
    char **deps = NULL;
    int nd = 0;
    int r = resolve_module("ext4", &path, &deps, &nd);
    ASSERT_EQ(r, 0);
    ASSERT_NOT_NULL(path);
    ASSERT_EQ(nd, 2);
    ASSERT_STR_EQ(deps[0], "mbcache");
    ASSERT_STR_EQ(deps[1], "jbd2");
    mem_free(path);
    for (int i = 0; i < nd; i++)
      mem_free(deps[i]);
    mem_free(deps);
  }

  /* middle entry, no deps */
  {
    char *path = NULL;
    char **deps = NULL;
    int nd = 0;
    int r = resolve_module("overlay", &path, &deps, &nd);
    ASSERT_EQ(r, 0);
    ASSERT_NOT_NULL(path);
    ASSERT_EQ(nd, 0);
    ASSERT_NULL(deps);
    mem_free(path);
  }

  /* last entry, one dep */
  {
    char *path = NULL;
    char **deps = NULL;
    int nd = 0;
    int r = resolve_module("vfscow", &path, &deps, &nd);
    ASSERT_EQ(r, 0);
    ASSERT_NOT_NULL(path);
    ASSERT_EQ(nd, 1);
    ASSERT_STR_EQ(deps[0], "mbcache");
    mem_free(path);
    for (int i = 0; i < nd; i++)
      mem_free(deps[i]);
    mem_free(deps);
  }

  dep_teardown();
}

static void test_resolve_no_colon_last_line(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko: kernel/fs/jbd2.ko\nno_colon"), 0);

  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("overlay", &path, &deps, &nd);
  ASSERT_EQ(r, -1);
  ASSERT_NULL(path);

  dep_teardown();
}

static void test_resolve_empty_mod_path(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup(": extra\n"), 0);

  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("extra", &path, &deps, &nd);
  ASSERT_EQ(r, -1);

  dep_teardown();
}

static void test_resolve_dep_not_ko(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko: kernel/fs/mbcache\n"), 0);

  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("ext4", &path, &deps, &nd);
  ASSERT_EQ(r, 0);
  ASSERT_NOT_NULL(path);
  ASSERT_EQ(nd, 0);

  mem_free(path);
  dep_teardown();
}

static void test_resolve_trailing_whitespace(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko: kernel/fs/jbd2.ko   "), 0);

  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("ext4", &path, &deps, &nd);
  ASSERT_EQ(r, 0);
  ASSERT_NOT_NULL(path);
  ASSERT_EQ(nd, 1);

  mem_free(path);
  mem_free(deps[0]);
  mem_free(deps);
  dep_teardown();
}

static void test_resolve_gz_suffix(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko.gz:\n"), 0);
  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("ext4", &path, &deps, &nd);
  ASSERT_EQ(r, 0);
  ASSERT_NOT_NULL(path);
  mem_free(path);
  dep_teardown();
}

static void test_resolve_zst_suffix(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko.zst:\n"), 0);
  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("ext4", &path, &deps, &nd);
  ASSERT_EQ(r, 0);
  ASSERT_NOT_NULL(path);
  mem_free(path);
  dep_teardown();
}

static void test_resolve_name_no_match(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko:\n"), 0);
  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("ext5", &path, &deps, &nd);
  ASSERT_EQ(r, -1);
  dep_teardown();
}

static void test_resolve_short_dep(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko: a\n"), 0);
  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("ext4", &path, &deps, &nd);
  ASSERT_EQ(r, 0);
  ASSERT_NOT_NULL(path);
  ASSERT_EQ(nd, 0);
  mem_free(path);
  dep_teardown();
}

static void test_resolve_invalid_suffix(void)
{
  dep_teardown();
  _mock_mod_dir = get_moddir();
  ASSERT_EQ(dep_setup("kernel/fs/ext4.ko: kernel/fs/jbd2.ko.xyy kernel/fs/jbd2.ko.wxyz kernel/fs/jbd2.ko.a\n"), 0);
  char *path = NULL;
  char **deps = NULL;
  int nd = 0;
  int r = resolve_module("ext4", &path, &deps, &nd);
  ASSERT_EQ(r, 0);
  ASSERT_NOT_NULL(path);
  ASSERT_EQ(nd, 0);
  mem_free(path);
  dep_teardown();
}

SUITE(resolve)
{
  RUN_TEST(resolve_nonexistent);
  RUN_TEST(resolve_empty_name);
  RUN_TEST(resolve_with_deps);
  RUN_TEST(resolve_no_deps);
  RUN_TEST(resolve_not_found_with_dep);
  RUN_TEST(resolve_multiple_lines);
  RUN_TEST(resolve_no_colon_last_line);
  RUN_TEST(resolve_empty_mod_path);
  RUN_TEST(resolve_dep_not_ko);
  RUN_TEST(resolve_trailing_whitespace);
  RUN_TEST(resolve_gz_suffix);
  RUN_TEST(resolve_zst_suffix);
  RUN_TEST(resolve_name_no_match);
  RUN_TEST(resolve_short_dep);
  RUN_TEST(resolve_invalid_suffix);
}

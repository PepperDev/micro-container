#include "framework.h"
#include "test_util.h"
#include "sandbox/vol.h"
#include "util/io.h"
#include "mock.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>

static void test_mount_user_volume_dir(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *host = mem_path(root, "host-dir");
  io_mkdir(host);

  vol_t v;
  memset(&v, 0, sizeof(v));
  strcpy(v.host, host);
  strcpy(v.target, "mnt");

  ASSERT_EQ(mount_user_volume(&v, root), 0);
  ASSERT_EQ(_mock_mount_count, 2);

  mem_free(host);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_user_volume_empty_root(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *host = mem_path(root, "host-empty");
  io_mkdir(host);

  vol_t v;
  memset(&v, 0, sizeof(v));
  strcpy(v.host, host);
  strcpy(v.target, "mnt/sub");

  ASSERT_EQ(mount_user_volume(&v, root), 0);

  mem_free(host);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_user_volume_host_created(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *host = mem_path(root, "host-new");

  vol_t v;
  memset(&v, 0, sizeof(v));
  strcpy(v.host, host);
  strcpy(v.target, "mnt");

  ASSERT_EQ(mount_user_volume(&v, root), 0);
  ASSERT_EQ(io_isdir(host), 0);

  mem_free(host);
  test_rmtree(root);
  mem_free(root);
}

static void test_parse_vol_basic(void)
{
  mock_sys_reset();

  vol_t v;
  ASSERT_EQ(parse_vol(&v, "/tmp/host-path"), 0);
  ASSERT_STR_EQ(v.host, "/tmp/host-path");
  ASSERT_STR_EQ(v.target, "/tmp/host-path");
}

static void test_parse_vol_with_target(void)
{
  mock_sys_reset();

  vol_t v;
  ASSERT_EQ(parse_vol(&v, "/tmp/host:mnt"), 0);
  ASSERT_STR_EQ(v.host, "/tmp/host");
  ASSERT_STR_EQ(v.target, "mnt");
}

static void test_parse_vol_empty(void)
{
  mock_sys_reset();

  vol_t v;
  ASSERT_EQ(parse_vol(&v, ""), -1);
}

static void test_parse_vol_empty_host(void)
{
  mock_sys_reset();

  vol_t v;
  ASSERT_EQ(parse_vol(&v, ":target"), -1);
}

static void test_parse_vol_empty_target(void)
{
  mock_sys_reset();

  vol_t v;
  ASSERT_EQ(parse_vol(&v, "host:"), -1);
}

static void test_mount_user_volume_file(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *host = mem_path(root, "host-file");
  io_touch(host);

  vol_t v;
  memset(&v, 0, sizeof(v));
  strcpy(v.host, host);
  strcpy(v.target, "mnt/myfile");

  ASSERT_EQ(mount_user_volume(&v, root), 0);
  ASSERT_EQ(_mock_mount_count, 2);

  mem_free(host);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_user_volume_mount_bind_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *host = mem_path(root, "host-bf");
  io_mkdir(host);

  vol_t v;
  memset(&v, 0, sizeof(v));
  strcpy(v.host, host);
  strcpy(v.target, "mnt");

  ASSERT_EQ(mount_user_volume(&v, root), -1);
  ASSERT_EQ(_mock_mount_count, 1);

  mem_free(host);
  test_rmtree(root);
  mem_free(root);
}

static void test_parse_and_mount_volume(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *host = mem_path(root, "parse-host");
  io_mkdir(host);
  char *spec = mem_cat(host, strlen(host), ":mnt", 4, MEM_CAT_END);

  vol_t v;
  ASSERT_EQ(parse_vol(&v, spec), 0);
  ASSERT_EQ(mount_user_volume(&v, root), 0);
  ASSERT_EQ(_mock_mount_count, 2);

  char *guest = mem_path(root, "mnt");
  ASSERT_EQ(io_isdir(guest), 0);
  mem_free(guest);

  mem_free(spec);
  mem_free(host);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_user_volume_create_guest_dir_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *host = mem_path(root, "host-cd");
  io_mkdir(host);

  char *block = mem_path(root, "mnt");
  io_touch(block);
  mem_free(block);

  vol_t v;
  memset(&v, 0, sizeof(v));
  strcpy(v.host, host);
  strcpy(v.target, "mnt");

  ASSERT_EQ(mount_user_volume(&v, root), -1);

  mem_free(host);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_user_volume_create_guest_file_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *host = mem_path(root, "host-cf");
  io_touch(host);

  char *block = mem_path(root, "mnt");
  io_mkdir(block);
  mem_free(block);

  vol_t v;
  memset(&v, 0, sizeof(v));
  strcpy(v.host, host);
  strcpy(v.target, "mnt");

  ASSERT_EQ(mount_user_volume(&v, root), -1);

  mem_free(host);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_user_volume_private_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_mount_fail_after = 2;
  _mock_mount_errno = EPERM;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *host = mem_path(root, "host-pf");
  io_mkdir(host);

  vol_t v;
  memset(&v, 0, sizeof(v));
  strcpy(v.host, host);
  strcpy(v.target, "mnt");

  ASSERT_EQ(mount_user_volume(&v, root), -1);
  ASSERT_EQ(_mock_mount_count, 2);

  mem_free(host);
  test_rmtree(root);
  mem_free(root);
}

SUITE(vol)
{
  RUN_TEST(mount_user_volume_dir);
  RUN_TEST(mount_user_volume_empty_root);
  RUN_TEST(mount_user_volume_host_created);
  RUN_TEST(parse_vol_basic);
  RUN_TEST(parse_vol_with_target);
  RUN_TEST(parse_vol_empty);
  RUN_TEST(parse_vol_empty_host);
  RUN_TEST(parse_vol_empty_target);
  RUN_TEST(mount_user_volume_file);
  RUN_TEST(mount_user_volume_mount_bind_fails);
  RUN_TEST(parse_and_mount_volume);
  RUN_TEST(mount_user_volume_create_guest_dir_fails);
  RUN_TEST(mount_user_volume_create_guest_file_fails);
  RUN_TEST(mount_user_volume_private_fails);
}

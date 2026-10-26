#include "framework.h"
#include "test_util.h"
#include "util/mem.h"
#include "sandbox/loop.h"
#include "mock.h"
#include "util/io.h"
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

static void test_find_free_loop_fails(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = -1;
  _mock_loop_get_stat_ret = -1;

  ASSERT_EQ(find_free_loop(), -1);
}

static void test_find_free_loop_ctrl_fd_ok(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = 3;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "loop-ctl");
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  _mock_loop_ctl_fd = fd;

  int id = find_free_loop();
  ASSERT_EQ(id, 3);
  ASSERT_EQ(_mock_loop_get_free_count, 1);

  _mock_loop_ctl_fd = -1;
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

static void test_find_free_loop_control_ok(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = 3;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "loop-ctl");
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  _mock_open_loop_control_fd = fd;

  int id = find_free_loop();
  ASSERT_EQ(id, 3);
  ASSERT_EQ(_mock_loop_get_free_count, 1);
  ASSERT_EQ(_mock_open_loop_control_count, 1);

  _mock_open_loop_control_fd = -1;
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

static void test_find_free_loop_control_add(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = 7;
  errno = EINVAL;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "loop-ctl");
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  _mock_open_loop_control_fd = fd;

  int id = find_free_loop();
  ASSERT_EQ(id, 7);
  ASSERT_EQ(_mock_loop_get_free_count, 1);
  ASSERT_EQ(_mock_loop_add_count, 1);

  _mock_open_loop_control_fd = -1;
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

static void test_find_free_loop_scan(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = -1;
  _mock_loop_get_stat_ret = -1;
  _mock_open_loop_control_errno = ENXIO;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "loop0");
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  _mock_open_loop_dev_allow = 0;
  _mock_open_loop_dev_fd = fd;

  errno = ENXIO;
  int id = find_free_loop();
  ASSERT_EQ(id, 0);

  _mock_open_loop_dev_allow = -1;
  _mock_open_loop_dev_fd = -1;
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

/* ── Direct function tests ── */

static void test_loop_control_alloc_open_fails(void)
{
  mock_sys_reset();
  ASSERT_EQ(loop_control_alloc(), -1);
}

static void test_loop_control_alloc_get_free_ok(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = 3;
  char *root0 = test_temp_root();
  ASSERT_NOT_NULL(root0);
  char *path0 = mem_path(root0, "loop-ctl");
  int fd = open(path0, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  _mock_loop_ctl_fd = fd;
  ASSERT_EQ(loop_control_alloc(), 3);
  ASSERT_EQ(_mock_loop_get_free_count, 1);
  _mock_loop_ctl_fd = -1;
  mem_free(path0);
  io_rmtree(root0);
  mem_free(root0);
}

static void test_loop_control_alloc_add_ok(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = 7;
  char *root1 = test_temp_root();
  ASSERT_NOT_NULL(root1);
  char *path1 = mem_path(root1, "loop-ctl2");
  int fd = open(path1, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  errno = EINVAL;
  _mock_loop_ctl_fd = fd;
  ASSERT_EQ(loop_control_alloc(), 7);
  ASSERT_EQ(_mock_loop_get_free_count, 1);
  ASSERT_EQ(_mock_loop_add_count, 1);
  _mock_loop_ctl_fd = -1;
  mem_free(path1);
  io_rmtree(root1);
  mem_free(root1);
}

static void test_loop_control_alloc_add_fails(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = -1;
  char *root2 = test_temp_root();
  ASSERT_NOT_NULL(root2);
  char *path2 = mem_path(root2, "loop-ctl3");
  int fd = open(path2, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  errno = EINVAL;
  _mock_loop_ctl_fd = fd;
  ASSERT_EQ(loop_control_alloc(), -1);
  ASSERT_EQ(_mock_loop_get_free_count, 1);
  ASSERT_EQ(_mock_loop_add_count, 1);
  _mock_loop_ctl_fd = -1;
  mem_free(path2);
  io_rmtree(root2);
  mem_free(root2);
}

static void test_loop_control_alloc_enospc(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  char *root3 = test_temp_root();
  ASSERT_NOT_NULL(root3);
  char *path3 = mem_path(root3, "loop-ctl4");
  int fd = open(path3, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  errno = ENOSPC;
  _mock_loop_ctl_fd = fd;
  ASSERT_EQ(loop_control_alloc(), -1);
  ASSERT_EQ(_mock_loop_get_free_count, 1);
  ASSERT_EQ(_mock_loop_add_count, 0);
  _mock_loop_ctl_fd = -1;
  mem_free(path3);
  io_rmtree(root3);
  mem_free(root3);
}

static void test_try_open_loop_dev_nonexistent(void)
{
  mock_sys_reset();
  ASSERT_EQ(try_open_loop_dev(0), -1);
}

static void test_scan_loop_devices_empty(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = -1;
  ASSERT_EQ(scan_loop_devices(), -1);
}

static void test_scan_loop_devices_with_existing(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "loop0");
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  _mock_open_loop_dev_allow = 0;
  _mock_open_loop_dev_fd = fd;

  _mock_loop_get_stat_ret = -1;
  errno = ENXIO;
  ASSERT_EQ(scan_loop_devices(), 0);

  _mock_open_loop_dev_allow = -1;
  _mock_open_loop_dev_fd = -1;
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

static void test_scan_loop_devices_stale_errno(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "loop0");
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  _mock_open_loop_dev_allow = 0;
  _mock_open_loop_dev_fd = fd;

  _mock_loop_get_stat_ret = 0;
  errno = ENXIO;
  ASSERT_EQ(scan_loop_devices(), -1);

  _mock_open_loop_dev_allow = -1;
  _mock_open_loop_dev_fd = -1;
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

static void test_loop_nodisk(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = -1;
  char dev[64];
  char *root4 = test_temp_root();
  ASSERT_NOT_NULL(root4);
  char *file = mem_path(root4, "loop-img");
  io_unlink(file);
  io_truncate(file, 1048576);
  ASSERT_EQ(io_loop(dev, sizeof(dev), file), -1);
  mem_free(file);
  io_rmtree(root4);
  mem_free(root4);
}

static void test_loop_returns_open_fd(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = 0;
  _mock_loop_set_fd_ret = 0;
  _mock_loop_set_status64_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *ctl_path = mem_path(root, "loop-ctl");
  int ctl_fd = open(ctl_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(ctl_fd, -1);
  _mock_open_loop_control_fd = ctl_fd;

  char *dev_path = mem_path(root, "loop0");
  int dev_fd = open(dev_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(dev_fd, -1);
  _mock_open_loop_dev_allow = 0;
  _mock_open_loop_dev_fd = dev_fd;

  char *file = mem_path(root, "loop-img-ok");
  io_truncate(file, 1048576);

  char dev[64];
  int r = io_loop(dev, sizeof(dev), file);
  ASSERT_EQ(r, dev_fd);
  ASSERT_EQ(strcmp(dev, "/dev/loop0"), 0);
  ASSERT_NE(fcntl(r, F_GETFD), -1);
  ASSERT_EQ(_mock_loop_set_status64_count, 1);
  ASSERT_EQ(_mock_loop_clr_fd_count, 0);
  close(r);

  _mock_open_loop_control_fd = -1;
  _mock_open_loop_dev_allow = -1;
  _mock_open_loop_dev_fd = -1;
  mem_free(ctl_path);
  mem_free(dev_path);
  mem_free(file);
  io_rmtree(root);
  mem_free(root);
}

SUITE(loop)
{
  RUN_TEST(find_free_loop_fails);
  RUN_TEST(find_free_loop_ctrl_fd_ok);
  RUN_TEST(find_free_loop_control_ok);
  RUN_TEST(find_free_loop_control_add);
  RUN_TEST(find_free_loop_scan);
  RUN_TEST(loop_control_alloc_open_fails);
  RUN_TEST(loop_control_alloc_get_free_ok);
  RUN_TEST(loop_control_alloc_add_ok);
  RUN_TEST(loop_control_alloc_add_fails);
  RUN_TEST(loop_control_alloc_enospc);
  RUN_TEST(try_open_loop_dev_nonexistent);
  RUN_TEST(scan_loop_devices_empty);
  RUN_TEST(scan_loop_devices_with_existing);
  RUN_TEST(scan_loop_devices_stale_errno);
  RUN_TEST(loop_nodisk);
  RUN_TEST(loop_returns_open_fd);
}

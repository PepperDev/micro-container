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

static int try_create_dev(const char *path)
{
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0)
    return -1;
  close(fd);
  return 0;
}

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
  close(fd);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_find_free_loop_control_ok(void)
{
  if (try_create_dev("/dev/loop-control") != 0)
    return;

  mock_sys_reset();
  _mock_loop_get_free_ret = 3;

  int id = find_free_loop();
  ASSERT_EQ(id, 3);
  ASSERT_EQ(_mock_loop_get_free_count, 1);

  io_unlink("/dev/loop-control");
}

static void test_find_free_loop_control_add(void)
{
  if (try_create_dev("/dev/loop-control") != 0)
    return;

  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = 7;

  int id = find_free_loop();
  ASSERT_EQ(id, 7);
  ASSERT_EQ(_mock_loop_get_free_count, 1);
  ASSERT_EQ(_mock_loop_add_count, 1);

  io_unlink("/dev/loop-control");
}

static void test_find_free_loop_scan(void)
{
  if (try_create_dev("/dev/loop-control") != 0)
    return;
  if (try_create_dev("/dev/loop0") != 0) {
    io_unlink("/dev/loop-control");
    return;
  }

  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = -1;
  _mock_loop_get_stat_ret = -1;

  errno = ENXIO;
  int id = find_free_loop();
  ASSERT_EQ(id, 0);

  io_unlink("/dev/loop0");
  io_unlink("/dev/loop-control");
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
  close(fd);
  mem_free(path0);
  test_rmtree(root0);
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
  close(fd);
  mem_free(path1);
  test_rmtree(root1);
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
  close(fd);
  mem_free(path2);
  test_rmtree(root2);
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
  close(fd);
  mem_free(path3);
  test_rmtree(root3);
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
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = -1;

  /* Create a /dev/loop0 that scan will find */
  io_unlink("/dev/loop0");
  int fd = open("/dev/loop0", O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  close(fd);

  _mock_loop_get_stat_ret = -1;
  errno = ENXIO;
  ASSERT_EQ(scan_loop_devices(), 0);

  io_unlink("/dev/loop0");
}

static void test_scan_loop_devices_stale_errno(void)
{
  mock_sys_reset();
  _mock_loop_get_free_ret = -1;
  _mock_loop_add_ret = -1;

  io_unlink("/dev/loop0");
  int fd = open("/dev/loop0", O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  close(fd);

  /* A successful stat must win even when errno still holds a stale ENXIO */
  _mock_loop_get_stat_ret = 0;
  errno = ENXIO;
  ASSERT_EQ(scan_loop_devices(), -1);

  io_unlink("/dev/loop0");
}

static void test_loop_nodisk(void)
{
  char dev[64];
  char *root4 = test_temp_root();
  ASSERT_NOT_NULL(root4);
  char *file = mem_path(root4, "loop-img");
  io_unlink(file);
  io_truncate(file, 1048576);
  int r = io_loop(dev, sizeof(dev), file);
  ASSERT(r == -1 || r >= 0);
  if (r >= 0)
    close(r);
  mem_free(file);
  test_rmtree(root4);
  mem_free(root4);
}

static void test_loop_returns_open_fd(void)
{
  int had_ctl = io_exists("/dev/loop-control") == 0;
  int had_loop0 = io_exists("/dev/loop0") == 0;
  if (try_create_dev("/dev/loop-control") != 0)
    return;
  if (try_create_dev("/dev/loop0") != 0) {
    if (!had_ctl)
      io_unlink("/dev/loop-control");
    return;
  }

  mock_sys_reset();
  _mock_loop_get_free_ret = 0;
  _mock_loop_set_fd_ret = 0;
  _mock_loop_set_status64_ret = 0;

  char *root5 = test_temp_root();
  ASSERT_NOT_NULL(root5);
  char *file = mem_path(root5, "loop-img-ok");
  io_unlink(file);
  io_truncate(file, 1048576);

  char dev[64];
  int r = io_loop(dev, sizeof(dev), file);
  ASSERT(r >= 0);
  ASSERT_EQ(strcmp(dev, "/dev/loop0"), 0);
  ASSERT_NE(fcntl(r, F_GETFD), -1);
  ASSERT_EQ(_mock_loop_set_status64_count, 1);
  ASSERT_EQ(_mock_loop_clr_fd_count, 0);
  close(r);

  mem_free(file);
  test_rmtree(root5);
  mem_free(root5);
  if (!had_loop0)
    io_unlink("/dev/loop0");
  if (!had_ctl)
    io_unlink("/dev/loop-control");
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

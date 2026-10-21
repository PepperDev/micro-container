#include "framework.h"
#include "test_util.h"
#include "sandbox/acl.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
#include "mock.h"
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

static int create_test_sock(const char *path)
{
  unlink(path);
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
  int r = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
  if (r < 0) {
    close(fd);
    return -1;
  }
  r = listen(fd, 1);
  if (r < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void test_acl_set_root_uid(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "root");
  int fd = create_test_sock(path);
  ASSERT(fd >= 0);
  close(fd);
  ASSERT_EQ(acl_set(path, 0, 0, NULL, 0), 0);
  ASSERT_EQ(_mock_setxattr_count, 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_acl_set_same_uid(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "same");
  int fd = create_test_sock(path);
  ASSERT(fd >= 0);
  close(fd);
  struct stat st;
  ASSERT_EQ(stat(path, &st), 0);
  ASSERT_EQ(acl_set(path, st.st_uid, 0, NULL, 0), 0);
  ASSERT_EQ(_mock_setxattr_count, 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_acl_set_calls_setxattr(void)
{
  mock_sys_reset();
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "xattr");
  int fd = create_test_sock(path);
  ASSERT(fd >= 0);
  close(fd);
  uid_t test_uid = (getuid() == 1000) ? 1001 : 1000;
  ASSERT_EQ(acl_set(path, test_uid, 1000, NULL, 0), 0);
  ASSERT_EQ(_mock_setxattr_count, 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_acl_set_setxattr_fails(void)
{
  mock_sys_reset();
  _mock_setxattr_ret = -1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "xattr-fail");
  int fd = create_test_sock(path);
  ASSERT(fd >= 0);
  close(fd);
  uid_t test_uid = (getuid() == 1000) ? 1001 : 1000;
  ASSERT_EQ(acl_set(path, test_uid, 1000, NULL, 0), -1);
  ASSERT_EQ(_mock_setxattr_count, 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_acl_set_nonexistent_path(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent");
  ASSERT_EQ(acl_set(path, 1000, 1000, NULL, 0), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_acl_set_symlink_rejected(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *target = mem_path(root, "real");
  int fd = create_test_sock(target);
  ASSERT(fd >= 0);
  close(fd);
  char *link = mem_path(root, "link");
  ASSERT_EQ(symlink(target, link), 0);

  uid_t test_uid = (getuid() == 1000) ? 1001 : 1000;
  ASSERT_EQ(acl_set(link, test_uid, 1000, NULL, 0), -1);
  ASSERT_EQ(_mock_setxattr_count, 0);

  mem_free(link);
  mem_free(target);
  test_rmtree(root);
  mem_free(root);
}

static void test_acl_set_fd_success(void)
{
  mock_sys_reset();
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "fd-sock");
  int sfd = create_test_sock(path);
  ASSERT(sfd >= 0);
  int fd = sys_open_rdonly(path, 0);
  ASSERT(fd >= 0);

  uid_t test_uid = (getuid() == 1000) ? 1001 : 1000;
  ASSERT_EQ(acl_set_fd(fd, test_uid, 1000, NULL, 0), 0);
  ASSERT_EQ(_mock_setxattr_count, 1);

  close(fd);
  close(sfd);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_acl_guest_set_success(void)
{
  mock_sys_reset();
  _mock_setxattr_ret = 0;

  char *gr = test_temp_root();
  ASSERT_NOT_NULL(gr);
  char *sd = mem_path(gr, "guest");
  io_mkdir(sd);
  const char *target = "test-file";
  char *full = mem_path(sd, target);
  int fd = create_test_sock(full);
  ASSERT(fd >= 0);
  close(fd);

  uid_t test_uid = (getuid() == 1000) ? 1001 : 1000;
  ASSERT_EQ(acl_guest_set(full, test_uid, 1000, NULL, 0), 0);
  ASSERT_EQ(_mock_setxattr_count, 1);

  mem_free(full);
  mem_free(sd);
  test_rmtree(gr);
  mem_free(gr);
}

static void test_acl_guest_set_nonexistent(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *p = mem_path(root, "nope");
  ASSERT_EQ(acl_guest_set(p, 1000, 1000, NULL, 0), -1);
  mem_free(p);
  test_rmtree(root);
  mem_free(root);
}

SUITE(acl)
{
  RUN_TEST(acl_set_root_uid);
  RUN_TEST(acl_set_same_uid);
  RUN_TEST(acl_set_calls_setxattr);
  RUN_TEST(acl_set_setxattr_fails);
  RUN_TEST(acl_set_nonexistent_path);
  RUN_TEST(acl_set_symlink_rejected);
  RUN_TEST(acl_set_fd_success);
  RUN_TEST(acl_guest_set_success);
  RUN_TEST(acl_guest_set_nonexistent);
}

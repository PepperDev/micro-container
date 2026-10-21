#include "framework.h"
#include "test_util.h"
#include "sandbox/cgroup.h"
#include "util/io.h"
#include "util/mem.h"
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include "mock.h"

/* rmtree moved to shared test_util.h */

static void seed_procs(const char *dir)
{
  io_mkdir(dir);
  char *p = mem_path(dir, "cgroup.procs");
  io_touch(p);
  mem_free(p);
}

static void seed_v2_cage(const char *root, const char *name)
{
  char *cg = mem_path(root, "sys/fs/cgroup");
  char *cage = mem_path(cg, name);
  seed_procs(cage);
  mem_free(cage);
  mem_free(cg);
}

static void seed_v1_cages(const char *root, const char *name)
{
  char names[32][64];
  int n = read_proc_cgroups(names);
  for (int i = 0; i < n; i++) {
    char *cg = mem_path(root, "sys/fs/cgroup");
    char *ctrl = mem_path(cg, names[i]);
    char *cage = mem_path(ctrl, name);
    seed_procs(cage);
    mem_free(cage);
    mem_free(ctrl);
    mem_free(cg);
  }
}

static void test_cgroup_cleanup_empty(void)
{
  cgroup_cleanup(0, 0);
}

static void test_cgroup_get_type(void)
{
  int r = cgroup_get_type();
  ASSERT(r >= -1);
  /* /proc/filesystems is always readable on Linux */
  ASSERT(r >= 0);
  ASSERT(r <= 2);
}

static void test_cgroup_get_type_cached(void)
{
  int r1 = cgroup_get_type();
  int r2 = cgroup_get_type();
  ASSERT_EQ(r1, r2);
}

static void test_cgroup_cleanup_nonexistent(void)
{
  /* exercise fs_type=2 path with nonexistent cgroup dir */
  cgroup_cleanup(getpid(), 2);
}

static void test_cgroup_cleanup_removes_dir(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *cg = mem_path(root, "sys/fs/cgroup");
  ASSERT_EQ(io_mkdir(cg), 0);
  char *cage = mem_path(cg, "cage-12345");
  ASSERT_EQ(io_mkdir(cage), 0);
  ASSERT_EQ(io_exists(cage), 0);
  cgroup_cleanup_at(cg, 12345, 2);
  ASSERT_NE(io_exists(cage), 0);
  mem_free(cage);
  mem_free(cg);
  test_rmtree(root);
  mem_free(root);
}

static void test_cgroup_cleanup_at_v1_noop(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *cage = mem_path(root, "cage-1");
  ASSERT_EQ(io_mkdir(cage), 0);
  cgroup_cleanup_at(root, 1, 1);
  ASSERT_EQ(io_exists(cage), 0);
  mem_free(cage);
  test_rmtree(root);
  mem_free(root);
}

static void test_cgroup_cleanup_at_missing_noop(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  cgroup_cleanup_at(root, 4242, 2);
  char *cage = mem_path(root, "cage-4242");
  ASSERT_NE(io_exists(cage), 0);
  mem_free(cage);
  test_rmtree(root);
  mem_free(root);
}

static void test_v2_creates_dirs(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  seed_v2_cage(root, "cage-1");
  int r = cgroup_v2_enter(root, "cage-1", 1);
  ASSERT_EQ(r, 0);
  char *cg = mem_path(root, "sys/fs/cgroup");
  ASSERT_EQ(io_isdir(cg), 0);
  char *cage = mem_path(cg, "cage-1");
  ASSERT_EQ(io_isdir(cage), 0);
  mem_free(cage);
  mem_free(cg);
  test_rmtree(root);
  mem_free(root);
}

static void test_v2_creates_procs(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  seed_v2_cage(root, "cage-1");
  int r = cgroup_v2_enter(root, "cage-1", 1);
  ASSERT_EQ(r, 0);
  char *cg = mem_path(root, "sys/fs/cgroup/cage-1");
  char *procs = mem_path(cg, "cgroup.procs");
  int fd = open(procs, O_RDONLY);
  ASSERT_NE(fd, -1);
  char buf[64] = { 0 };
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  ASSERT(n > 0);
  ASSERT_EQ(strcmp(buf, "1\n"), 0);
  mem_free(procs);
  mem_free(cg);
  test_rmtree(root);
  mem_free(root);
}

static void test_v2_with_pid_42(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  seed_v2_cage(root, "cage-42");
  int r = cgroup_v2_enter(root, "cage-42", 42);
  ASSERT_EQ(r, 0);
  char *cage = mem_path(root, "sys/fs/cgroup/cage-42");
  ASSERT_EQ(io_isdir(cage), 0);
  mem_free(cage);
  test_rmtree(root);
  mem_free(root);
}

static void test_v2_multiple_pids(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  seed_v2_cage(root, "cage-1");
  seed_v2_cage(root, "cage-999");
  seed_v2_cage(root, "cage-12345");
  ASSERT_EQ(cgroup_v2_enter(root, "cage-1", 1), 0);
  ASSERT_EQ(cgroup_v2_enter(root, "cage-999", 999), 0);
  ASSERT_EQ(cgroup_v2_enter(root, "cage-12345", 12345), 0);
  char *c1 = mem_path(root, "sys/fs/cgroup/cage-1");
  ASSERT_EQ(io_isdir(c1), 0);
  mem_free(c1);
  char *c2 = mem_path(root, "sys/fs/cgroup/cage-999");
  ASSERT_EQ(io_isdir(c2), 0);
  mem_free(c2);
  char *c3 = mem_path(root, "sys/fs/cgroup/cage-12345");
  ASSERT_EQ(io_isdir(c3), 0);
  mem_free(c3);
  test_rmtree(root);
  mem_free(root);
}

static void test_v2_deep_path(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  seed_v2_cage(root, "cage-7");
  int r = cgroup_v2_enter(root, "cage-7", 7);
  ASSERT_EQ(r, 0);
  char *cg = mem_path(root, "sys/fs/cgroup");
  ASSERT_EQ(io_isdir(cg), 0);
  char *cage = mem_path(cg, "cage-7");
  ASSERT_EQ(io_isdir(cage), 0);
  mem_free(cage);
  mem_free(cg);
  test_rmtree(root);
  mem_free(root);
}

static void test_v2_sys_blocked_by_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *block = mem_path(root, "sys");
  int fd = open(block, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  int r = cgroup_v2_enter(root, "cage-1", 1);
  ASSERT_EQ(r, -1);
  mem_free(block);
  test_rmtree(root);
  mem_free(root);
}

static void test_v2_root_is_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *block = mem_path(root, "block");
  int fd = open(block, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  int r = cgroup_v2_enter(block, "cage-1", 1);
  ASSERT_EQ(r, -1);
  mem_free(block);
  test_rmtree(root);
  mem_free(root);
}

static void test_v2_with_pid_0(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  seed_v2_cage(root, "cage-0");
  int r = cgroup_v2_enter(root, "cage-0", 0);
  ASSERT_EQ(r, 0);
  char *cage = mem_path(root, "sys/fs/cgroup/cage-0");
  ASSERT_EQ(io_isdir(cage), 0);
  mem_free(cage);
  test_rmtree(root);
  mem_free(root);
}

static void test_v2_existing_path(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  seed_v2_cage(root, "cage-1");
  ASSERT_EQ(cgroup_v2_enter(root, "cage-1", 1), 0);
  ASSERT_EQ(cgroup_v2_enter(root, "cage-1", 1), 0);
  char *cage = mem_path(root, "sys/fs/cgroup/cage-1");
  ASSERT_EQ(io_isdir(cage), 0);
  mem_free(cage);
  test_rmtree(root);
  mem_free(root);
}

static void test_write_pid_fail(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "nonexistent/write.pid");
  ASSERT_EQ(write_pid(pf, 42), -1);
  mem_free(pf);
  test_rmtree(root);
  mem_free(root);
}

static void test_write_pid_missing_rejected(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "missing.pid");
  ASSERT_EQ(write_pid(pf, 42), -1);
  ASSERT_EQ(io_exists(pf), 1);
  mem_free(pf);
  test_rmtree(root);
  mem_free(root);
}

static void test_write_pid_success(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "writepid");
  io_touch(pf);
  ASSERT_EQ(write_pid(pf, 123), 0);
  int fd = open(pf, O_RDONLY);
  ASSERT_NE(fd, -1);
  char buf[32] = { 0 };
  ASSERT_NE(read(fd, buf, sizeof(buf) - 1), -1);
  ASSERT_EQ(strcmp(buf, "123\n"), 0);
  close(fd);
  mem_free(pf);
  test_rmtree(root);
  mem_free(root);
}

static void test_advance_line_with_newline(void)
{
  char buf[] = "hello\nworld";
  char *next = advance_line(buf);
  ASSERT_EQ(next, buf + 6);
  ASSERT_EQ(memcmp(next, "world", 5), 0);
}

static void test_advance_line_no_newline(void)
{
  char buf[] = "hello";
  char *next = advance_line(buf);
  ASSERT_EQ(next, buf + 5);
  ASSERT_EQ(*next, '\0');
}

static void test_advance_line_empty(void)
{
  char buf[] = "";
  char *next = advance_line(buf);
  ASSERT_EQ(next, buf);
  ASSERT_EQ(*next, '\0');
}

static void test_read_proc_cgroups_exists(void)
{
  char names[32][64];
  int n = read_proc_cgroups(names);
  ASSERT(n >= 0);
  if (n > 0)
    ASSERT(strlen(names[0]) > 0);
}

static void test_v2_enter_basic(void)
{
  char *tmp = test_temp_root();
  ASSERT_NOT_NULL(tmp);
  seed_v2_cage(tmp, "test-cage");
  ASSERT_EQ(cgroup_v2_enter(tmp, "test-cage", 77), 0);
  ASSERT_EQ(cgroup_v2_enter(tmp, "test-cage", 77), 0);
  char *procs = mem_path(tmp, "sys/fs/cgroup/test-cage/cgroup.procs");
  int fd = open(procs, O_RDONLY);
  ASSERT_NE(fd, -1);
  char buf[32] = { 0 };
  ASSERT_NE(read(fd, buf, sizeof(buf) - 1), -1);
  ASSERT_EQ(strcmp(buf, "77\n"), 0);
  close(fd);
  mem_free(procs);
  test_rmtree(tmp);
  mem_free(tmp);
}

static void test_v1_enter_one_creates_dirs(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  (void)cgroup_v1_enter_one(root, "memory", "test-cage-1", 42);
  char *cgdir = mem_path(root, "memory/test-cage-1");
  ASSERT_EQ(io_isdir(cgdir), 0);
  mem_free(cgdir);
  test_rmtree(root);
  mem_free(root);
}

static void test_v1_enter_creates_dirs(void)
{
  char names[32][64];
  int n = read_proc_cgroups(names);
  if (n <= 0)
    return;
  char *tmp = test_temp_root();
  ASSERT_NOT_NULL(tmp);
  seed_v1_cages(tmp, "test-cage-99");
  int r = cgroup_v1_enter(tmp, "test-cage-99", 99);
  ASSERT_EQ(r, 0);
  test_rmtree(tmp);
  mem_free(tmp);
}

static void test_build_subtree_basic(void)
{
  char out[256];
  size_t len;
  ASSERT_EQ(build_subtree_value("cpu io pids", 11, out, sizeof(out), &len), 0);
  ASSERT_EQ(len, 14);
  ASSERT_EQ(strcmp(out, "+cpu +io +pids"), 0);
}

static void test_build_subtree_single(void)
{
  char out[256];
  size_t len;
  ASSERT_EQ(build_subtree_value("cpu", 3, out, sizeof(out), &len), 0);
  ASSERT_EQ(len, 4);
  ASSERT_EQ(strcmp(out, "+cpu"), 0);
}

static void test_build_subtree_empty(void)
{
  char out[256];
  size_t len;
  ASSERT_EQ(build_subtree_value("", 0, out, sizeof(out), &len), -1);
}

static void test_build_subtree_all_spaces_fits(void)
{
  char in[126];
  memset(in, ' ', sizeof(in));
  char out[256];
  size_t len;
  ASSERT_EQ(build_subtree_value(in, sizeof(in), out, sizeof(out), &len), 0);
  ASSERT_EQ(len, 1 + 2 * sizeof(in));
  ASSERT_EQ(out[len], '\0');
}

static void test_build_subtree_overflow_rejected(void)
{
  char in[128];
  memset(in, ' ', sizeof(in));
  char out[256];
  size_t len;
  ASSERT_EQ(build_subtree_value(in, sizeof(in), out, sizeof(out), &len), -1);
}

static void test_write_subtree_basic(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *sub = mem_path(root, "cgroup.subtree_control");
  int fd = open(sub, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(sub);

  ASSERT_EQ(write_subtree(root, "+cpu +io", 8), 0);

  sub = mem_path(root, "cgroup.subtree_control");
  char buf[32] = { 0 };
  fd = open(sub, O_RDONLY);
  ASSERT_NE(fd, -1);
  ASSERT_NE(read(fd, buf, sizeof(buf) - 1), -1);
  ASSERT_EQ(strcmp(buf, "+cpu +io"), 0);
  close(fd);
  mem_free(sub);
  test_rmtree(root);
  mem_free(root);
}

static void test_write_subtree_fail(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  ASSERT_EQ(write_subtree(root, "+cpu", 4), -1);
  test_rmtree(root);
  mem_free(root);
}

static void test_enable_cgroup_controllers_success(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *cg = mem_path(root, "sys/fs/cgroup");
  ASSERT_EQ(io_mkdir(cg), 0);

  char *ctrls = mem_path(cg, "cgroup.controllers");
  int fd = open(ctrls, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_NE(write(fd, "cpu io pids\n", 12), -1);
  close(fd);
  mem_free(ctrls);

  char *sub = mem_path(cg, "cgroup.subtree_control");
  fd = open(sub, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(sub);

  ASSERT_EQ(enable_cgroup_controllers(cg), 0);

  sub = mem_path(cg, "cgroup.subtree_control");
  char buf[256] = { 0 };
  fd = open(sub, O_RDONLY);
  ASSERT_NE(fd, -1);
  ASSERT_NE(read(fd, buf, sizeof(buf) - 1), -1);
  ASSERT_EQ(strcmp(buf, "+cpu +io +pids"), 0);
  close(fd);
  mem_free(sub);
  mem_free(cg);
  test_rmtree(root);
  mem_free(root);
}

static void test_enable_cgroup_controllers_no_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *cg = mem_path(root, "sys/fs/cgroup");
  ASSERT_EQ(io_mkdir(cg), 0);
  ASSERT_EQ(enable_cgroup_controllers(cg), 0);
  mem_free(cg);
  test_rmtree(root);
  mem_free(root);
}

static void test_pre_unshare_not_root(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  int r = cgroup_pre_unshare_at(root, 2, getpid());
  test_rmtree(root);
  mem_free(root);
  if (geteuid() == 0)
    return;
  ASSERT_EQ(r, -1);
}

static void test_pre_unshare_success(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *ctrl = mem_path(root, "cgroup.controllers");
  int fd = open(ctrl, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_NE(write(fd, "cpu io pids\n", 12), -1);
  close(fd);
  mem_free(ctrl);

  char *sub = mem_path(root, "cgroup.subtree_control");
  fd = open(sub, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(sub);

  char cgdir[PATH_MAX];
  snprintf(cgdir, sizeof(cgdir), "%s/cage-%d", root, (int)getpid());
  ASSERT_EQ(io_mkdir(cgdir), 0);

  char *procs = mem_path(cgdir, "cgroup.procs");
  fd = open(procs, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(procs);

  int r = cgroup_pre_unshare_at(root, 2, getpid());
  ASSERT_EQ(r, 0);

  procs = mem_path(cgdir, "cgroup.procs");
  fd = open(procs, O_RDONLY);
  ASSERT_NE(fd, -1);
  char buf[64] = { 0 };
  ASSERT_NE(read(fd, buf, sizeof(buf) - 1), -1);
  close(fd);
  mem_free(procs);
  char pid[32];
  snprintf(pid, sizeof(pid), "%d\n", (int)getpid());
  ASSERT_EQ(strcmp(buf, pid), 0);

  test_rmtree(root);
  mem_free(root);
}

static void test_pre_unshare_subtree_control(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *ctrl = mem_path(root, "cgroup.controllers");
  int fd = open(ctrl, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_NE(write(fd, "cpu io pids\n", 12), -1);
  close(fd);
  mem_free(ctrl);

  char *sub_root = mem_path(root, "cgroup.subtree_control");
  fd = open(sub_root, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(sub_root);

  char cgdir[PATH_MAX];
  snprintf(cgdir, sizeof(cgdir), "%s/cage-%d", root, (int)getpid());
  ASSERT_EQ(io_mkdir(cgdir), 0);

  char *procs = mem_path(cgdir, "cgroup.procs");
  fd = open(procs, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(procs);

  char *child_ctrl = mem_path(cgdir, "cgroup.controllers");
  fd = open(child_ctrl, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_NE(write(fd, "cpu io pids\n", 12), -1);
  close(fd);
  mem_free(child_ctrl);

  char *sub = mem_path(cgdir, "cgroup.subtree_control");
  fd = open(sub, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(sub);

  int r = cgroup_pre_unshare_at(root, 2, getpid());
  ASSERT_EQ(r, 0);

  sub = mem_path(cgdir, "cgroup.subtree_control");
  fd = open(sub, O_RDONLY);
  ASSERT_NE(fd, -1);
  char buf[256] = { 0 };
  ASSERT_NE(read(fd, buf, sizeof(buf) - 1), -1);
  close(fd);
  ASSERT_EQ(strcmp(buf, "+cpu +io +pids"), 0);
  mem_free(sub);

  test_rmtree(root);
  mem_free(root);
}

static void test_pre_unshare_fails_no_procs(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *ctrl = mem_path(root, "cgroup.controllers");
  int fd = open(ctrl, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_NE(write(fd, "cpu\n", 4), -1);
  close(fd);
  mem_free(ctrl);

  char cgdir[PATH_MAX];
  snprintf(cgdir, sizeof(cgdir), "%s/cage-%d", root, (int)getpid());
  ASSERT_EQ(io_mkdir(cgdir), 0);

  int r = cgroup_pre_unshare_at(root, 2, getpid());
  ASSERT_EQ(r, -1);

  test_rmtree(root);
  mem_free(root);
}

static void test_pre_unshare_no_controllers(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  int r = cgroup_pre_unshare_at(root, 2, getpid());
  if (geteuid() == 0)
    return;
  ASSERT_EQ(r, -1);
  test_rmtree(root);
  mem_free(root);
}

static void test_cgroup_enter_with_mock_root(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  seed_v2_cage(root, "cage-parent");
  int r = cgroup_enter(root, 1);
  ASSERT_EQ(r, 0);
  char *cg = mem_path(root, "sys/fs/cgroup/cage-parent");
  ASSERT_EQ(io_isdir(cg), 0);
  mem_free(cg);
  test_rmtree(root);
  mem_free(root);
}

static void test_cgroup_pre_unshare_no_root(void)
{
  if (geteuid() == 0)
    return;
  int r = cgroup_pre_unshare(2, getpid());
  ASSERT_EQ(r, -1);
}

static void test_pre_unshare_mkdir_fails(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *ctrl = mem_path(root, "cgroup.controllers");
  int fd = open(ctrl, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_NE(write(fd, "cpu\n", 4), -1);
  close(fd);
  mem_free(ctrl);

  char *sub = mem_path(root, "cgroup.subtree_control");
  fd = open(sub, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(sub);

  ASSERT_EQ(chmod(root, 0555), 0);

  int r = cgroup_pre_unshare_at(root, 2, getpid());
  if (geteuid() == 0)
    return;
  ASSERT_EQ(r, -1);

  ASSERT_EQ(chmod(root, 0755), 0);
  test_rmtree(root);
  mem_free(root);
}

static void test_pre_unshare_unreadable_controllers(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *ctrl = mem_path(root, "cgroup.controllers");
  int fd = open(ctrl, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_NE(write(fd, "cpu io pids\n", 12), -1);
  close(fd);
  ASSERT_EQ(chmod(ctrl, 0000), 0);
  mem_free(ctrl);

  char *sub = mem_path(root, "cgroup.subtree_control");
  fd = open(sub, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(sub);

  char cgdir[PATH_MAX];
  snprintf(cgdir, sizeof(cgdir), "%s/cage-%d", root, (int)getpid());
  ASSERT_EQ(io_mkdir(cgdir), 0);

  char *procs = mem_path(cgdir, "cgroup.procs");
  fd = open(procs, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(procs);

  int r = cgroup_pre_unshare_at(root, 2, getpid());
  ASSERT_EQ(r, 0);

  test_rmtree(root);
  mem_free(root);
}

static void test_pre_unshare_mount_root_bad_path(void)
{
  char *tmp = test_temp_root();
  ASSERT_NOT_NULL(tmp);

  char *block = mem_path(tmp, "block");
  int fd = open(block, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  close(fd);

  char *bad = mem_path(block, "cgroup2");

  int r = cgroup_pre_unshare_at(bad, 2, getpid());
  ASSERT_EQ(r, -1);

  mem_free(bad);
  mem_free(block);
  test_rmtree(tmp);
  mem_free(tmp);
}

static void test_cgroup_rmdir_recursive_empty(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  ASSERT_EQ(cgroup_rmdir_recursive(root), 0);
  ASSERT_NE(io_exists(root), 0);
  mem_free(root);
}

static void test_cgroup_rmdir_recursive_nested(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *child = mem_path(root, "cage-parent");
  ASSERT_EQ(io_mkdir(child), 0);
  char *deep = mem_path(child, "sub");
  ASSERT_EQ(io_mkdir(deep), 0);
  mem_free(deep);
  ASSERT_EQ(cgroup_rmdir_recursive(root), 0);
  ASSERT_NE(io_exists(root), 0);
  ASSERT_NE(io_exists(child), 0);
  mem_free(child);
  mem_free(root);
}

static void test_cgroup_rmdir_recursive_preserves_files(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *procs = mem_path(root, "cgroup.procs");
  int fd = open(procs, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(procs);
  ASSERT_EQ(cgroup_rmdir_recursive(root), -1);
  procs = mem_path(root, "cgroup.procs");
  ASSERT_EQ(io_exists(procs), 0);
  mem_free(procs);
  test_rmtree(root);
  mem_free(root);
}

static void test_cgroup_rmdir_recursive_nonexistent(void)
{
  ASSERT_EQ(cgroup_rmdir_recursive("/nonexistent/foo"), 0);
}

static void test_cgroup_rmdir_recursive_subdir_with_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *child = mem_path(root, "cage-parent");
  ASSERT_EQ(io_mkdir(child), 0);
  char *procs = mem_path(child, "cgroup.procs");
  int fd = open(procs, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(procs);
  int ret = cgroup_rmdir_recursive(root);
  ASSERT_EQ(ret, -1);
  procs = mem_path(child, "cgroup.procs");
  ASSERT_EQ(io_exists(procs), 0);
  mem_free(procs);
  test_rmtree(root);
  mem_free(root);
}

static void test_pre_unshare_no_op(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  ASSERT_EQ(cgroup_pre_unshare_at(root, 0, 42), 0);
  ASSERT_EQ(cgroup_pre_unshare_at(root, 1, 42), 0);
  test_rmtree(root);
  mem_free(root);
}

static void test_cgroup_enter_v1(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  seed_v1_cages(root, "cage-parent");
  ASSERT_EQ(cgroup_enter(root, 0), 0);
  test_rmtree(root);
  mem_free(root);
}

static void test_pre_unshare_subtree_control_readonly(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *ctrl = mem_path(root, "cgroup.controllers");
  int fd = open(ctrl, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_NE(write(fd, "cpu io pids\n", 12), -1);
  close(fd);
  mem_free(ctrl);

  char *sub_root = mem_path(root, "cgroup.subtree_control");
  fd = open(sub_root, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(sub_root);

  char cgdir[PATH_MAX];
  snprintf(cgdir, sizeof(cgdir), "%s/cage-%d", root, (int)getpid());
  ASSERT_EQ(io_mkdir(cgdir), 0);

  char *procs = mem_path(cgdir, "cgroup.procs");
  fd = open(procs, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(procs);

  char *child_ctrl = mem_path(cgdir, "cgroup.controllers");
  fd = open(child_ctrl, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_NE(write(fd, "cpu io pids\n", 12), -1);
  close(fd);
  mem_free(child_ctrl);

  char *sub = mem_path(cgdir, "cgroup.subtree_control");
  fd = open(sub, O_WRONLY | O_CREAT, 0644);
  ASSERT_NE(fd, -1);
  close(fd);
  ASSERT_EQ(chmod(sub, 0444), 0);
  mem_free(sub);

  int r = cgroup_pre_unshare_at(root, 2, getpid());
  ASSERT_EQ(r, 0);

  test_rmtree(root);
  mem_free(root);
}

SUITE(cgroup)
{
  RUN_TEST(cgroup_cleanup_empty);
  RUN_TEST(cgroup_get_type);
  RUN_TEST(cgroup_get_type_cached);
  RUN_TEST(cgroup_cleanup_nonexistent);
  RUN_TEST(cgroup_cleanup_removes_dir);
  RUN_TEST(cgroup_cleanup_at_v1_noop);
  RUN_TEST(cgroup_cleanup_at_missing_noop);
  RUN_TEST(cgroup_rmdir_recursive_empty);
  RUN_TEST(cgroup_rmdir_recursive_nested);
  RUN_TEST(cgroup_rmdir_recursive_preserves_files);
  RUN_TEST(cgroup_rmdir_recursive_nonexistent);
  RUN_TEST(cgroup_rmdir_recursive_subdir_with_file);
  RUN_TEST(v2_creates_dirs);
  RUN_TEST(v2_creates_procs);
  RUN_TEST(v2_with_pid_42);
  RUN_TEST(v2_multiple_pids);
  RUN_TEST(v2_deep_path);
  RUN_TEST(v2_sys_blocked_by_file);
  RUN_TEST(v2_root_is_file);
  RUN_TEST(v2_with_pid_0);
  RUN_TEST(v2_existing_path);
  RUN_TEST(v2_enter_basic);
  RUN_TEST(v1_enter_one_creates_dirs);
  RUN_TEST(v1_enter_creates_dirs);
  RUN_TEST(write_pid_fail);
  RUN_TEST(write_pid_missing_rejected);
  RUN_TEST(write_pid_success);
  RUN_TEST(advance_line_with_newline);
  RUN_TEST(advance_line_no_newline);
  RUN_TEST(advance_line_empty);
  RUN_TEST(read_proc_cgroups_exists);
  RUN_TEST(build_subtree_basic);
  RUN_TEST(build_subtree_single);
  RUN_TEST(build_subtree_empty);
  RUN_TEST(build_subtree_all_spaces_fits);
  RUN_TEST(build_subtree_overflow_rejected);
  RUN_TEST(write_subtree_basic);
  RUN_TEST(write_subtree_fail);
  RUN_TEST(enable_cgroup_controllers_success);
  RUN_TEST(enable_cgroup_controllers_no_file);
  RUN_TEST(pre_unshare_not_root);
  RUN_TEST(pre_unshare_success);
  RUN_TEST(pre_unshare_subtree_control);
  RUN_TEST(pre_unshare_fails_no_procs);
  RUN_TEST(pre_unshare_no_controllers);
  RUN_TEST(pre_unshare_mkdir_fails);
  RUN_TEST(cgroup_enter_with_mock_root);
  RUN_TEST(cgroup_pre_unshare_no_root);
  RUN_TEST(pre_unshare_unreadable_controllers);
  RUN_TEST(pre_unshare_mount_root_bad_path);
  RUN_TEST(pre_unshare_no_op);
  RUN_TEST(cgroup_enter_v1);
  RUN_TEST(pre_unshare_subtree_control_readonly);
}

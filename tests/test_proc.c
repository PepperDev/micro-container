#include "framework.h"
#include "test_util.h"
#include "core/proc.h"
#include "util/io.h"
#include "mock.h"
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static int make_pidfile(const char *path, pid_t pid)
{
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0)
    return -1;
  if (flock(fd, LOCK_EX) != 0) {
    close(fd);
    return -1;
  }
  char buf[64];
  int n = snprintf(buf, sizeof(buf), "%d", (int)pid);
  if (write(fd, buf, (size_t)n) != (ssize_t) n) {
    close(fd);
    return -1;
  }
  return close_pid(fd);
}

static int wait_exited(pid_t pid)
{
  siginfo_t si;
  return waitid(P_PID, (id_t) pid, &si, WEXITED | WNOWAIT) == 0;
}

static void test_killpid_nonexistent_pidfile(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "nonexistent");
  ASSERT_EQ(killpid(NULL, pf), 0);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_killpid_stale_pidfile(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "stale.pid");
  ASSERT_EQ(make_pidfile(pf, 99999999), 0);
  ASSERT_EQ(killpid(NULL, pf), 0);
  ASSERT_EQ(io_exists(pf), 1);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_killpid_alive_child(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "alive.pid");
  pid_t child = pidfork();
  if (child == 0) {
    pause();
    _exit(0);
  }
  ASSERT_EQ(make_pidfile(pf, child), 0);
  ASSERT_EQ(killpid(NULL, pf), 0);
  ASSERT_EQ(io_exists(pf), 1);
  pidwait(child, NULL);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_killpid_kill_fails(void)
{
  mock_sys_reset();
  _mock_kill_ret = 1;
  _mock_kill_errno = EPERM;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "kill-fail.pid");
  pid_t child = pidfork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    pause();
    _exit(0);
  }
  ASSERT_EQ(make_pidfile(pf, child), 0);
  ASSERT_EQ(killpid(NULL, pf), -1);
  ASSERT_EQ(io_exists(pf), 1);
  kill(child, SIGKILL);
  pidwait(child, NULL);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_join_namespaces_setns_fails(void)
{
  mock_sys_reset();
  _mock_setns_ret = -1;

  pid_t self = getpid();
  ASSERT_EQ(join_namespaces(self), -1);
  ASSERT_EQ(_mock_setns_count, 1);
}

static void test_join_namespaces_success(void)
{
  mock_sys_reset();
  _mock_setns_ret = 0;

  pid_t self = getpid();
  ASSERT_EQ(join_namespaces(self), 0);
  ASSERT_EQ(_mock_setns_count, 4);
}

static void test_detect_existing_no_pidfile(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "detect-none.pid");
  pid_t r = detect_existing("nonexistent", pf);
  ASSERT_EQ(r, 0);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_detect_existing_stale(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "detect-stale.pid");
  ASSERT_EQ(make_pidfile(pf, 99999999), 0);
  pid_t r = detect_existing("test", pf);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(io_exists(pf), 1);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_detect_existing_alive(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "detect-alive.pid");
  pid_t child = pidfork();
  if (child == 0) {
    pause();
    _exit(0);
  }
  ASSERT_EQ(make_pidfile(pf, child), 0);
  pid_t r = detect_existing("test", pf);
  ASSERT_EQ(r, child);
  kill(child, SIGKILL);
  pidwait(child, NULL);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_detect_existing_zombie(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "detect-zombie.pid");
  pid_t child = pidfork();
  ASSERT_NE(child, -1);
  if (child == 0)
    _exit(0);
  ASSERT_EQ(wait_exited(child), 1);
  _mock_pid_stat = "123 (cage) Z 1 123 123 0 -1\n";
  ASSERT_EQ(make_pidfile(pf, child), 0);
  pid_t r = detect_existing("test", pf);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(io_exists(pf), 1);
  pidwait(child, NULL);
  _mock_pid_stat = NULL;
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_killpid_zombie_target(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "kill-zombie.pid");
  pid_t child = pidfork();
  ASSERT_NE(child, -1);
  if (child == 0)
    _exit(0);
  ASSERT_EQ(wait_exited(child), 1);
  _mock_pid_stat = "123 (cage) Z 1 123 123 0 -1\n";
  ASSERT_EQ(make_pidfile(pf, child), 0);
  time_t t0 = time(NULL);
  ASSERT_EQ(killpid(NULL, pf), 0);
  ASSERT(time(NULL) - t0 <= 3);
  ASSERT_EQ(io_exists(pf), 1);
  pidwait(child, NULL);
  _mock_pid_stat = NULL;
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_pidfork_and_wait(void)
{
  pid_t pid = pidfork();
  ASSERT_NE(pid, -1);
  if (pid == 0)
    _exit(42);
  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT_EQ(WEXITSTATUS(status), 42);
}

static void test_pidexists_self(void)
{
  ASSERT_EQ(pidexists(getpid()), 0);
}

static void test_pidexists_dead(void)
{
  ASSERT_EQ(pidexists(99999999), 1);
}

static void test_readpid_success(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "readpid.pid");
  ASSERT_EQ(make_pidfile(pf, 12345), 0);

  int rfd;
  pid_t pid = readpid(pf, &rfd);
  ASSERT_EQ(pid, 12345);
  close_pid(rfd);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_compute_pidfile_no_name(void)
{
  char *pf = compute_pidfile(NULL, NULL);
  ASSERT_NOT_NULL(pf);
  ASSERT_STR_EQ(pf, "/run/cage/pid");
  mem_free(pf);
}

static void test_compute_pidfile_with_name(void)
{
  char *pf = compute_pidfile("testapp", NULL);
  ASSERT_NOT_NULL(pf);
  ASSERT_STR_EQ(pf, "/run/cage/testapp.pid");
  mem_free(pf);
}

static void test_create_pidfile_success(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "createpf.pid");
  int fd = create_pidfile(pf);
  ASSERT_NE(fd, -1);
  close_pid(fd);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_create_pidfile_existing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "createpf-exists.pid");
  io_touch(pf);
  int fd = create_pidfile(pf);
  ASSERT_EQ(fd, -1);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_create_pidfile_writes_pid(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "writepid.pid");
  int fd = create_pidfile(pf);
  ASSERT_NE(fd, -1);
  dprintf(fd, "%d", getpid());
  close_pid(fd);
  fd = open(pf, O_RDONLY | O_CLOEXEC);
  char buf[64];
  memset(buf, 0, sizeof(buf));
  read(fd, buf, sizeof(buf));
  close(fd);
  ASSERT_EQ(atoi(buf), getpid());
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_readpid_open_fails(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "nonexistent-readpid");
  pid_t pid = readpid(pf, NULL);
  ASSERT_EQ(pid, -1);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_readpid_read_fails(void)
{
  pid_t pid = readpid("/tmp", NULL);
  ASSERT_EQ(pid, -1);
}

static void test_pidexists_unexpected_errno(void)
{
  mock_sys_reset();
  _mock_kill_ret = 1;
  _mock_kill_errno = EPERM;
  ASSERT_EQ(pidexists(1), -1);
  _mock_kill_errno = ESRCH;
  ASSERT_EQ(pidexists(1), 1);
  _mock_kill_ret = 0;
  ASSERT_EQ(pidexists(1), 0);
}

static void test_readpid_invalid_content(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "readpid-invalid.pid");
  int fd = open(pf, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  write(fd, "notapid", 7);
  close(fd);

  pid_t pid = readpid(pf, NULL);
  ASSERT_EQ(pid, -1);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_readpid_zero_rejected(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "readpid-zero.pid");
  int fd = open(pf, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  write(fd, "0", 1);
  close(fd);

  ASSERT_EQ(readpid(pf, NULL), -1);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_readpid_negative_rejected(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "readpid-negative.pid");
  int fd = open(pf, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  write(fd, "-5", 2);
  close(fd);

  ASSERT_EQ(readpid(pf, NULL), -1);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_readpid_overflow_rejected(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "readpid-overflow.pid");
  int fd = open(pf, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  write(fd, "9999999999", 10);
  close(fd);

  ASSERT_EQ(readpid(pf, NULL), -1);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_readpid_no_fd(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "readpid-nofd.pid");
  ASSERT_EQ(make_pidfile(pf, 42), 0);

  pid_t pid = readpid(pf, NULL);
  ASSERT_EQ(pid, 42);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_pidwait_echild(void)
{
  pid_t pid = pidfork();
  if (pid == 0)
    _exit(0);
  int status = 0;
  ASSERT_EQ(pidwait(pid, &status), 0);

  /* A second wait has no child to report, so pidwait must not touch status */
  int sentinel = 0x5eed;
  ASSERT_EQ(pidwait(pid, &sentinel), 0);
  ASSERT_EQ(sentinel, 0x5eed);
}

static void test_killpid_readpid_invalid(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "badcontent.pid");
  int fd = open(pf, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  write(fd, "notapid", 7);
  close(fd);
  ASSERT_EQ(killpid(NULL, pf), -1);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_killpid_io_exists_error(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *file = mem_path(root, "notadir");
  io_touch(file);
  char *child = mem_path(root, "notadir/child");
  ASSERT_EQ(killpid(NULL, child), -1);
  mem_free(child);
  mem_free(file);
  io_rmtree(root);
  mem_free(root);
}

static void test_detect_existing_io_exists_error(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *file = mem_path(root, "notadir2");
  io_touch(file);
  char *child = mem_path(root, "notadir2/child");
  pid_t r = detect_existing("test", child);
  ASSERT_EQ(r, -1);
  mem_free(child);
  mem_free(file);
  io_rmtree(root);
  mem_free(root);
}

static void test_detect_existing_readpid_fails(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "detect-badpid.pid");
  int fd = open(pf, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  write(fd, "notapid", 7);
  close(fd);

  pid_t r = detect_existing("test", pf);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(io_exists(pf), 1);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_compute_pidfile_with_len(void)
{
  size_t len;
  char *pf = compute_pidfile(NULL, &len);
  ASSERT_NOT_NULL(pf);
  ASSERT_EQ(len, sizeof("/run/cage/pid") - 1);
  ASSERT_STR_EQ(pf, "/run/cage/pid");
  mem_free(pf);

  pf = compute_pidfile("testapp", &len);
  ASSERT_NOT_NULL(pf);
  ASSERT_EQ(len, sizeof("/run/cage/testapp.pid") - 1);
  ASSERT_STR_EQ(pf, "/run/cage/testapp.pid");
  mem_free(pf);
}

static void test_close_pid(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "close-pid.pid");
  int fd = create_pidfile(pf);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(close_pid(fd), 0);
  mem_free(pf);
  io_rmtree(root);
  mem_free(root);
}

static void test_exit_code_from_status_exited(void)
{
  ASSERT_EQ(exit_code_from_status(0 << 8), 0);
  ASSERT_EQ(exit_code_from_status(42 << 8), 42);
  ASSERT_EQ(exit_code_from_status(255 << 8), 255);
}

static void test_exit_code_from_status_signaled(void)
{
  ASSERT_EQ(exit_code_from_status(SIGKILL), 128 + SIGKILL);
  ASSERT_EQ(exit_code_from_status(SIGTERM), 128 + SIGTERM);
}

static void test_exit_code_from_status_real_signal(void)
{
  pid_t pid = pidfork();
  ASSERT_NE(pid, -1);
  if (pid == 0) {
    raise(SIGKILL);
    _exit(0);
  }
  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT_EQ(exit_code_from_status(status), 128 + SIGKILL);
}

SUITE(proc)
{
  RUN_TEST(killpid_nonexistent_pidfile);
  RUN_TEST(killpid_stale_pidfile);
  RUN_TEST(killpid_alive_child);
  RUN_TEST(killpid_kill_fails);
  RUN_TEST(join_namespaces_setns_fails);
  RUN_TEST(join_namespaces_success);
  RUN_TEST(detect_existing_no_pidfile);
  RUN_TEST(detect_existing_stale);
  RUN_TEST(detect_existing_alive);
  RUN_TEST(detect_existing_zombie);
  RUN_TEST(killpid_zombie_target);
  RUN_TEST(pidfork_and_wait);
  RUN_TEST(pidexists_self);
  RUN_TEST(pidexists_dead);
  RUN_TEST(readpid_success);
  RUN_TEST(compute_pidfile_no_name);
  RUN_TEST(compute_pidfile_with_name);
  RUN_TEST(create_pidfile_success);
  RUN_TEST(create_pidfile_existing);
  RUN_TEST(create_pidfile_writes_pid);
  RUN_TEST(readpid_open_fails);
  RUN_TEST(readpid_read_fails);
  RUN_TEST(readpid_invalid_content);
  RUN_TEST(readpid_zero_rejected);
  RUN_TEST(readpid_negative_rejected);
  RUN_TEST(readpid_overflow_rejected);
  RUN_TEST(readpid_no_fd);
  RUN_TEST(pidwait_echild);
  RUN_TEST(killpid_readpid_invalid);
  RUN_TEST(killpid_io_exists_error);
  RUN_TEST(detect_existing_io_exists_error);
  RUN_TEST(detect_existing_readpid_fails);
  RUN_TEST(compute_pidfile_with_len);
  RUN_TEST(close_pid);
  RUN_TEST(exit_code_from_status_exited);
  RUN_TEST(exit_code_from_status_signaled);
  RUN_TEST(exit_code_from_status_real_signal);
  RUN_TEST(pidexists_unexpected_errno);
}

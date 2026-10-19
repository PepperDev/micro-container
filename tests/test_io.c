#include "framework.h"
#include "test_util.h"
#include "util/io.h"
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#define CHUNK 4096

static void io_umask(mode_t mask)
{
  umask(mask);
}

static int io_blankhead(const char *path)
{
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    fprintf(stderr, "cage: unable to open %s\n", path);
    return -1;
  }
  unsigned char buf[CHUNK];
  ssize_t n = read(fd, buf, sizeof(buf));
  close(fd);
  if (n < 0) {
    fprintf(stderr, "cage: unable to read %s\n", path);
    return -1;
  }
  for (ssize_t i = 0; i < n; i++)
    if (buf[i] != 0)
      return 1;
  return 0;
}

static void test_truncate_sparse_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "sparse.img");
  ASSERT_EQ(io_truncate(path, 10485760), 0);
  struct stat st;
  ASSERT_EQ(stat(path, &st), 0);
  ASSERT_EQ(st.st_size, 10485760);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_truncate_zero_size(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "zero.img");
  ASSERT_EQ(io_truncate(path, 0), 0);
  struct stat st;
  ASSERT_EQ(stat(path, &st), 0);
  ASSERT_EQ(st.st_size, 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_blankhead_empty_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "blank");
  int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  close(fd);
  ASSERT_EQ(io_blankhead(path), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_blankhead_nonzero_content(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonblank");
  int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  write(fd, "x", 1);
  close(fd);
  ASSERT_EQ(io_blankhead(path), 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_blankhead_larger_zeros(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "blank-lg");
  ASSERT_EQ(io_truncate(path, 10485760), 0);
  ASSERT_EQ(io_blankhead(path), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static int io_samefs(const char *a, const char *b)
{
  struct stat sa, sb;
  if (stat(a, &sa) != 0 || stat(b, &sb) != 0) {
    fprintf(stderr, "cage: unable to stat for same-fs check\n");
    return -1;
  }
  return sa.st_dev == sb.st_dev ? 1 : 0;
}

static void test_samefs_same_file(void)
{
  ASSERT_EQ(io_samefs("/", "/"), 1);
}

static void test_samefs_different_paths(void)
{
  ASSERT_EQ(io_samefs("/", "/tmp"), 1);
}

static void test_samefs_missing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent-xyz");
  ASSERT_EQ(io_samefs(path, "/tmp"), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_exists_missing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent");
  ASSERT_EQ(io_exists(path), 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_exists_existing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "exists");
  io_touch(path);
  ASSERT_EQ(io_exists(path), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_isdir_dir(void)
{
  ASSERT_EQ(io_isdir("/tmp"), 0);
}

static void test_isdir_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "isdir-file");
  io_touch(path);
  ASSERT_EQ(io_isdir(path), 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_isdir_missing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent-dir");
  ASSERT_EQ(io_isdir(path), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_islink_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "islink-file");
  io_touch(path);
  ASSERT_EQ(io_islink(path), 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_islink_symlink(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *target = mem_path(root, "islink-target");
  char *link = mem_path(root, "islink-link");
  io_touch(target);
  io_symlink(target, link);
  ASSERT_EQ(io_islink(link), 0);
  mem_free(target);
  mem_free(link);
  test_rmtree(root);
  mem_free(root);
}

static void test_islink_missing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent-link");
  ASSERT_EQ(io_islink(path), 2);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_symlink_basic(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *target = mem_path(root, "symlink-target");
  char *link = mem_path(root, "symlink-link");
  io_touch(target);
  ASSERT_EQ(io_symlink(target, link), 0);
  struct stat st;
  ASSERT_EQ(lstat(link, &st), 0);
  ASSERT_NE(S_ISLNK(st.st_mode), 0);
  mem_free(target);
  mem_free(link);
  test_rmtree(root);
  mem_free(root);
}

static void test_symlink_existing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "symlink-existing");
  io_touch(path);
  ASSERT_EQ(io_symlink("/some/target", path), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_mkdir_simple(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "mkdir");
  ASSERT_EQ(io_mkdir(path), 0);
  ASSERT_EQ(io_isdir(path), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_mkdir_existing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "mkdir-exists");
  ASSERT_EQ(io_mkdir(path), 0);
  ASSERT_EQ(io_mkdir(path), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_mkdir_nested(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "parent/nested");
  ASSERT_EQ(io_mkdir(path), 0);
  ASSERT_EQ(io_isdir(path), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_mkdtmp_normal(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "mkdtmp");
  ASSERT_EQ(io_mkdtmp(path, 0), 0);
  ASSERT_EQ(io_isdir(path), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_mkdtmp_sticky(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "mkdtmp-sticky");
  ASSERT_EQ(io_mkdtmp(path, 1), 0);
  ASSERT_EQ(io_isdir(path), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_touch_create(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "touch");
  ASSERT_EQ(io_touch(path), 0);
  struct stat st;
  ASSERT_EQ(stat(path, &st), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_touch_existing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "touch-exists");
  io_touch(path);
  ASSERT_EQ(io_touch(path), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_read_small(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "read");
  int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  write(fd, "hello", 5);
  close(fd);
  buf_t b = BUF_INIT;
  ASSERT_EQ(io_read(path, &b), 0);
  ASSERT_EQ(b.len, 5);
  ASSERT_EQ(memcmp(b.data, "hello", 5), 0);
  buf_free(&b);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_unlink_existing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "unlink-existing");
  io_touch(path);
  ASSERT_EQ(io_unlink(path), 0);
  ASSERT_EQ(io_exists(path), 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_unlink_missing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "unlink-missing");
  ASSERT_EQ(io_unlink(path), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_unlink_dir(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "unlink-dir");
  ASSERT_EQ(io_mkdir(path), 0);
  ASSERT_EQ(io_unlink(path), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_rmdir_existing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "rmdir-existing");
  ASSERT_EQ(io_mkdir(path), 0);
  ASSERT_EQ(io_rmdir(path), 0);
  ASSERT_EQ(io_exists(path), 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_rmdir_missing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "rmdir-missing");
  ASSERT_EQ(io_rmdir(path), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_rmdir_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "rmdir-file");
  io_touch(path);
  ASSERT_EQ(io_rmdir(path), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_rmtree_dir(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *a = mem_path(root, "a");
  char *b = mem_path(root, "a/b");
  ASSERT_EQ(io_mkdir(a), 0);
  ASSERT_EQ(io_mkdir(b), 0);
  ASSERT_EQ(io_rmtree(a), 0);
  ASSERT_EQ(io_exists(a), 1);
  mem_free(b);
  mem_free(a);
  test_rmtree(root);
  mem_free(root);
}

static void test_chown_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "chown-file");
  io_touch(path);
  int r = io_chown(path, getuid(), getgid());
  ASSERT(r == 0 || r == -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_chown_nop(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "chown-nop");
  io_touch(path);
  ASSERT_EQ(io_chown(path, -1, -1), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_chown_missing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "chown-missing");
  ASSERT_EQ(io_chown(path, 0, 0), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_umask_effect(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "umask");
  mode_t old = umask(0);
  io_umask(0022);
  io_touch(path);
  struct stat st;
  ASSERT_EQ(stat(path, &st), 0);
  ASSERT_EQ(st.st_mode & 0777, 0644);
  io_unlink(path);
  io_umask(0077);
  io_touch(path);
  ASSERT_EQ(stat(path, &st), 0);
  ASSERT_EQ(st.st_mode & 0777, 0600);
  io_umask(old);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_umask_restore(void)
{
  mode_t original = umask(0);
  umask(original);
  mode_t before = umask(0);
  umask(before);
  io_umask(0077);
  mode_t after = umask(0);
  umask(after);
  ASSERT_EQ(after, 0077);
  io_umask(before);
}

static void test_samefs_second_fails(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "samefs-a");
  io_touch(path);
  ASSERT_EQ(io_samefs(path, "/dev/null/foo"), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_blankhead_open_fail(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent-blank");
  ASSERT_EQ(io_blankhead(path), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_read_readdir_fails(void)
{
  buf_t b = BUF_INIT;
  ASSERT_EQ(io_read("/tmp", &b), -1);
  buf_free(&b);
}

static void test_mkdir_io_exists_error(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "mkdir-err");
  io_touch(path);
  char *child = mem_path(root, "mkdir-err/child");
  ASSERT_EQ(io_mkdir(child), -1);
  mem_free(path);
  mem_free(child);
  test_rmtree(root);
  mem_free(root);
}

static void test_islink_notdir(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "islink-notdir");
  io_touch(path);
  char *child = mem_path(root, "islink-notdir/child");
  ASSERT_EQ(io_islink(child), -1);
  mem_free(path);
  mem_free(child);
  test_rmtree(root);
  mem_free(root);
}

static void test_isdir_notdir(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "isdir-notdir");
  io_touch(path);
  char *child = mem_path(root, "isdir-notdir/child");
  ASSERT_EQ(io_isdir(child), -1);
  mem_free(path);
  mem_free(child);
  test_rmtree(root);
  mem_free(root);
}

static void test_mkdir_exists_as_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "mkdir-file");
  io_touch(path);
  ASSERT_EQ(io_mkdir(path), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_mkdir_vs_io_mkdir(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *a = mem_path(root, "a");
  char *ab = mem_path(root, "a/b");
  char *cd = mem_path(root, "c/d");
  char *e = mem_path(root, "e");

  ASSERT_EQ(mkdir(ab, 0755), -1);       // parent a doesn't exist
  ASSERT_EQ(mkdir(a, 0755), 0); // parent root exists
  ASSERT_EQ(io_mkdir(cd), 0);   // creates c then c/d
  ASSERT_EQ(io_mkdir(e), 0);    // parent root exists

  ASSERT_EQ(io_isdir(a), 0);
  ASSERT_EQ(io_isdir(cd), 0);
  ASSERT_EQ(io_isdir(e), 0);

  mem_free(a);
  mem_free(ab);
  mem_free(cd);
  mem_free(e);
  test_rmtree(root);
  mem_free(root);
}

static void test_read_empty(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "read-empty");
  io_touch(path);
  buf_t b = BUF_INIT;
  ASSERT_EQ(io_read(path, &b), 0);
  ASSERT_EQ(b.len, 0);
  buf_free(&b);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_read_large(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "read-large");
  int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
  size_t sz = 65536;
  char *data = malloc(sz);
  memset(data, 'A', sz);
  ASSERT_EQ(write(fd, data, sz), (ssize_t) sz);
  close(fd);
  buf_t b = BUF_INIT;
  ASSERT_EQ(io_read(path, &b), 0);
  ASSERT_EQ(b.len, sz);
  ASSERT_EQ(memcmp(b.data, data, sz), 0);
  buf_free(&b);
  free(data);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_read_fail(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "read-fail");
  buf_t b = BUF_INIT;
  ASSERT_EQ(io_read(path, &b), -1);
  buf_free(&b);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

SUITE(io)
{
  RUN_TEST(truncate_sparse_file);
  RUN_TEST(truncate_zero_size);
  RUN_TEST(blankhead_empty_file);
  RUN_TEST(blankhead_nonzero_content);
  RUN_TEST(blankhead_larger_zeros);
  RUN_TEST(blankhead_open_fail);
  RUN_TEST(samefs_same_file);
  RUN_TEST(samefs_different_paths);
  RUN_TEST(samefs_missing);
  RUN_TEST(samefs_second_fails);
  RUN_TEST(exists_missing);
  RUN_TEST(exists_existing);
  RUN_TEST(isdir_dir);
  RUN_TEST(isdir_file);
  RUN_TEST(isdir_missing);
  RUN_TEST(isdir_notdir);
  RUN_TEST(islink_file);
  RUN_TEST(islink_symlink);
  RUN_TEST(islink_missing);
  RUN_TEST(islink_notdir);
  RUN_TEST(symlink_basic);
  RUN_TEST(symlink_existing);
  RUN_TEST(mkdir_simple);
  RUN_TEST(mkdir_existing);
  RUN_TEST(mkdir_nested);
  RUN_TEST(mkdir_exists_as_file);
  RUN_TEST(mkdir_vs_io_mkdir);
  RUN_TEST(mkdtmp_normal);
  RUN_TEST(mkdtmp_sticky);
  RUN_TEST(touch_create);
  RUN_TEST(touch_existing);
  RUN_TEST(read_small);
  RUN_TEST(read_empty);
  RUN_TEST(read_large);
  RUN_TEST(read_fail);
  RUN_TEST(read_readdir_fails);
  RUN_TEST(mkdir_io_exists_error);
  RUN_TEST(unlink_existing);
  RUN_TEST(unlink_missing);
  RUN_TEST(unlink_dir);
  RUN_TEST(rmdir_existing);
  RUN_TEST(rmdir_missing);
  RUN_TEST(rmdir_file);
  RUN_TEST(rmtree_dir);
  RUN_TEST(chown_file);
  RUN_TEST(chown_nop);
  RUN_TEST(chown_missing);
  RUN_TEST(umask_effect);
  RUN_TEST(umask_restore);
}

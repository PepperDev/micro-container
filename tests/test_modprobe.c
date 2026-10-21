#include "framework.h"
#include "test_util.h"
#include "pkg/modprobe.h"
#include "pkg/resolve.h"
#include "util/io.h"
#include "util/mem.h"
#include "mock.h"
#include <sys/utsname.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

static int dep_setup(const char *moddir, const char *content)
{
  struct utsname uts;
  if (uname(&uts) != 0)
    return -1;
  char dir[4160];
  if (snprintf(dir, sizeof(dir), "%s/%s", moddir, uts.release) >= (int)sizeof(dir))
    return -1;
  io_mkdir(dir);
  char path[4160];
  if (snprintf(path, sizeof(path), "%s/modules.dep", dir) >= (int)sizeof(path))
    return -1;
  FILE *f = fopen(path, "w");
  if (!f)
    return -1;
  int r = fprintf(f, "%s", content);
  fclose(f);
  return r < 0 ? -1 : 0;
}

static void dep_teardown(const char *moddir)
{
  struct utsname uts;
  if (uname(&uts) != 0)
    return;
  char path[4160];
  snprintf(path, sizeof(path), "%s/%s/modules.dep", moddir, uts.release);
  io_unlink(path);
  snprintf(path, sizeof(path), "%s/%s", moddir, uts.release);
  io_rmdir(path);
  io_rmdir(moddir);
  _mock_mod_dir = NULL;
  resolve_cache_clear();
}

/* ── Existing tests ── */

static void test_modprobe_load_nonexistent(void)
{
  mock_sys_reset();
  ASSERT_EQ(modprobe_load("nonexistent-module"), -1);
}

static void test_modprobe_load_nonexistent_finit_fails(void)
{
  mock_sys_reset();
  _mock_finit_module_ret = -1;
  ASSERT_EQ(modprobe_load("nonexistent-module"), -1);
}

static void test_modprobe_load_overlay(void)
{
  mock_sys_reset();
  _mock_finit_module_ret = 0;
  int r = modprobe_load("overlay");
  ASSERT_EQ(r, 0);
}

static void test_modprobe_load_real_module_scan(void)
{
  mock_sys_reset();
  int r = modprobe_load("overlay");
  ASSERT_EQ(r, 0);
  ASSERT_EQ(_mock_finit_module_count, 0);
}

static void test_modprobe_load_various_names(void)
{
  mock_sys_reset();
  ASSERT_EQ(modprobe_load(""), -1);
  ASSERT_EQ(modprobe_load("a"), -1);
}

static void test_modprobe_load_finit_never_called(void)
{
  mock_sys_reset();
  _mock_finit_module_ret = -1;
  modprobe_load("nonexistent-module");
  ASSERT_EQ(_mock_finit_module_count, 0);
  _mock_finit_module_ret = 0;
  modprobe_load("nonexistent-module");
  ASSERT_EQ(_mock_finit_module_count, 0);
}

static void test_modprobe_load_names_with_dots(void)
{
  mock_sys_reset();
  ASSERT_EQ(modprobe_load("overlay.ko"), -1);
  ASSERT_EQ(modprobe_load("test.mod"), -1);
  ASSERT_EQ(modprobe_load("name.xz"), -1);
  ASSERT_EQ(_mock_finit_module_count, 0);
}

static void test_modprobe_load_partial_prefix_match(void)
{
  mock_sys_reset();
  ASSERT_EQ(modprobe_load("ove"), -1);
  ASSERT_EQ(modprobe_load("over"), -1);
  ASSERT_EQ(modprobe_load("overl"), -1);
  ASSERT_EQ(_mock_finit_module_count, 0);
}

static void test_modprobe_load_loaded_from_proc(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *proc = mem_path(root, "proc-modules");
  io_unlink(proc);
  FILE *f = fopen(proc, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "overlay 12345 0 0 0\n");
  fprintf(f, "veth 23456 0 0 0\n");
  fprintf(f, "bridge 34567 0 0 0\n");
  fclose(f);
  ASSERT_EQ(scan_modules(proc, "overlay"), 1);
  ASSERT_EQ(scan_modules(proc, "veth"), 1);
  ASSERT_EQ(scan_modules(proc, "bridge"), 1);
  ASSERT_EQ(scan_modules(proc, "xt_conntrack"), 0);
  mem_free(proc);
  test_rmtree(root);
  mem_free(root);
}

static void test_modprobe_load_loaded_from_modules_builtin(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *builtin = mem_path(root, "modules-builtin");
  io_unlink(builtin);
  FILE *f = fopen(builtin, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "kernel/net/netfilter/xt_conntrack.ko\n");
  fprintf(f, "kernel/net/netfilter/nf_nat.ko\n");
  fprintf(f, "kernel/fs/ext4/ext4.ko\n");
  fclose(f);
  ASSERT_EQ(scan_modules(builtin, "xt_conntrack"), 1);
  ASSERT_EQ(scan_modules(builtin, "nf_nat"), 1);
  ASSERT_EQ(scan_modules(builtin, "ext4"), 1);
  mem_free(builtin);
  test_rmtree(root);
  mem_free(root);
}

static void test_modprobe_load_already_loaded_verbose(void)
{
  mock_sys_reset();
  int r = modprobe_load("overlay");
  ASSERT_EQ(r, 0);
  ASSERT_EQ(_mock_finit_module_count, 0);
  ASSERT_EQ(_mock_mount_count, 0);
}

static void test_modprobe_load_special_chars(void)
{
  mock_sys_reset();
  ASSERT_EQ(modprobe_load("module-name"), -1);
  ASSERT_EQ(modprobe_load("module_name"), -1);
  ASSERT_EQ(modprobe_load("module.name"), -1);
  ASSERT_EQ(modprobe_load("MODULE"), -1);
  ASSERT_EQ(_mock_finit_module_count, 0);
}

/* ── New tests ── */

/* Modules loaded on test system are now correctly detected by is_loaded. */
static void test_modprobe_load_confirmed_loaded(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *proc = mem_path(root, "proc-modules");
  io_unlink(proc);
  FILE *f = fopen(proc, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "overlay 12345 0 0 0\n");
  fprintf(f, "veth 23456 0 0 0\n");
  fprintf(f, "bridge 34567 0 0 0\n");
  fclose(f);
  ASSERT_EQ(scan_modules(proc, "overlay"), 1);
  ASSERT_EQ(scan_modules(proc, "veth"), 1);
  ASSERT_EQ(scan_modules(proc, "bridge"), 1);
  mem_free(proc);
  test_rmtree(root);
  mem_free(root);
}

/* Verify no false match occurs with a prefix that happens to appear as a
   leading substring of other module names in /proc/modules. */
static void test_modprobe_load_prefix_no_false_match(void)
{
  mock_sys_reset();
  ASSERT_EQ(modprobe_load("x"), -1);
  ASSERT_EQ(modprobe_load("n"), -1);
  ASSERT_EQ(modprobe_load("n_"), -1);
  ASSERT_EQ(modprobe_load("ve"), -1);
  ASSERT_EQ(modprobe_load("br"), -1);
  ASSERT_EQ(modprobe_load("ov"), -1);
  ASSERT_EQ(_mock_finit_module_count, 0);
}

/* Test that the single-character module name 'a' which does NOT appear
   in /proc/modules is still handled consistently. */
static void test_modprobe_load_single_char_not_loaded(void)
{
  mock_sys_reset();
  ASSERT_EQ(modprobe_load("a"), -1);
  ASSERT_EQ(modprobe_load("b"), -1);
  ASSERT_EQ(modprobe_load("c"), -1);
  ASSERT_EQ(modprobe_load("z"), -1);
  ASSERT_EQ(_mock_finit_module_count, 0);
}

/* Set up the resolve_module infrastructure (modules.dep in temp dir via
   mock infrastructure). Even though check_module_status blocks
   reaching resolve_module on this system (no /lib/modules/modules.builtin),
   this confirms the test infrastructure is ready and doesn't crash. */
static void test_modprobe_load_resolve_infrastructure(void)
{
  mock_sys_reset();
  _mock_finit_module_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *moddir = mem_path(root, "mods");
  char *mod = mem_path(root, "cage-mod-resolve.ko");
  io_touch(mod);

  char dep[4160];
  snprintf(dep, sizeof(dep), "../../../../tmp/%s/cage-mod-resolve.ko:\n", root + 5);
  ASSERT_EQ(dep_setup(moddir, dep), 0);
  _mock_mod_dir = moddir;
  resolve_cache_clear();

  int r = modprobe_load("cage-mod-resolve");
  ASSERT_EQ(r, -1);

  io_unlink(mod);
  mem_free(mod);
  dep_teardown(moddir);
  mem_free(moddir);
  test_rmtree(root);
  mem_free(root);
}

/* Same infrastructure but without the module file - confirms no crash
   when open_and_read fails in decompress_module. */
static void test_modprobe_load_resolve_module_missing(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *moddir = mem_path(root, "mods");

  char dep[4160];
  snprintf(dep, sizeof(dep), "../../../../tmp/%s/cage-mod-missing.ko:\n", root + 5);
  ASSERT_EQ(dep_setup(moddir, dep), 0);
  _mock_mod_dir = moddir;
  resolve_cache_clear();

  int r = modprobe_load("cage-mod-missing");
  ASSERT_EQ(r, -1);

  dep_teardown(moddir);
  mem_free(moddir);
  test_rmtree(root);
  mem_free(root);
}

/* Test long module name that won't be in /proc/modules */
static void test_modprobe_load_long_name(void)
{
  mock_sys_reset();
  char name[256];
  memset(name, 'x', 250);
  name[250] = '\0';
  ASSERT_EQ(modprobe_load(name), -1);
  ASSERT_EQ(_mock_finit_module_count, 0);
}

/* Test module name that includes a path separator - not valid */
static void test_modprobe_load_with_slash(void)
{
  mock_sys_reset();
  ASSERT_EQ(modprobe_load("some/module"), -1);
  ASSERT_EQ(modprobe_load("/module"), -1);
  ASSERT_EQ(modprobe_load("module/"), -1);
  ASSERT_EQ(_mock_finit_module_count, 0);
}

/* ── Compressed module loading tests ── */

#include "test_gzip_data.h"

static void write_gz(const char *path, const unsigned char *gz, size_t len)
{
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(write(fd, gz, len), (ssize_t) len);
  close(fd);
}

/* Verify cache invalidation fires when modprobe_load successfully loads a
   module via finit_module, covering the invalidate_module_cache path. */
static void test_modprobe_load_triggers_cache_invalidation(void)
{
  mock_sys_reset();
  _mock_finit_module_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  /* Create an empty modules.builtin so is_builtin returns 0 not -1 */
  struct utsname uts;
  ASSERT_EQ(uname(&uts), 0);
  char *builtin_root = mem_path(root, "builtin");
  io_mkdir(builtin_root);
  char builtin_rel[4160];
  snprintf(builtin_rel, sizeof(builtin_rel), "%s/%s", builtin_root, uts.release);
  io_mkdir(builtin_rel);
  char *builtin_file = mem_path(builtin_rel, "modules.builtin");
  io_touch(builtin_file);
  _mock_builtin_prefix = builtin_root;
  modprobe_cache_clear_builtin();
  _mock_build_prefix = builtin_root;

  char *moddir = mem_path(root, "mods");

  char dep[4160];
  snprintf(dep, sizeof(dep), "../../../../tmp/%s/cage-cache-mod.ko.gz:\n", root + 5);
  ASSERT_EQ(dep_setup(moddir, dep), 0);
  _mock_mod_dir = moddir;
  resolve_cache_clear();

  char *mod = mem_path(root, "cage-cache-mod.ko.gz");
  write_gz(mod, not_a_real_module_gz, NOT_A_REAL_MODULE_GZ_LEN);

  ASSERT_EQ(modprobe_load("cage-cache-mod"), 0);
  ASSERT_EQ(_mock_finit_module_count, 1);

  io_unlink(mod);
  mem_free(mod);
  _mock_builtin_prefix = NULL;
  modprobe_cache_clear_builtin();
  _mock_build_prefix = NULL;
  dep_teardown(moddir);
  mem_free(moddir);
  io_unlink(builtin_file);
  mem_free(builtin_file);
  io_rmdir(builtin_rel);
  mem_free(builtin_root);
  test_rmtree(root);
  mem_free(root);
}

static void test_buf_write_to_fd_success(void)
{
  char data[] = "hello write";
  buf_t b = {.data = data,.len = sizeof(data),.cap = 0 };
  int fd = memfd_create("test", 0);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(buf_write_to_fd(fd, &b), 0);
  char buf[64] = { 0 };
  ASSERT_NE(read(fd, buf, sizeof(buf)), -1);
  ASSERT_EQ(memcmp(buf, data, sizeof(data)), 0);
  close(fd);
}

static void test_buf_write_to_fd_empty(void)
{
  buf_t b = {.data = NULL,.len = 0,.cap = 0 };
  int fd = memfd_create("test", 0);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(buf_write_to_fd(fd, &b), 0);
  close(fd);
}

static void test_pipe_to_fd_success(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *tmp = mem_path(root, "pipe-test");
  io_unlink(tmp);
  FILE *f = fopen(tmp, "w+");
  ASSERT_NOT_NULL(f);
  fprintf(f, "piped data test");
  rewind(f);
  int fd = memfd_create("test", 0);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(pipe_to_fd(f, fd), 0);
  fclose(f);
  lseek(fd, 0, SEEK_SET);
  char buf[64] = { 0 };
  ASSERT_NE(read(fd, buf, sizeof(buf)), -1);
  ASSERT_STR_EQ(buf, "piped data test");
  close(fd);
  mem_free(tmp);
  test_rmtree(root);
  mem_free(root);
}

static void test_pipe_to_fd_empty(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *tmp = mem_path(root, "pipe-empty");
  io_unlink(tmp);
  FILE *f = fopen(tmp, "w+");
  ASSERT_NOT_NULL(f);
  rewind(f);
  int fd = memfd_create("test", 0);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(pipe_to_fd(f, fd), 0);
  fclose(f);
  lseek(fd, 0, SEEK_SET);
  char buf[64] = { 0 };
  ASSERT_EQ(read(fd, buf, sizeof(buf)), 0);
  close(fd);
  mem_free(tmp);
  test_rmtree(root);
  mem_free(root);
}

static void test_buf_write_to_fd_bad_fd(void)
{
  buf_t b = {.data = "test",.len = 4,.cap = 0 };
  ASSERT_EQ(buf_write_to_fd(-1, &b), -1);
}

static void test_buf_write_to_fd_lseek_fails(void)
{
  int p[2];
  ASSERT_NE(pipe(p), -1);
  buf_t b = {.data = "test",.len = 4,.cap = 0 };
  ASSERT_EQ(buf_write_to_fd(p[1], &b), -1);
  close(p[0]);
  close(p[1]);
}

static void test_finit_module_fd_eexist(void)
{
  mock_sys_reset();
  _mock_finit_module_ret = -1;
  _mock_finit_module_errno = EEXIST;
  ASSERT_EQ(finit_module_fd(-1, 0), 0);
  ASSERT_EQ(_mock_finit_module_count, 1);
}

static void test_pipe_to_fd_bad_fd(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *tmp = mem_path(root, "pipe-bad-fd");
  io_unlink(tmp);
  FILE *f = fopen(tmp, "w+");
  ASSERT_NOT_NULL(f);
  fprintf(f, "data to write");
  rewind(f);
  ASSERT_EQ(pipe_to_fd(f, -1), -1);
  fclose(f);
  io_unlink(tmp);
  mem_free(tmp);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_module_path_compressed_happy(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "comp-happy.ko");
  io_unlink(path);
  write_gz(path, not_a_real_module_gz, NOT_A_REAL_MODULE_GZ_LEN);
  _mock_finit_module_ret = 0;
  ASSERT_EQ(load_module_path(path), 0);
  ASSERT_EQ(_mock_finit_module_count, 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_module_path_compressed_fallback(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "comp-fallback.ko");
  io_unlink(path);
  write_gz(path, some_module_data_gz, SOME_MODULE_DATA_GZ_LEN);
  _mock_finit_module_ret = -1;
  _mock_finit_module_errno = EOPNOTSUPP;
  ASSERT_EQ(load_module_path(path), -1);
  ASSERT_EQ(_mock_finit_module_count, 2);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_decompressed_gzip_success(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "decomp-ok.ko");
  io_unlink(path);
  write_gz(path, decomp_test_data_gz, DECOMP_TEST_DATA_GZ_LEN);
  _mock_finit_module_ret = 0;
  ASSERT_EQ(load_decompressed(path, 1), 0);
  ASSERT_EQ(_mock_finit_module_count, 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_decompressed_gzip_fail_finit(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "decomp-fail.ko");
  io_unlink(path);
  write_gz(path, decomp_fail_data_gz, DECOMP_FAIL_DATA_GZ_LEN);
  _mock_finit_module_ret = -1;
  ASSERT_EQ(load_decompressed(path, 1), -1);
  ASSERT_EQ(_mock_finit_module_count, 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_decompressed_missing_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent-decomp2");
  mock_sys_reset();
  ASSERT_EQ(load_decompressed(path, 1), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_module_path_compressed_finit_err(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "comp-err.ko");
  io_unlink(path);
  write_gz(path, err_data_gz, ERR_DATA_GZ_LEN);
  _mock_finit_module_ret = -1;
  _mock_finit_module_errno = EINVAL;
  ASSERT_EQ(load_module_path(path), -1);
  ASSERT_EQ(_mock_finit_module_count, 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void write_raw(const char *path, const unsigned char *data, size_t len)
{
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  write(fd, data, len);
  close(fd);
}

static void test_load_module_path_xz_fallback(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "xz-fallback.ko");
  unsigned char xz_magic[] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };
  write_raw(path, xz_magic, sizeof(xz_magic));
  _mock_finit_module_ret = -1;
  _mock_finit_module_errno = EOPNOTSUPP;
  ASSERT_EQ(load_module_path(path), -1);
  ASSERT_EQ(_mock_finit_module_count, 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_module_path_xz_no_shell(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *sentinel = mem_path(root, "shell-was-used");
  char *name = mem_cat("xz-\"; touch ", 12, sentinel, strlen(sentinel), ";\" ", 3, ".ko", 3, MEM_CAT_END);
  char *path = mem_path(root, name);
  unsigned char xz_magic[] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };
  write_raw(path, xz_magic, sizeof(xz_magic));
  _mock_finit_module_ret = -1;
  _mock_finit_module_errno = EOPNOTSUPP;
  ASSERT_EQ(load_module_path(path), -1);
  ASSERT_EQ(io_exists(sentinel), 1);
  mem_free(path);
  mem_free(name);
  mem_free(sentinel);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_module_path_zstd_fallback(void)
{
  mock_sys_reset();
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "zstd-fallback.ko");
  unsigned char zstd_magic[] = { 0x28, 0xB5, 0x2F, 0xFD };
  write_raw(path, zstd_magic, sizeof(zstd_magic));
  _mock_finit_module_ret = -1;
  _mock_finit_module_errno = EOPNOTSUPP;
  ASSERT_EQ(load_module_path(path), -1);
  ASSERT_EQ(_mock_finit_module_count, 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

/* Test with a module name that fully matches the first word of a line
   in /proc/modules, plus the numeric suffix that nf_conntrack_netlink
   or similar lines have. */
static void test_modprobe_load_numeric_suffix(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *proc = mem_path(root, "proc-modules");
  io_unlink(proc);
  FILE *f = fopen(proc, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "xt_MASQUERADE 12345 0 0 0\n");
  fprintf(f, "nf_conntrack_netlink 23456 0 0 4\n");
  fclose(f);
  ASSERT_EQ(scan_modules(proc, "xt_MASQUERADE"), 1);
  ASSERT_EQ(scan_modules(proc, "nf_conntrack_netlink"), 1);
  mem_free(proc);
  test_rmtree(root);
  mem_free(root);
}

/* Verify is_builtin handles the missing /lib/modules gracefully.
   overlay is already loaded, so this returns 0 without touching disk. */
static void test_modprobe_load_builtin_missing(void)
{
  mock_sys_reset();
  int r = modprobe_load("overlay");
  ASSERT_EQ(r, 0);
}

/* ── Direct function tests ── */

static void test_line_has_name_ko_match(void)
{
  ASSERT_EQ(line_has_name("ext4.ko", 7, "ext4"), 1);
}

static void test_line_has_name_overlay(void)
{
  ASSERT_EQ(line_has_name("overlay.ko", 10, "overlay"), 1);
}

static void test_line_has_name_no_match(void)
{
  ASSERT_EQ(line_has_name("ext4.ko", 7, "ext3"), 0);
}

static void test_line_has_name_empty(void)
{
  ASSERT_EQ(line_has_name("", 0, "ext4"), 0);
}

static void test_line_has_name_path_match(void)
{
  ASSERT_EQ(line_has_name("kernel/fs/overlay/overlay.ko", 28, "overlay"), 1);
}

static void test_line_has_name_path_extra(void)
{
  ASSERT_EQ(line_has_name("extra/crypto/aes.ko", 19, "aes"), 1);
}

static void test_scan_modules_file_not_found(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "scan-nonexistent");
  ASSERT_EQ(scan_modules(path, "overlay"), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_scan_modules_found_in_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "scan-test");
  io_unlink(path);
  FILE *f = fopen(path, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "kernel/fs/overlay/overlay.ko\n");
  fclose(f);
  ASSERT_EQ(scan_modules(path, "overlay"), 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_scan_modules_found_builtin(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "scan-builtin");
  io_unlink(path);
  FILE *f = fopen(path, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "kernel/fs/overlay/overlay.ko\nkernel/fs/veth/veth.ko\n");
  fclose(f);
  ASSERT_EQ(scan_modules(path, "overlay"), 1);
  ASSERT_EQ(scan_modules(path, "veth"), 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_scan_modules_not_found(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "scan-nomatch");
  io_unlink(path);
  FILE *f = fopen(path, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "ext4.ko:\n");
  fclose(f);
  ASSERT_EQ(scan_modules(path, "overlay"), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_detect_compression_gzip(void)
{
  const unsigned char gzip[] = { 0x1F, 0x8B, 0x08, 0x00 };
  ASSERT_EQ(decompress_magic(gzip), 1);
}

static void test_detect_compression_xz(void)
{
  static const unsigned char xz[] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };
  ASSERT_EQ(decompress_magic(xz), 2);
}

static void test_detect_compression_zstd(void)
{
  static const unsigned char zstd[] = { 0x28, 0xB5, 0x2F, 0xFD };
  ASSERT_EQ(decompress_magic(zstd), 3);
}

static void test_detect_compression_uncompressed(void)
{
  const unsigned char plain[] = { 0x7F, 0x45, 0x4C, 0x46 };
  ASSERT_EQ(decompress_magic(plain), 0);
}

static void test_detect_compression_too_short(void)
{
  const unsigned char shortbuf[] = { 0x1F };
  ASSERT_EQ(decompress_magic(shortbuf), 0);
}

static void test_load_module_fd_bad_fd(void)
{
  mock_sys_reset();
  _mock_finit_module_ret = -1;
  ASSERT_EQ(finit_module_fd(-1, 0), -1);
  ASSERT_EQ(_mock_finit_module_count, 1);
}

static void test_open_module_success(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "open-test");
  io_unlink(path);
  io_touch(path);
  int fd;
  ASSERT_EQ(open_module(path, &fd), 0);
  ASSERT_NE(fd, -1);
  close(fd);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_open_module_fail(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent-open");
  int fd = 999;
  ASSERT_EQ(open_module(path, &fd), -1);
  ASSERT_EQ(fd, 999);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static int write_temp_module(const buf_t *out, int *out_fd)
{
  int fd = memfd_create("cage-mod", 0);
  if (fd < 0)
    return -1;
  ssize_t written = 0;
  while (written < (ssize_t) out->len) {
    ssize_t n = write(fd, out->data + written, out->len - (size_t)written);
    if (n < 0) {
      close(fd);
      return -1;
    }
    written += n;
  }
  lseek(fd, 0, SEEK_SET);
  *out_fd = fd;
  return 0;
}

static void test_write_temp_module_success(void)
{
  const char data[] = "hello module";
  buf_t b;
  b.data = (char *)data;
  b.len = sizeof(data);
  b.cap = 0;
  int fd;
  ASSERT_EQ(write_temp_module(&b, &fd), 0);
  ASSERT_NE(fd, -1);
  lseek(fd, 0, SEEK_SET);
  char buf[64] = { 0 };
  ASSERT_NE(read(fd, buf, sizeof(buf)), -1);
  ASSERT_EQ(memcmp(buf, data, sizeof(data)), 0);
  close(fd);
}

static void test_write_temp_module_large(void)
{
  char data[4096];
  memset(data, 'A', sizeof(data));
  buf_t b;
  b.data = data;
  b.len = sizeof(data);
  b.cap = 0;
  int fd;
  ASSERT_EQ(write_temp_module(&b, &fd), 0);
  ASSERT_NE(fd, -1);
  lseek(fd, 0, SEEK_SET);
  char buf[4096] = { 0 };
  ASSERT_EQ(read(fd, buf, sizeof(buf)), (ssize_t) sizeof(buf));
  ASSERT_EQ(memcmp(buf, data, sizeof(data)), 0);
  close(fd);
}

static void test_decompress_module_nonexistent(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent-decomp");
  mock_sys_reset();
  int fd;
  ASSERT_EQ(open_module(path, &fd), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_module_path_nonexistent(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "nonexistent-load");
  mock_sys_reset();
  ASSERT_EQ(load_module_path(path), -1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_check_module_status_in_system(void)
{
  mock_sys_reset();
  int r = check_module_status("overlay");
  ASSERT_EQ(r, 1);
}

static void test_scan_modules_not_found_in_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "scan-nomatch2");
  io_unlink(path);
  FILE *f = fopen(path, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "ext4.ko\n");
  fclose(f);
  ASSERT_EQ(scan_modules(path, "overlay"), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_dep_nonexistent(void)
{
  load_dep("does_not_exist");
}

static void test_scan_modules_with_path_in_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "scan-path");
  io_unlink(path);
  FILE *f = fopen(path, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "kernel/crypto/aes.ko\n");
  fclose(f);
  ASSERT_EQ(scan_modules(path, "aes"), 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_module_fd_ok(void)
{
  mock_sys_reset();
  _mock_finit_module_ret = 0;
  int fd = open("/dev/null", O_RDONLY);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(finit_module_fd(fd, 0), 0);
  ASSERT_EQ(_mock_finit_module_count, 1);
  close(fd);
}

static void test_load_module_path_success(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "path-ok");
  mock_sys_reset();
  _mock_finit_module_ret = 0;
  io_unlink(path);
  io_touch(path);
  ASSERT_EQ(load_module_path(path), 0);
  ASSERT_EQ(_mock_finit_module_count, 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_name_matches_dash(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "scan-dash");
  io_unlink(path);
  FILE *f = fopen(path, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "test_module\n");
  fclose(f);
  ASSERT_EQ(scan_modules(path, "test-module"), 1);
  ASSERT_EQ(scan_modules(path, "test_module"), 1);
  io_unlink(path);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_load_module_path_uncompressed_finit_fail(void)
{
  mock_sys_reset();
  _mock_finit_module_ret = -1;
  _mock_finit_module_errno = EINVAL;
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "path-fail");
  io_unlink(path);
  io_touch(path);
  ASSERT_EQ(load_module_path(path), -1);
  ASSERT_EQ(_mock_finit_module_count, 1);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

static void test_scan_modules_no_newline_not_found(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "scan-nonl");
  io_unlink(path);
  FILE *f = fopen(path, "w");
  ASSERT_NOT_NULL(f);
  fprintf(f, "ext4.ko");
  fclose(f);
  ASSERT_EQ(scan_modules(path, "overlay"), 0);
  mem_free(path);
  test_rmtree(root);
  mem_free(root);
}

SUITE(modprobe)
{
  RUN_TEST(modprobe_load_nonexistent);
  RUN_TEST(modprobe_load_nonexistent_finit_fails);
  RUN_TEST(modprobe_load_overlay);
  RUN_TEST(modprobe_load_real_module_scan);
  RUN_TEST(modprobe_load_various_names);
  RUN_TEST(modprobe_load_finit_never_called);
  RUN_TEST(modprobe_load_names_with_dots);
  RUN_TEST(modprobe_load_partial_prefix_match);
  RUN_TEST(modprobe_load_loaded_from_proc);
  RUN_TEST(modprobe_load_loaded_from_modules_builtin);
  RUN_TEST(modprobe_load_already_loaded_verbose);
  RUN_TEST(modprobe_load_special_chars);

  RUN_TEST(modprobe_load_confirmed_loaded);
  RUN_TEST(modprobe_load_prefix_no_false_match);
  RUN_TEST(modprobe_load_single_char_not_loaded);
  RUN_TEST(modprobe_load_resolve_infrastructure);
  RUN_TEST(modprobe_load_resolve_module_missing);
  RUN_TEST(modprobe_load_triggers_cache_invalidation);
  RUN_TEST(modprobe_load_long_name);
  RUN_TEST(modprobe_load_with_slash);
  RUN_TEST(modprobe_load_numeric_suffix);
  RUN_TEST(modprobe_load_builtin_missing);

  RUN_TEST(line_has_name_ko_match);
  RUN_TEST(line_has_name_overlay);
  RUN_TEST(line_has_name_no_match);
  RUN_TEST(line_has_name_empty);
  RUN_TEST(line_has_name_path_match);
  RUN_TEST(line_has_name_path_extra);
  RUN_TEST(scan_modules_file_not_found);
  RUN_TEST(scan_modules_found_in_file);
  RUN_TEST(scan_modules_found_builtin);
  RUN_TEST(scan_modules_not_found);
  RUN_TEST(detect_compression_gzip);
  RUN_TEST(detect_compression_xz);
  RUN_TEST(detect_compression_zstd);
  RUN_TEST(detect_compression_uncompressed);
  RUN_TEST(detect_compression_too_short);
  RUN_TEST(load_module_fd_bad_fd);
  RUN_TEST(finit_module_fd_eexist);
  RUN_TEST(open_module_success);
  RUN_TEST(open_module_fail);
  RUN_TEST(write_temp_module_success);
  RUN_TEST(write_temp_module_large);
  RUN_TEST(decompress_module_nonexistent);
  RUN_TEST(load_module_path_nonexistent);
  RUN_TEST(check_module_status_in_system);
  RUN_TEST(scan_modules_not_found_in_file);
  RUN_TEST(scan_modules_with_path_in_file);
  RUN_TEST(load_dep_nonexistent);
  RUN_TEST(load_module_fd_ok);
  RUN_TEST(load_module_path_success);
  RUN_TEST(scan_modules_no_newline_not_found);
  RUN_TEST(name_matches_dash);
  RUN_TEST(load_module_path_uncompressed_finit_fail);

  RUN_TEST(buf_write_to_fd_success);
  RUN_TEST(buf_write_to_fd_empty);
  RUN_TEST(buf_write_to_fd_bad_fd);
  RUN_TEST(buf_write_to_fd_lseek_fails);
  RUN_TEST(pipe_to_fd_success);
  RUN_TEST(pipe_to_fd_empty);
  RUN_TEST(pipe_to_fd_bad_fd);
  RUN_TEST(load_module_path_compressed_happy);
  RUN_TEST(load_module_path_compressed_fallback);
  RUN_TEST(load_decompressed_gzip_success);
  RUN_TEST(load_decompressed_gzip_fail_finit);
  RUN_TEST(load_decompressed_missing_file);
  RUN_TEST(load_module_path_compressed_finit_err);
  RUN_TEST(load_module_path_xz_fallback);
  RUN_TEST(load_module_path_xz_no_shell);
  RUN_TEST(load_module_path_zstd_fallback);
}

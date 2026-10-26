#include "framework.h"
#include "test_util.h"
#include "sandbox/setup.h"
#include "sandbox/cgroup.h"
#include "cli/args.h"
#include "util/io.h"
#include "util/mem.h"
#include "mock.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>

extern int _mock_mount_fail_after;

static cage_config_t default_cfg = {
  .unshare_net = 1,
  .unshare_cgroup = 1,
};

static void test_mount_misc_basic(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  mem_free(run);

  ASSERT_EQ(mount_misc(root, &default_cfg), 0);

  char *etc = mem_path(root, "etc");
  ASSERT_EQ(io_isdir(etc), 0);
  mem_free(etc);

  io_rmtree(root);
  mem_free(root);
}

static void test_mount_misc_resolv_mount_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(mount_misc(root, &default_cfg), -1);

  io_rmtree(root);
  mem_free(root);
}

static void test_setup_resolv_mkdir_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *etc = mem_path(root, "etc");
  io_touch(etc);
  mem_free(etc);

  ASSERT_EQ(mount_misc(root, &default_cfg), -1);

  io_rmtree(root);
  mem_free(root);
}

static void test_setup_resolv_touch_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *etc = mem_path(root, "etc");
  io_mkdir(etc);
  char *resolv = mem_path(root, "etc/resolv.conf");
  mkdir(resolv, 0755);
  mem_free(resolv);
  mem_free(etc);

  ASSERT_EQ(mount_misc(root, &default_cfg), -1);

  io_rmtree(root);
  mem_free(root);
}

static void test_setup_run_dirs_lock_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  char *lock = mem_path(root, "run/lock");
  io_touch(lock);
  mem_free(lock);
  mem_free(run);

  ASSERT_EQ(mount_misc(root, &default_cfg), -1);

  io_rmtree(root);
  mem_free(root);
}

static void test_setup_run_dirs_user_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  char *lock = mem_path(root, "run/lock");
  io_mkdir(lock);
  mem_free(lock);
  char *user = mem_path(root, "run/user");
  io_touch(user);
  mem_free(user);
  mem_free(run);

  ASSERT_EQ(mount_misc(root, &default_cfg), -1);

  io_rmtree(root);
  mem_free(root);
}

static void test_devshm_blocked(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  char *run_shm = mem_path(root, "run/shm");
  io_touch(run_shm);
  mem_free(run_shm);
  char *dev = mem_path(root, "dev");
  io_mkdir(dev);
  char *dev_shm = mem_path(root, "dev/shm");
  io_touch(dev_shm);
  mem_free(dev_shm);
  mem_free(dev);
  mem_free(run);

  ASSERT_EQ(mount_misc(root, &default_cfg), -1);

  io_rmtree(root);
  mem_free(root);
}

static void test_setup_devshm_nonexistent(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  mem_free(run);

  ASSERT_EQ(mount_misc(root, &default_cfg), 0);

  char *shm = mem_path(root, "run/shm");
  ASSERT_EQ(io_isdir(shm), 0);
  mem_free(shm);

  io_rmtree(root);
  mem_free(root);
}

static void test_mount_misc_resolv_mount_early_fail(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_mount_fail_after = 1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(mount_misc(root, &default_cfg), -1);
  ASSERT_EQ(_mock_mount_count, 1);

  io_rmtree(root);
  mem_free(root);
}

/* Full flow with generous fail_after — all mounts must succeed.
   Mount order: 1=resolv.conf bind, 2=dev/shm tmpfs.
   Uses unshare_net=0 for deterministic mount count (no host DNS dependency). */
static void test_mount_misc_cgroup_fail_after_high(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_mount_fail_after = 3;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  mem_free(run);

  cage_config_t cfg = default_cfg;
  cfg.unshare_net = 0;
  ASSERT_EQ(mount_misc(root, &cfg), 0);
  ASSERT_EQ(_mock_mount_count, 2);

  io_rmtree(root);
  mem_free(root);
}

/* Verify that when /run/shm exists as a file, mount_misc
   fails (io_symlink cannot overwrite an existing path). */
static void test_setup_devshm_run_shm_blocked_by_file(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  mem_free(run);
  char *run_shm = mem_path(root, "run/shm");
  io_touch(run_shm);
  mem_free(run_shm);

  ASSERT_EQ(mount_misc(root, &default_cfg), -1);

  io_rmtree(root);
  mem_free(root);
}

/* mount_cgroup_v2 with SYS_MOUNT returning -1 for cgroup2 mount
   (all mounts fail). The first mount (resolv) fails immediately. */
static void test_mount_misc_all_mounts_fail(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(mount_misc(root, &default_cfg), -1);
  ASSERT_EQ(_mock_mount_count, 1);

  io_rmtree(root);
  mem_free(root);
}

/* Verify the full mount_misc flow creates the expected directory structure
   under root: etc/, etc/resolv.conf, run/, run/shm/, run/lock/, run/user/. */
static void test_mount_misc_verify_directory_structure(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  mem_free(run);

  ASSERT_EQ(mount_misc(root, &default_cfg), 0);

  char *etc = mem_path(root, "etc");
  ASSERT_EQ(io_isdir(etc), 0);
  char *resolv = mem_path(root, "etc/resolv.conf");
  ASSERT_EQ(io_exists(resolv), 0);
  char *run_shm = mem_path(root, "run/shm");
  ASSERT_EQ(io_isdir(run_shm), 0);
  char *run_lock = mem_path(root, "run/lock");
  ASSERT_EQ(io_isdir(run_lock), 0);
  char *run_user = mem_path(root, "run/user");
  ASSERT_EQ(io_isdir(run_user), 0);

  mem_free(run_user);
  mem_free(run_lock);
  mem_free(run_shm);
  mem_free(resolv);
  mem_free(etc);

  io_rmtree(root);
  mem_free(root);
}

/* mount_misc with run/lock stuck as file and run/user also blocked.
   Tests that the first failure (lock) correctly stops execution. */
static void test_setup_run_dirs_both_fail(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  char *lock = mem_path(root, "run/lock");
  io_touch(lock);
  mem_free(lock);
  char *user = mem_path(root, "run/user");
  io_touch(user);
  mem_free(user);
  mem_free(run);

  ASSERT_EQ(mount_misc(root, &default_cfg), -1);

  io_rmtree(root);
  mem_free(root);
}

/* Verify the mount count for the full successful path.
   Mounts: 1=resolv.conf bind, 2=dev/shm tmpfs.
   Uses unshare_net=0 for deterministic mount count. */
static void test_mount_misc_mount_count(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  mem_free(run);

  cage_config_t cfg = default_cfg;
  cfg.unshare_net = 0;
  ASSERT_EQ(mount_misc(root, &cfg), 0);
  ASSERT_EQ(_mock_mount_count, 2);

  io_rmtree(root);
  mem_free(root);
}

/* ── Direct function tests ── */

static void test_setup_resolv_direct(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(setup_resolv(root, &default_cfg), 0);

  char *etc = mem_path(root, "etc");
  ASSERT_EQ(io_isdir(etc), 0);
  mem_free(etc);
  io_rmtree(root);
  mem_free(root);
}

static void test_setup_resolv_default_dns_bind(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_needs_default_dns = 1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(setup_resolv(root, &default_cfg), 0);
  ASSERT_EQ(_mock_mount_count, 1);
  ASSERT_EQ(_mock_umount_count, 0);

  char *etc = mem_path(root, "etc");
  ASSERT_EQ(io_isdir(etc), 0);
  mem_free(etc);
  io_rmtree(root);
  mem_free(root);
  _mock_needs_default_dns = -1;
}

static void test_setup_resolv_default_dns_mount_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;
  _mock_needs_default_dns = 1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(setup_resolv(root, &default_cfg), -1);
  ASSERT_EQ(_mock_mount_count, 1);

  io_rmtree(root);
  mem_free(root);
  _mock_needs_default_dns = -1;
}

static void test_setup_resolv_oversized_rejected(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *etc = mem_path(root, "etc");
  ASSERT_EQ(io_mkdir(etc), 0);
  char *resolv = mem_path(root, "etc/resolv.conf");
  FILE *f = fopen(resolv, "w");
  ASSERT_NOT_NULL(f);
  for (int i = 0; i < 70000; i++)
    fputc('x', f);
  fclose(f);

  ASSERT_EQ(setup_resolv(root, &default_cfg), -1);
  ASSERT_EQ(_mock_mount_count, 0);

  mem_free(resolv);
  mem_free(etc);
  io_rmtree(root);
  mem_free(root);
}

static void test_setup_resolv_dangling_symlink(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *etc = mem_path(root, "etc");
  ASSERT_EQ(io_mkdir(etc), 0);
  char *resolv = mem_path(root, "etc/resolv.conf");
  ASSERT_EQ(symlink("missing-cage-resolv", resolv), 0);
  mem_free(etc);

  ASSERT_EQ(setup_resolv(root, &default_cfg), 0);

  struct stat st;
  ASSERT_EQ(lstat(resolv, &st), 0);
  ASSERT(S_ISREG(st.st_mode));

  mem_free(resolv);
  io_rmtree(root);
  mem_free(root);
}

static void test_setup_resolv_valid_symlink_replaced(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *etc = mem_path(root, "etc");
  ASSERT_EQ(io_mkdir(etc), 0);
  char *target = mem_path(etc, "real-resolv");
  ASSERT_EQ(io_touch(target), 0);
  char *resolv = mem_path(root, "etc/resolv.conf");
  ASSERT_EQ(symlink("real-resolv", resolv), 0);

  ASSERT_EQ(setup_resolv(root, &default_cfg), 0);

  struct stat st;
  ASSERT_EQ(lstat(resolv, &st), 0);
  ASSERT(S_ISREG(st.st_mode));
  ASSERT_EQ(io_exists(target), 0);

  mem_free(resolv);
  mem_free(target);
  mem_free(etc);
  io_rmtree(root);
  mem_free(root);
}

static void test_setup_run_dirs_direct(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  mem_free(run);

  ASSERT_EQ(setup_run_dirs(root), 0);

  char *lock = mem_path(root, "run/lock");
  ASSERT_EQ(io_isdir(lock), 0);
  char *user = mem_path(root, "run/user");
  ASSERT_EQ(io_isdir(user), 0);
  mem_free(user);
  mem_free(lock);
  io_rmtree(root);
  mem_free(root);
}

static void test_setup_devshm_direct(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  mem_free(run);
  char *dev = mem_path(root, "dev");
  io_mkdir(dev);
  mem_free(dev);

  ASSERT_EQ(setup_devshm(root), 0);

  char *shm = mem_path(root, "run/shm");
  ASSERT_EQ(io_isdir(shm), 0);
  mem_free(shm);
  io_rmtree(root);
  mem_free(root);
}

static void test_mount_cgroups_direct(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *run = mem_path(root, "run");
  io_mkdir(run);
  mem_free(run);

  int r = mount_cgroups(root, 1);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(io_isdir(mem_path(root, "sys/fs/cgroup")), 0);
  io_rmtree(root);
  mem_free(root);
}

static void test_mount_cgroup_v2_direct(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(mount_cgroup_v2(root), 0);

  char *cg = mem_path(root, "sys/fs/cgroup");
  ASSERT_EQ(io_isdir(cg), 0);
  mem_free(cg);
  io_rmtree(root);
  mem_free(root);
}

static void test_mount_cgroup_v2_dir_exists(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *cg = mem_path(root, "sys/fs/cgroup");
  io_mkdir(cg);
  mem_free(cg);

  ASSERT_EQ(mount_cgroup_v2(root), 0);
  io_rmtree(root);
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

static void test_mount_cgroup_tmpfs_direct(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *cg = test_temp_root();
  ASSERT_NOT_NULL(cg);

  ASSERT_EQ(mount_cgroup_tmpfs(cg), 0);
  ASSERT_EQ(io_isdir(cg), 0);
  io_rmtree(cg);
  mem_free(cg);
}

static void test_mount_cgroup_v1_direct(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_cgroups = "cpu\t0\t1\t1\nmemory\t0\t1\t1\n";

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  int r = mount_cgroup_v1(root);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(_mock_mount_count, 3);

  char *cg = mem_path(root, "sys/fs/cgroup");
  ASSERT_EQ(io_isdir(cg), 0);
  mem_free(cg);
  io_rmtree(root);
  mem_free(root);
}

static void test_needs_default_dns(void)
{
  ASSERT_EQ(!has_real_ns_in_content("nameserver 8.8.8.8"), 0);
  ASSERT_EQ(!has_real_ns_in_content("nameserver 127.0.0.1"), 1);
  ASSERT_EQ(!has_real_ns_in_content("nameserver 127.0.0.53\nnameserver 8.8.8.8"), 0);
  ASSERT_EQ(!has_real_ns_in_content(""), 1);
  ASSERT_EQ(!has_real_ns_in_content("nameserver 192.168.1.1\nnameserver 10.0.0.1"), 0);
  ASSERT_EQ(!has_real_ns_in_content("nameserver ::1"), 1);
  ASSERT_EQ(!has_real_ns_in_content("nameserver "), 1);
  ASSERT_EQ(!has_real_ns_in_content("nameserver localhost"), 1);
  ASSERT_EQ(!has_real_ns_in_content("nameserver localhost\nnameserver 8.8.8.8"), 0);
}

SUITE(setup)
{
  RUN_TEST(mount_misc_basic);
  RUN_TEST(mount_misc_resolv_mount_fails);
  RUN_TEST(setup_resolv_mkdir_fails);
  RUN_TEST(setup_resolv_touch_fails);
  RUN_TEST(setup_run_dirs_lock_fails);
  RUN_TEST(setup_run_dirs_user_fails);
  RUN_TEST(devshm_blocked);
  RUN_TEST(setup_devshm_nonexistent);
  RUN_TEST(mount_misc_resolv_mount_early_fail);
  RUN_TEST(mount_misc_cgroup_fail_after_high);
  RUN_TEST(setup_devshm_run_shm_blocked_by_file);
  RUN_TEST(mount_misc_all_mounts_fail);
  RUN_TEST(mount_misc_verify_directory_structure);
  RUN_TEST(setup_run_dirs_both_fail);
  RUN_TEST(mount_misc_mount_count);

  RUN_TEST(setup_resolv_direct);
  RUN_TEST(setup_resolv_default_dns_bind);
  RUN_TEST(setup_resolv_default_dns_mount_fails);
  RUN_TEST(setup_resolv_oversized_rejected);
  RUN_TEST(setup_resolv_dangling_symlink);
  RUN_TEST(setup_resolv_valid_symlink_replaced);
  RUN_TEST(setup_run_dirs_direct);
  RUN_TEST(setup_devshm_direct);
  RUN_TEST(mount_cgroup_v2_direct);
  RUN_TEST(mount_cgroup_v2_dir_exists);
  RUN_TEST(advance_line_with_newline);
  RUN_TEST(advance_line_no_newline);
  RUN_TEST(advance_line_empty);
  RUN_TEST(mount_cgroup_tmpfs_direct);
  RUN_TEST(mount_cgroup_v1_direct);
  RUN_TEST(mount_cgroups_direct);

  RUN_TEST(needs_default_dns);
}

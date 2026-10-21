#include "framework.h"
#include "test_util.h"
#include "sandbox/mount.h"
#include "sandbox/nssetup.h"
#include "sandbox/overlay.h"
#include "core/prep.h"
#include "sandbox/ext4fix.h"
#include "sandbox/appdir.h"
#include "sandbox/setup.h"
#include "cli/args.h"
#include "mock.h"
#include "util/io.h"
#include "util/mem.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>

/* === mount_bind tests === */

static void test_mount_bind_success(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *bind = mem_path(root, "bind-test");
  ASSERT_EQ(io_mkdir(bind), 0);

  ASSERT_EQ(mount_bind(root, "bind-test", "/tmp"), 0);
  ASSERT_EQ(_mock_mount_count, 1);

  mem_free(bind);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_bind_mount_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *bind = mem_path(root, "bind-fail");
  ASSERT_EQ(io_mkdir(bind), 0);

  ASSERT_EQ(mount_bind(root, "bind-fail", "/tmp"), -1);
  ASSERT_EQ(_mock_mount_count, 1);

  mem_free(bind);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_bind_mkdir_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *block = mem_path(root, "blocked");
  io_touch(block);

  ASSERT_EQ(mount_bind(root, "blocked", "/tmp"), -1);
  ASSERT_EQ(_mock_mount_count, 0);

  mem_free(block);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_bind_bad_parent_perms(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *child = mem_path(root, "child");
  io_mkdir(child);
  chmod(child, 0000);

  ASSERT_EQ(mount_bind(root, "child/target", "/tmp"), -1);
  ASSERT_EQ(_mock_mount_count, 0);

  chmod(child, 0755);
  mem_free(child);
  test_rmtree(root);
  mem_free(root);
}

/* === strip_trailing_slash === */

static void test_strip_trailing_slash_none(void)
{
  char p[] = "/tmp/foo";
  strip_trailing_slash(p);
  ASSERT_STR_EQ(p, "/tmp/foo");
}

static void test_strip_trailing_slash_single(void)
{
  char p[] = "/tmp/foo/";
  strip_trailing_slash(p);
  ASSERT_STR_EQ(p, "/tmp/foo");
}

static void test_strip_trailing_slash_multiple(void)
{
  char p[] = "/tmp/foo///";
  strip_trailing_slash(p);
  ASSERT_STR_EQ(p, "/tmp/foo");
}

static void test_strip_trailing_slash_root(void)
{
  char p[] = "/";
  strip_trailing_slash(p);
  ASSERT_STR_EQ(p, "/");
}

/* === grandparent === */

static void test_grandparent_basic(void)
{
  char *gp = grandparent("/tmp/cage-gp-basic");
  ASSERT_NOT_NULL(gp);
  ASSERT_STR_EQ(gp, "/tmp");
  mem_free(gp);
}

static void test_grandparent_three_level(void)
{
  char *gp = grandparent("/a/b/c");
  ASSERT_NOT_NULL(gp);
  ASSERT_STR_EQ(gp, "/a");
  mem_free(gp);
}

static void test_grandparent_root(void)
{
  char *gp = grandparent("/");
  ASSERT_NULL(gp);
}

static void test_grandparent_single_component(void)
{
  char *gp = grandparent("/foo");
  ASSERT_NULL(gp);
}

/* === build_overlay_opts === */

static void test_build_overlay_opts_basic(void)
{
  char *opts = build_overlay_opts("/lower", "/upper", "/work");
  ASSERT_NOT_NULL(opts);
  ASSERT_STR_EQ(opts, "lowerdir=/lower,upperdir=/upper,workdir=/work");
  mem_free(opts);
}

static void test_build_overlay_opts_backslash(void)
{
  char *opts = build_overlay_opts("/a\\b", "/upper", "/work");
  ASSERT_NOT_NULL(opts);
  ASSERT_STR_EQ(opts, "lowerdir=/a\\\\b,upperdir=/upper,workdir=/work");
  mem_free(opts);
}

static void test_build_overlay_opts_comma(void)
{
  char *opts = build_overlay_opts("/x,y", "/upper", "/work");
  ASSERT_NOT_NULL(opts);
  ASSERT_STR_EQ(opts, "lowerdir=/x\\,y,upperdir=/upper,workdir=/work");
  mem_free(opts);
}

static void test_build_overlay_opts_mixed(void)
{
  char *opts = build_overlay_opts("/a\\b,c", "/d\\e,f", "/g,h\\i");
  ASSERT_NOT_NULL(opts);
  ASSERT_STR_EQ(opts, "lowerdir=/a\\\\b\\,c,upperdir=/d\\\\e\\,f,workdir=/g\\,h\\\\i");
  mem_free(opts);
}

static void test_build_overlay_opts_empty(void)
{
  char *opts = build_overlay_opts("", "", "");
  ASSERT_NOT_NULL(opts);
  ASSERT_STR_EQ(opts, "lowerdir=,upperdir=,workdir=");
  mem_free(opts);
}

/* === prepare_overlay === */

static void test_prepare_overlay_success(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *upper = mem_path(root, "upper");
  char *work = mem_path(root, "work");

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  strcpy(dirs.lowerdir, "/");
  strcpy(dirs.upperdir, upper);
  strcpy(dirs.workdir, work);

  ASSERT_EQ(prepare_overlay(root, &dirs), 0);
  ASSERT_EQ(_mock_mount_count, 1);

  mem_free(upper);
  mem_free(work);
  test_rmtree(root);
  mem_free(root);
}

static void test_prepare_overlay_upper_mkdir_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *upper = mem_path(root, "upper-block");
  io_touch(upper);

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  strcpy(dirs.lowerdir, "/");
  strcpy(dirs.upperdir, upper);
  char *wk = mem_path(root, "work");
  strcpy(dirs.workdir, wk);

  ASSERT_EQ(prepare_overlay(root, &dirs), -1);
  ASSERT_EQ(_mock_mount_count, 0);

  mem_free(upper);
  mem_free(wk);
  test_rmtree(root);
  mem_free(root);
}

static void test_prepare_overlay_workdir_mkdir_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *work = mem_path(root, "work-block");
  io_touch(work);

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  strcpy(dirs.lowerdir, "/");
  char *up = mem_path(root, "upper");
  strcpy(dirs.upperdir, up);
  strcpy(dirs.workdir, work);

  ASSERT_EQ(prepare_overlay(root, &dirs), -1);
  ASSERT_EQ(_mock_mount_count, 0);

  mem_free(work);
  mem_free(up);
  test_rmtree(root);
  mem_free(root);
}

static void test_prepare_overlay_mount_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *upper = mem_path(root, "upper");
  char *work = mem_path(root, "work");

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  strcpy(dirs.lowerdir, "/");
  strcpy(dirs.upperdir, upper);
  strcpy(dirs.workdir, work);

  ASSERT_EQ(prepare_overlay(root, &dirs), -1);
  ASSERT_EQ(_mock_mount_count, 1);

  mem_free(upper);
  mem_free(work);
  test_rmtree(root);
  mem_free(root);
}

static void test_prepare_overlay_upper_on_overlayfs(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;
  _mock_statfs_ret = 0;
  _mock_statfs_type = 0x794c7630UL;     /* OVERLAYFS_SUPER_MAGIC */

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *upper = mem_path(root, "upper");
  char *work = mem_path(root, "work");

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  strcpy(dirs.lowerdir, "/");
  strcpy(dirs.upperdir, upper);
  strcpy(dirs.workdir, work);

  ASSERT_EQ(prepare_overlay(root, &dirs), -1);
  ASSERT_EQ(_mock_mount_count, 1);

  mem_free(upper);
  mem_free(work);
  test_rmtree(root);
  mem_free(root);
}

static void test_prepare_overlay_enodev_retry(void)
{
  mock_sys_reset();
  _mock_mount_fail_count = 1;
  _mock_mount_errno = ENODEV;
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *upper = mem_path(root, "upper");
  char *work = mem_path(root, "work");

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  strcpy(dirs.lowerdir, "/");
  strcpy(dirs.upperdir, upper);
  strcpy(dirs.workdir, work);

  ASSERT_EQ(prepare_overlay(root, &dirs), 0);
  ASSERT_EQ(_mock_mount_count, 2);

  mem_free(upper);
  mem_free(work);
  test_rmtree(root);
  mem_free(root);
}

/* === mount_fs_type === */

static void test_mount_fs_type_success(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(mount_fs_type(root, "proc", "proc", MS_NOSUID, NULL), 0);
  ASSERT_EQ(_mock_mount_count, 1);

  test_rmtree(root);
  mem_free(root);
}

static void test_mount_fs_type_mkdir_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *block = mem_path(root, "proc");
  io_touch(block);

  ASSERT_EQ(mount_fs_type(root, "proc", "proc", MS_NOSUID, NULL), -1);
  ASSERT_EQ(_mock_mount_count, 0);

  mem_free(block);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_fs_type_mount_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(mount_fs_type(root, "proc", "proc", MS_NOSUID, NULL), -1);
  ASSERT_EQ(_mock_mount_count, 1);

  test_rmtree(root);
  mem_free(root);
}

static void test_mount_fs_type_with_data(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(mount_fs_type(root, "run", "tmpfs", MS_NOSUID | MS_NOEXEC, "mode=0755"), 0);
  ASSERT_EQ(_mock_mount_count, 1);

  test_rmtree(root);
  mem_free(root);
}

/* === mount_volumes === */

static void test_mount_volumes_none(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.volume_count = 0;

  ASSERT_EQ(mount_volumes(root, &cfg), 0);

  test_rmtree(root);
  mem_free(root);
}

static void test_mount_volumes_invalid_spec(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.volumes[0] = ":";
  cfg.volume_count = 1;

  ASSERT_EQ(mount_volumes(root, &cfg), -1);

  test_rmtree(root);
  mem_free(root);
}

static void test_mount_volumes_success(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *host = mem_path(root, "host");
  ASSERT_EQ(io_mkdir(host), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  char vol[4096];
  snprintf(vol, sizeof(vol), "%s:mnt", host);
  cfg.volumes[0] = vol;
  cfg.volume_count = 1;

  ASSERT_EQ(mount_volumes(root, &cfg), 0);

  mem_free(host);
  test_rmtree(root);
  mem_free(root);
}

static void test_mount_volumes_mount_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.volumes[0] = "/nonexistent-xyz-vol:mnt";
  cfg.volume_count = 1;

  ASSERT_EQ(mount_volumes(root, &cfg), -1);

  test_rmtree(root);
  mem_free(root);
}

static void test_mount_volumes_multiple(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *h1 = mem_path(root, "h1");
  char *h2 = mem_path(root, "h2");
  ASSERT_EQ(io_mkdir(h1), 0);
  ASSERT_EQ(io_mkdir(h2), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  char v1[4096], v2[4096];
  snprintf(v1, sizeof(v1), "%s:a", h1);
  snprintf(v2, sizeof(v2), "%s:b", h2);
  cfg.volumes[0] = v1;
  cfg.volumes[1] = v2;
  cfg.volume_count = 2;

  ASSERT_EQ(mount_volumes(root, &cfg), 0);

  mem_free(h1);
  mem_free(h2);
  test_rmtree(root);
  mem_free(root);
}

/* === ns_setup / mount_ns_setup === */

static void test_ns_setup_success(void)
{
  mock_sys_reset();
  _mock_unshare_ret = 0;
  _mock_mount_ret = 0;

  cage_config_t cfg;
  cage_config_init(&cfg);

  ASSERT_EQ(ns_setup(&cfg), 0);
  ASSERT_EQ(_mock_unshare_count, 1);
  ASSERT_EQ(_mock_mount_count, 0);
}

static void test_ns_setup_cgroup(void)
{
  mock_sys_reset();
  _mock_unshare_ret = 0;

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.unshare_cgroup = 1;

  ASSERT_EQ(ns_setup(&cfg), 0);
  ASSERT_EQ(_mock_unshare_count, 1);
}

static void test_ns_setup_unshare_fails(void)
{
  mock_sys_reset();
  _mock_unshare_ret = -1;

  cage_config_t cfg;
  cage_config_init(&cfg);

  ASSERT_EQ(ns_setup(&cfg), -1);
  ASSERT_EQ(_mock_unshare_count, 1);
  ASSERT_EQ(_mock_mount_count, 0);
}

static void test_mount_ns_setup_private(void)
{
  mock_sys_reset();
  _mock_unshare_ret = 0;
  _mock_mount_ret = 0;

  ASSERT_EQ(mount_ns_setup(0), 0);
  ASSERT_EQ(_mock_unshare_count, 1);
  ASSERT_EQ(_mock_mount_count, 1);
}

static void test_mount_ns_setup_root_private(void)
{
  mock_sys_reset();
  _mock_unshare_ret = 0;
  _mock_mount_ret = 0;

  ASSERT_EQ(mount_ns_setup(1), 0);
  ASSERT_EQ(_mock_unshare_count, 1);
  ASSERT_EQ(_mock_mount_count, 0);
}

static void test_mount_ns_setup_unshare_fails(void)
{
  mock_sys_reset();
  _mock_unshare_ret = -1;

  ASSERT_EQ(mount_ns_setup(0), -1);
  ASSERT_EQ(_mock_unshare_count, 1);
  ASSERT_EQ(_mock_mount_count, 0);
}

static void test_mount_ns_setup_mount_fails(void)
{
  mock_sys_reset();
  _mock_unshare_ret = 0;
  _mock_mount_ret = -1;

  ASSERT_EQ(mount_ns_setup(0), -1);
  ASSERT_EQ(_mock_unshare_count, 1);
  ASSERT_EQ(_mock_mount_count, 1);
}

/* === same_fs_detect === */

static void test_same_fs_detect_grandparent_null(void)
{
  _mock_same_fs_ret = -1;
  ASSERT_EQ(same_fs_detect("/tmp", "/", NULL), 0);
  ASSERT_EQ(same_fs_detect("/tmp", "/single", NULL), 0);
  _mock_same_fs_ret = 0;
}

static void test_same_fs_detect_same_fs(void)
{
  _mock_same_fs_ret = -1;
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *sub = mem_path(root, "sub");
  ASSERT_EQ(io_mkdir(sub), 0);

  char *gc = mem_path(root, "sub/grandchild");
  dev_t dev;
  int r = same_fs_detect(root, gc, &dev);
  ASSERT_EQ(r, 1);

  mem_free(gc);
  mem_free(sub);
  test_rmtree(root);
  mem_free(root);
  _mock_same_fs_ret = 0;
}

static void test_same_fs_detect_different_fs(void)
{
  _mock_same_fs_ret = -1;
  int r = same_fs_detect("/tmp", "/var", NULL);
  ASSERT_NE(r, 1);
  _mock_same_fs_ret = 0;
}

static void test_same_fs_detect_lower_not_parent(void)
{
  /* An appdir in a sibling directory of the lowerdir shares the filesystem
   * but is not inside the lower tree, so no loopback is needed. */
  _mock_same_fs_ret = -1;
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *lower = mem_path(root, "lower");
  ASSERT_EQ(io_mkdir(lower), 0);
  char *appdir = mem_path(root, "appdir");
  ASSERT_EQ(io_mkdir(appdir), 0);
  char *upper = mem_path(appdir, "upper");
  int r = same_fs_detect(lower, upper, NULL);
  ASSERT_EQ(r, 0);
  mem_free(upper);
  mem_free(appdir);
  mem_free(lower);
  test_rmtree(root);
  mem_free(root);
  _mock_same_fs_ret = 0;
}

static void test_same_fs_detect_lower_parent_same_fs(void)
{
  /* The appdir is inside the lowerdir tree on the same filesystem: this is
   * the conflict that requires a loopback. */
  _mock_same_fs_ret = -1;
  char *lower = test_temp_root();
  ASSERT_NOT_NULL(lower);
  char *appdir = mem_path(lower, "app");
  ASSERT_EQ(io_mkdir(appdir), 0);
  char *upper = mem_path(appdir, "upper");
  int r = same_fs_detect(lower, upper, NULL);
  ASSERT_EQ(r, 1);
  mem_free(upper);
  mem_free(appdir);
  test_rmtree(lower);
  mem_free(lower);
  _mock_same_fs_ret = 0;
}

/* === make_relocated_dirs === */

static void test_make_relocated_dirs_success(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));

  int r = make_relocated_dirs(&dirs, root);
  ASSERT_EQ(r, 0);

  test_rmtree(root);
  mem_free(root);
}

static void test_make_relocated_dirs_appdir_fails(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *block = mem_path(root, "default");
  io_touch(block);

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  snprintf(dirs.appdir, sizeof(dirs.appdir), "%s/default", root);

  int r = make_relocated_dirs(&dirs, root);
  ASSERT_EQ(r, -1);

  mem_free(block);
  test_rmtree(root);
  mem_free(root);
}

static void test_make_relocated_dirs_upperdir_fails(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *block = mem_path(root, "default");
  io_mkdir(block);
  char *upper = mem_path(root, "default/upper");
  io_touch(upper);

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  snprintf(dirs.appdir, sizeof(dirs.appdir), "%s/default", root);

  int r = make_relocated_dirs(&dirs, root);
  ASSERT_EQ(r, -1);

  mem_free(upper);
  mem_free(block);
  test_rmtree(root);
  mem_free(root);
}

static void test_make_relocated_dirs_workdir_fails(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *block = mem_path(root, "default");
  io_mkdir(block);
  char *upper = mem_path(root, "default/upper");
  io_mkdir(upper);
  char *work = mem_path(root, "default/work");
  io_touch(work);

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  snprintf(dirs.appdir, sizeof(dirs.appdir), "%s/default", root);

  int r = make_relocated_dirs(&dirs, root);
  ASSERT_EQ(r, -1);

  mem_free(work);
  mem_free(upper);
  mem_free(block);
  test_rmtree(root);
  mem_free(root);
}

/* === derive_loopdir === */

static void test_derive_loopdir_default(void)
{
  char loop[4096];
  ASSERT_EQ(derive_loopdir("/var/lib/cage/default", loop, sizeof(loop)), 0);
  ASSERT_STR_EQ(loop, "/var/lib/cage-loop");
}

static void test_derive_loopdir_multilevel(void)
{
  char loop[4096];
  ASSERT_EQ(derive_loopdir("/other/dir/xpto", loop, sizeof(loop)), 0);
  ASSERT_STR_EQ(loop, "/other/dir-loop");
}

static void test_derive_loopdir_suffix_skip(void)
{
  char loop[4096];
  ASSERT_EQ(derive_loopdir("/tmp2/dir-loop/xpto", loop, sizeof(loop)), 0);
  ASSERT_STR_EQ(loop, "/tmp2/dir-loop");
}

static void test_derive_loopdir_root_leaf(void)
{
  char loop[4096];
  ASSERT_EQ(derive_loopdir("/ad8custom", loop, sizeof(loop)), 0);
  ASSERT_STR_EQ(loop, "/ad8custom-loop");
}

static void test_derive_loopdir_buffer_too_small(void)
{
  char loop[8];
  ASSERT_EQ(derive_loopdir("/var/lib/cage/default", loop, sizeof(loop)), -1);
}

/* === path_under === */
static void test_path_under_inside(void)
{
  ASSERT_EQ(path_under("/var/lib/cage-loop/mywork", "/var/lib/cage-loop"), 1);
  ASSERT_EQ(path_under("/var/lib/cage-loop", "/var/lib/cage-loop"), 1);
}

static void test_path_under_outside(void)
{
  ASSERT_EQ(path_under("/myw", "/var/lib/cage-loop"), 0);
}

static void test_path_under_prefix_collision(void)
{
  ASSERT_EQ(path_under("/var/lib/cage-loopX/work", "/var/lib/cage-loop"), 0);
}

/* === handle_same_fs guards === */

static void test_handle_same_fs_symlink_loopdir_fails(void)
{
  mock_sys_reset();
  _mock_same_fs_ret = -1;
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *sub = mem_path(root, "sub");
  ASSERT_EQ(io_mkdir(sub), 0);
  char *app = mem_path(sub, "xpto");
  ASSERT_EQ(io_mkdir(app), 0);

  char parent[4096];
  ASSERT_EQ(derive_loopdir(app, parent, sizeof(parent)), 0);
  char *real = mem_path(root, "real-loop");
  ASSERT_EQ(io_mkdir(real), 0);
  ASSERT_EQ(symlink(real, parent), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  snprintf(dirs.lowerdir, sizeof(dirs.lowerdir), "%s", root);
  snprintf(dirs.appdir, sizeof(dirs.appdir), "%s", app);
  snprintf(dirs.upperdir, sizeof(dirs.upperdir), "%s/upper", app);

  ASSERT_EQ(handle_same_fs(&cfg, &dirs), -1);

  mem_free(real);
  mem_free(app);
  mem_free(sub);
  test_rmtree(root);
  mem_free(root);
  _mock_same_fs_ret = 0;
}

static void test_handle_same_fs_custom_upper_fails(void)
{
  mock_sys_reset();
  _mock_same_fs_ret = -1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *sub = mem_path(root, "sub");
  ASSERT_EQ(io_mkdir(sub), 0);
  char *app = mem_path(sub, "xpto");
  ASSERT_EQ(io_mkdir(app), 0);
  char *upper = mem_path(app, "upper");
  ASSERT_EQ(io_mkdir(upper), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.upperdir = "/custom/upper";

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  snprintf(dirs.lowerdir, sizeof(dirs.lowerdir), "%s", root);
  snprintf(dirs.appdir, sizeof(dirs.appdir), "%s", app);
  snprintf(dirs.upperdir, sizeof(dirs.upperdir), "%s", upper);

  ASSERT_EQ(handle_same_fs(&cfg, &dirs), -1);

  mem_free(upper);
  mem_free(app);
  mem_free(sub);
  test_rmtree(root);
  mem_free(root);
  _mock_same_fs_ret = 0;
}

static void test_handle_same_fs_custom_work_outside_fails(void)
{
  mock_sys_reset();
  _mock_same_fs_ret = -1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *sub = mem_path(root, "sub");
  ASSERT_EQ(io_mkdir(sub), 0);
  char *app = mem_path(sub, "xpto");
  ASSERT_EQ(io_mkdir(app), 0);
  char *upper = mem_path(app, "upper");
  ASSERT_EQ(io_mkdir(upper), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.workdir = "/myw";

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  snprintf(dirs.lowerdir, sizeof(dirs.lowerdir), "%s", root);
  snprintf(dirs.appdir, sizeof(dirs.appdir), "%s", app);
  snprintf(dirs.upperdir, sizeof(dirs.upperdir), "%s", upper);
  snprintf(dirs.workdir, sizeof(dirs.workdir), "%s", cfg.workdir);

  ASSERT_EQ(handle_same_fs(&cfg, &dirs), -1);

  mem_free(upper);
  mem_free(app);
  mem_free(sub);
  test_rmtree(root);
  mem_free(root);
  _mock_same_fs_ret = 0;
}

/* === io_exists EACCES = not found: resolve_existing_parent walks up === */

static void test_same_fs_detect_inaccessible_subdir(void)
{
  mock_sys_reset();
  _mock_same_fs_ret = -1;

  char *path = test_temp_root();
  ASSERT_NOT_NULL(path);

  char *sub = mem_path(path, "sub");
  io_mkdir(sub);
  char *deep = mem_path(sub, "deep");
  io_mkdir(deep);
  char *target = mem_path(deep, "target");
  io_mkdir(target);

  chmod(path, 0000);

  int r = same_fs_detect(path, target, NULL);
  ASSERT_EQ(r, 1);

  chmod(path, 0755);
  _mock_same_fs_ret = 0;
  mem_free(target);
  mem_free(deep);
  mem_free(sub);
  test_rmtree(path);
  mem_free(path);
}

/* === early_setup_appdir resolve failure === */

static void test_early_setup_appdir_resolve_fails(void)
{
  mock_sys_reset();
  _mock_same_fs_ret = 0;

  char long_name[5000];
  memset(long_name, 'a', sizeof(long_name) - 1);
  long_name[sizeof(long_name) - 1] = '\0';

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.appdir = long_name;

  appdir_t dirs;
  memset(&dirs, 0, sizeof(dirs));
  ASSERT_EQ(early_setup_appdir(&cfg, &dirs), -1);
}

/* === mount_all === */

static void test_mount_all_success(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  cage_config_t cfg;
  cage_config_init(&cfg);

  ASSERT_EQ(mount_all(root, &cfg, -2), 0);
  ASSERT_EQ(_mock_mount_count, 9);

  test_rmtree(root);
  mem_free(root);
}

static void test_mount_all_proc_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_mount_fail_after = 1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  cage_config_t cfg;
  cage_config_init(&cfg);

  ASSERT_EQ(mount_all(root, &cfg, -2), -1);

  test_rmtree(root);
  mem_free(root);
}

static void test_mount_all_dev_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_mount_fail_after = 3;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  cage_config_t cfg;
  cage_config_init(&cfg);

  ASSERT_EQ(mount_all(root, &cfg, -2), -1);

  test_rmtree(root);
  mem_free(root);
}

SUITE(mount)
{
  RUN_TEST(mount_all_success);
  RUN_TEST(mount_all_proc_fails);
  RUN_TEST(mount_all_dev_fails);

  RUN_TEST(mount_bind_success);
  RUN_TEST(mount_bind_mount_fails);
  RUN_TEST(mount_bind_mkdir_fails);
  RUN_TEST(mount_bind_bad_parent_perms);

  RUN_TEST(strip_trailing_slash_none);
  RUN_TEST(strip_trailing_slash_single);
  RUN_TEST(strip_trailing_slash_multiple);
  RUN_TEST(strip_trailing_slash_root);

  RUN_TEST(grandparent_basic);
  RUN_TEST(grandparent_three_level);
  RUN_TEST(grandparent_root);
  RUN_TEST(grandparent_single_component);

  RUN_TEST(build_overlay_opts_basic);
  RUN_TEST(build_overlay_opts_backslash);
  RUN_TEST(build_overlay_opts_comma);
  RUN_TEST(build_overlay_opts_mixed);
  RUN_TEST(build_overlay_opts_empty);

  RUN_TEST(prepare_overlay_success);
  RUN_TEST(prepare_overlay_upper_mkdir_fails);
  RUN_TEST(prepare_overlay_workdir_mkdir_fails);
  RUN_TEST(prepare_overlay_mount_fails);
  RUN_TEST(prepare_overlay_upper_on_overlayfs);
  RUN_TEST(prepare_overlay_enodev_retry);

  RUN_TEST(mount_fs_type_success);
  RUN_TEST(mount_fs_type_mkdir_fails);
  RUN_TEST(mount_fs_type_mount_fails);
  RUN_TEST(mount_fs_type_with_data);

  RUN_TEST(mount_volumes_none);
  RUN_TEST(mount_volumes_invalid_spec);
  RUN_TEST(mount_volumes_success);
  RUN_TEST(mount_volumes_mount_fails);
  RUN_TEST(mount_volumes_multiple);

  RUN_TEST(ns_setup_success);
  RUN_TEST(ns_setup_cgroup);
  RUN_TEST(ns_setup_unshare_fails);
  RUN_TEST(mount_ns_setup_private);
  RUN_TEST(mount_ns_setup_root_private);
  RUN_TEST(mount_ns_setup_unshare_fails);
  RUN_TEST(mount_ns_setup_mount_fails);

  RUN_TEST(same_fs_detect_grandparent_null);
  RUN_TEST(same_fs_detect_same_fs);
  RUN_TEST(same_fs_detect_different_fs);
  RUN_TEST(same_fs_detect_lower_not_parent);
  RUN_TEST(same_fs_detect_lower_parent_same_fs);

  RUN_TEST(make_relocated_dirs_success);
  RUN_TEST(make_relocated_dirs_appdir_fails);
  RUN_TEST(make_relocated_dirs_upperdir_fails);
  RUN_TEST(make_relocated_dirs_workdir_fails);

  RUN_TEST(derive_loopdir_default);
  RUN_TEST(derive_loopdir_multilevel);
  RUN_TEST(derive_loopdir_suffix_skip);
  RUN_TEST(derive_loopdir_root_leaf);
  RUN_TEST(derive_loopdir_buffer_too_small);

  RUN_TEST(path_under_inside);
  RUN_TEST(path_under_outside);
  RUN_TEST(path_under_prefix_collision);

  RUN_TEST(handle_same_fs_symlink_loopdir_fails);
  RUN_TEST(handle_same_fs_custom_upper_fails);
  RUN_TEST(handle_same_fs_custom_work_outside_fails);

  RUN_TEST(same_fs_detect_inaccessible_subdir);

  RUN_TEST(early_setup_appdir_resolve_fails);
}

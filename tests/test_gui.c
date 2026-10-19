#include "framework.h"
#include "test_util.h"
#include "sandbox/gui.h"
#include "core/prep.h"
#include "user/user_priv.h"
#include "cli/args.h"
#include "util/env.h"
#include "util/io.h"
#include "mock.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

/* Keep stdlib mocking minimal — add more sys_* wrappers only when a test
   actually needs to intercept a libc call. No speculative wrappers. */

static void test_setup_gui_not_graphical(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000 }, "/home/user"), 0);

  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_no_container_home(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000 }, NULL), 0);

  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_no_host_home(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000 }, "/home/user"), 0);

  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_with_xdg_empty(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *xrd = mem_path(root, "xdg");
  io_mkdir(xrd);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000,.host_xrd = xrd },
                             "/home/user"), 0);

  mem_free(xrd);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_with_xdg_wayland(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *xrd = mem_path(root, "xdg-wl");
  io_mkdir(xrd);

  char *wayland = mem_path(xrd, "wayland-0");
  io_touch(wayland);
  mem_free(wayland);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000,.host_xrd = xrd },
                             "/home/user"), 0);

  mem_free(xrd);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_mount_xdg_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *xrd = mem_path(root, "xdg-fail");
  io_mkdir(xrd);

  char *wayland = mem_path(xrd, "wayland-0");
  io_touch(wayland);
  mem_free(wayland);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000,.host_xrd = xrd },
                             "/home/user"), -1);

  mem_free(xrd);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_x11_enabled(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_access_ret = 0;
  _mock_stat_ret = 0;
  _mock_dirents[0] = (struct mock_dirent) { "X0", DT_SOCK };
  _mock_dirent_count = 1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000 }, "/home/user"), 0);

  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_x11_bind_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;
  _mock_setxattr_ret = 0;
  _mock_access_ret = 0;
  _mock_stat_ret = 0;
  _mock_dirents[0] = (struct mock_dirent) { "X0", DT_SOCK };
  _mock_dirent_count = 1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000 }, "/home/user"), -1);

  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_pulse_enabled(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *xrd = mem_path(root, "xdg-pulse");
  io_mkdir(xrd);

  char *pulse = mem_path(xrd, "pulse/native");
  io_touch(pulse);
  mem_free(pulse);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000,.host_xrd = xrd },
                             "/home/user"), 0);

  mem_free(xrd);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_both_xdg_socks(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *xrd = mem_path(root, "xdg-both");
  io_mkdir(xrd);

  char *wayland = mem_path(xrd, "wayland-0");
  io_touch(wayland);
  mem_free(wayland);

  char *pulse = mem_path(xrd, "pulse/native");
  io_touch(pulse);
  mem_free(pulse);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000,.host_xrd = xrd },
                             "/home/user"), 0);

  mem_free(xrd);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_pulse_cookie_found(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_chown_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *home = mem_path(root, "home-cookie");
  io_mkdir(home);

  char *cookies_dir = mem_path(home, ".config/pulse");
  io_mkdir(cookies_dir);
  mem_free(cookies_dir);

  char *cookie = mem_path(home, ".config/pulse/cookie");
  io_touch(cookie);
  mem_free(cookie);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,.host_home = home,
                             }, "/home/user"), 0);

  mem_free(home);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_pulse_cookie_chown(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_chown_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *home = mem_path(root, "home-chown");
  io_mkdir(home);

  char *cookies_dir = mem_path(home, ".config/pulse");
  io_mkdir(cookies_dir);
  mem_free(cookies_dir);

  char *cookie = mem_path(home, ".config/pulse/cookie");
  io_touch(cookie);
  mem_free(cookie);

  _mock_chown_count = 0;
  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,.host_home = home,
                             }, "/home/user"), 0);
  ASSERT_NE(_mock_chown_count, 0);

  mem_free(home);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_pulse_cookie_chown_fails(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_chown_ret = 1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *home = mem_path(root, "home-chown-fail");
  io_mkdir(home);

  char *cookies_dir = mem_path(home, ".config/pulse");
  io_mkdir(cookies_dir);
  mem_free(cookies_dir);

  char *cookie = mem_path(home, ".config/pulse/cookie");
  io_touch(cookie);
  mem_free(cookie);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,.host_home = home,
                             }, "/home/user"), -1);

  mem_free(home);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_cookie_alt_location(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_chown_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *home = mem_path(root, "home-cookie-alt");
  io_mkdir(home);

  char *cookie = mem_path(home, ".pulse-cookie");
  io_touch(cookie);
  mem_free(cookie);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,.host_home = home,
                             }, "/home/user"), 0);

  mem_free(home);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_xauthority_mounted(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *home = mem_path(root, "home-xauth");
  io_mkdir(home);

  char *xauth = mem_path(home, ".Xauthority");
  io_touch(xauth);
  mem_free(xauth);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,.host_home = home,
                             }, "/home/user"), 0);

  mem_free(home);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_x11_display_mismatch(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_access_ret = 0;
  _mock_stat_ret = 0;
  _mock_dirents[0] = (struct mock_dirent) { "X0", DT_SOCK };
  _mock_dirents[1] = (struct mock_dirent) { "X1", DT_SOCK };
  _mock_dirents[2] = (struct mock_dirent) { "X2", DT_SOCK };
  _mock_dirent_count = 3;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *old = getenv("DISPLAY");
  setenv("DISPLAY", ":9", 1);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000 }, "/home/user"), 0);

  if (old)
    setenv("DISPLAY", old, 1);
  else
    unsetenv("DISPLAY");

  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_x11_display_match(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_access_ret = 0;
  _mock_stat_ret = 0;
  _mock_dirents[0] = (struct mock_dirent) { "X0", DT_SOCK };
  _mock_dirents[1] = (struct mock_dirent) { "X1", DT_SOCK };
  _mock_dirent_count = 2;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *old = getenv("DISPLAY");
  setenv("DISPLAY", ":1", 1);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000 }, "/home/user"), 0);

  if (old)
    setenv("DISPLAY", old, 1);
  else
    unsetenv("DISPLAY");

  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_x11_remote_display(void)
{
  mock_sys_reset();
  _mock_mount_ret = -1;
  _mock_setxattr_ret = 0;
  _mock_access_ret = 0;
  _mock_stat_ret = 0;
  _mock_dirents[0] = (struct mock_dirent) { "X0", DT_SOCK };
  _mock_dirents[1] = (struct mock_dirent) { "X1", DT_SOCK };
  _mock_dirent_count = 2;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *old = getenv("DISPLAY");
  setenv("DISPLAY", "localhost:10.0", 1);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,.host_display = getenv("DISPLAY"),
                             }, "/home/user"), 0);

  if (old)
    setenv("DISPLAY", old, 1);
  else
    unsetenv("DISPLAY");

  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_pulse_server_unix(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *xrd = mem_path(root, "xdg-pulse-unix");
  io_mkdir(xrd);

  char *sock = mem_path(root, "pulse-socket");
  ASSERT_NOT_NULL(sock);
  io_touch(sock);

  size_t slen = strlen(sock);
  char *ps_val = mem_cat("unix:", 5, sock, slen, MEM_CAT_END);
  ASSERT_NOT_NULL(ps_val);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,
                             .host_ps = ps_val,.host_xrd = xrd,
                             }, "/home/user"), 0);

  mem_free(ps_val);
  mem_free(sock);
  mem_free(xrd);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_pulse_server_bare_path(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *xrd = mem_path(root, "xdg-pulse-bare");
  io_mkdir(xrd);

  char *sock = mem_path(root, "pulse-socket2");
  io_touch(sock);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,
                             .host_ps = sock,.host_xrd = xrd,
                             }, "/home/user"), 0);

  mem_free(sock);
  mem_free(xrd);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_pulse_server_tcp_skip(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *xrd = mem_path(root, "xdg-pulse-tcp");
  io_mkdir(xrd);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,
                             .host_ps = "tcp:myhost:4713",.host_xrd = xrd,
                             }, "/home/user"), 0);

  mem_free(xrd);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_custom_pulse_parent_under_root(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_chown_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *xrd = mem_path(root, "xdg-cps");
  io_mkdir(xrd);
  char *pd = mem_path(xrd, "pulse");
  io_mkdir(pd);
  char *native = mem_path(xrd, "pulse/native");
  io_touch(native);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,
                             .host_xrd = xrd,
                             .guest_ps = "unix:/run/cps-test/gsock",
                             }, "/home/user"), 0);

  char *parent = mem_path(root, "run/cps-test");
  ASSERT_EQ(io_exists(parent), 0);
  char *tgt = mem_path(root, "run/cps-test/gsock");
  ASSERT_EQ(io_exists(tgt), 0);

  mem_free(tgt);
  mem_free(parent);
  mem_free(native);
  mem_free(pd);
  mem_free(xrd);
  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_x11_no_display(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_access_ret = 0;
  _mock_stat_ret = 0;
  _mock_dirents[0] = (struct mock_dirent) { "X0", DT_SOCK };
  _mock_dirent_count = 1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char *old = getenv("DISPLAY");
  unsetenv("DISPLAY");

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000 }, "/home/user"), 0);

  if (old)
    setenv("DISPLAY", old, 1);

  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_x11_no_access(void)
{
  mock_sys_reset();
  _mock_access_ret = -1;
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {.root = root,.uid = 1000,.gid = 1000 }, "/home/user"), 0);

  test_rmtree(root);
  mem_free(root);
}

static void test_setup_gui_guest_xrd(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *xrd = mem_path(root, "host-xrd");
  io_mkdir(xrd);

  char *wayland = mem_path(xrd, "wayland-0");
  io_touch(wayland);
  mem_free(wayland);

  ASSERT_EQ(setup_gui_mounts(&(struct gui_ctx) {
                             .root = root,.uid = 1000,.gid = 1000,.host_xrd = xrd,
                             .guest_xrd = "/run/user/1000",
                             }, "/home/user"), 0);

  mem_free(xrd);
  test_rmtree(root);
  mem_free(root);
}

static void test_child_setup_gui_fixup_home(void)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_chown_ret = 0;

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.graphical = 1;

  user_t user;
  memset(&user, 0, sizeof(user));
  user.uid = 1000;
  user.gid = 1000;
  user.home = strdup("/home/testuser");
  user.name = strdup("testuser");
  user.shell = strdup("/bin/sh");

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  env_t env;
  memset(&env, 0, sizeof(env));
  env_map_init(&env.map);

  child_setup_gui(&cfg, &user, root, &env, 0);

  const char *home = env_map_get(&env.map, "HOME");
  ASSERT_STR_EQ(home, "/home/testuser");

  _mock_chown_ret = -1;
  env_map_free(&env.map);
  mem_free(env.host_xdg_runtime_dir);
  free(user.home);
  free(user.name);
  free(user.shell);
  test_rmtree(root);
  mem_free(root);
}

SUITE(gui)
{
  RUN_TEST(setup_gui_not_graphical);
  RUN_TEST(setup_gui_no_container_home);
  RUN_TEST(setup_gui_no_host_home);
  RUN_TEST(setup_gui_with_xdg_empty);
  RUN_TEST(setup_gui_with_xdg_wayland);
  RUN_TEST(setup_gui_mount_xdg_fails);
  RUN_TEST(setup_gui_x11_no_access);
  RUN_TEST(setup_gui_x11_enabled);
  RUN_TEST(setup_gui_x11_bind_fails);
  RUN_TEST(setup_gui_x11_display_mismatch);
  RUN_TEST(setup_gui_x11_display_match);
  RUN_TEST(setup_gui_x11_remote_display);
  RUN_TEST(setup_gui_x11_no_display);
  RUN_TEST(setup_gui_pulse_enabled);
  RUN_TEST(setup_gui_both_xdg_socks);
  RUN_TEST(setup_gui_pulse_cookie_found);
  RUN_TEST(setup_gui_pulse_cookie_chown);
  RUN_TEST(setup_gui_pulse_cookie_chown_fails);
  RUN_TEST(setup_gui_cookie_alt_location);
  RUN_TEST(setup_gui_xauthority_mounted);
  RUN_TEST(setup_gui_pulse_server_unix);
  RUN_TEST(setup_gui_pulse_server_bare_path);
  RUN_TEST(setup_gui_pulse_server_tcp_skip);
  RUN_TEST(setup_gui_custom_pulse_parent_under_root);
  RUN_TEST(setup_gui_guest_xrd);
  RUN_TEST(child_setup_gui_fixup_home);
}

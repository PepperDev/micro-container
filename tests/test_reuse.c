#include "framework.h"
#include "test_util.h"
#include "util/mem.h"
#include "core/reuse.h"
#include "core/proc.h"
#include "cli/args.h"
#include "util/io.h"
#include "user/user_priv.h"
#include "mock.h"
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>

static void test_spawn_existing_join_ns_ok(void)
{
  mock_sys_reset();
  _mock_chdir_ret = 0;
  _mock_setns_ret = 0;
  _mock_fork_ret = 123;
  _mock_waitpid_ret = 123;
  _mock_waitpid_status = 7 << 8;

  cage_config_t cfg;
  cage_config_init(&cfg);

  ASSERT_EQ(spawn_existing(&cfg, getpid(), getuid()), 7);
  ASSERT_EQ(_mock_chdir_count, 1);
  ASSERT_EQ(_mock_setns_count, 4);
  ASSERT_EQ(_mock_fork_count, 1);
  ASSERT_EQ(_mock_waitpid_count, 1);
}

static void test_spawn_existing_fork_fails(void)
{
  mock_sys_reset();
  _mock_chdir_ret = 0;
  _mock_setns_ret = 0;
  _mock_fork_ret = -1;

  cage_config_t cfg;
  cage_config_init(&cfg);

  ASSERT_EQ(spawn_existing(&cfg, getpid(), getuid()), -1);
  ASSERT_EQ(_mock_fork_count, 1);
  ASSERT_EQ(_mock_waitpid_count, 0);
}

static void test_spawn_existing_join_ns_fails(void)
{
  mock_sys_reset();
  _mock_chdir_ret = 0;
  _mock_setns_ret = -1;

  cage_config_t cfg;
  cage_config_init(&cfg);

  ASSERT_EQ(spawn_existing(&cfg, getpid(), getuid()), -1);
  ASSERT_EQ(_mock_fork_count, 0);
}

/* ── Re-entry GUI env ── */

struct reuse_env_fixture {
  cage_config_t cfg;
  user_t user;
  env_t env;
  char *root;
  char *xrd;
  char *old_xrd;
  char *old_ps;
  char *old_display;
};

static void fixture_open(struct reuse_env_fixture *fx, int with_wayland)
{
  mock_sys_reset();
  _mock_mount_ret = 0;
  _mock_setxattr_ret = 0;
  _mock_chown_ret = 0;

  fx->root = test_temp_root();
  ASSERT_NOT_NULL(fx->root);
  fx->xrd = mem_path(fx->root, "xrd");
  ASSERT_NOT_NULL(fx->xrd);
  io_mkdir(fx->xrd);
  if (with_wayland) {
    char *wl = mem_path(fx->xrd, "wayland-0");
    ASSERT_NOT_NULL(wl);
    io_touch(wl);
    mem_free(wl);
  }

  const char *x = getenv("XDG_RUNTIME_DIR");
  const char *p = getenv("PULSE_SERVER");
  const char *d = getenv("DISPLAY");
  fx->old_xrd = x ? strdup(x) : NULL;
  fx->old_ps = p ? strdup(p) : NULL;
  fx->old_display = d ? strdup(d) : NULL;
  setenv("XDG_RUNTIME_DIR", fx->xrd, 1);
  unsetenv("PULSE_SERVER");
  unsetenv("DISPLAY");

  cage_config_init(&fx->cfg);
  fx->cfg.graphical = 1;

  memset(&fx->user, 0, sizeof(fx->user));
  fx->user.uid = 1000;
  fx->user.uid_filled = 1;
  fx->user.gid = 1000;
  fx->user.gid_filled = 1;
  fx->user.home = strdup("/home/testuser");
  fx->user.name = strdup("testuser");
  fx->user.shell = strdup("/bin/sh");

  memset(&fx->env, 0, sizeof(fx->env));
  env_map_init(&fx->env.map);
}

static void fixture_close(struct reuse_env_fixture *fx)
{
  env_map_free(&fx->env.map);
  mem_free(fx->env.host_xdg_runtime_dir);
  free(fx->user.home);
  free(fx->user.name);
  free(fx->user.shell);
  if (fx->old_xrd) {
    setenv("XDG_RUNTIME_DIR", fx->old_xrd, 1);
    free(fx->old_xrd);
  } else {
    unsetenv("XDG_RUNTIME_DIR");
  }
  if (fx->old_ps) {
    setenv("PULSE_SERVER", fx->old_ps, 1);
    free(fx->old_ps);
  } else {
    unsetenv("PULSE_SERVER");
  }
  if (fx->old_display) {
    setenv("DISPLAY", fx->old_display, 1);
    free(fx->old_display);
  } else {
    unsetenv("DISPLAY");
  }
  mem_free(fx->xrd);
  io_rmtree(fx->root);
  mem_free(fx->root);
}

static void test_reuse_env_no_socket_no_xdg(void)
{
  struct reuse_env_fixture fx;
  fixture_open(&fx, 0);

  ASSERT_EQ(reuse_setup_env(&fx.env, &(struct reuse_req) { &fx.cfg, &fx.user, fx.root, NULL, 1000 }), 0);
  ASSERT_NULL(env_map_get(&fx.env.map, "XDG_RUNTIME_DIR"));

  fixture_close(&fx);
}

static void test_reuse_env_wayland_sets_xdg(void)
{
  struct reuse_env_fixture fx;
  fixture_open(&fx, 1);

  ASSERT_EQ(reuse_setup_env(&fx.env, &(struct reuse_req) { &fx.cfg, &fx.user, fx.root, NULL, 1000 }), 0);
  ASSERT_STR_EQ(env_map_get(&fx.env.map, "XDG_RUNTIME_DIR"), "/run/user/1000");
  ASSERT_EQ(_mock_mount_count, 0);
  ASSERT_EQ(_mock_setxattr_count, 0);

  fixture_close(&fx);
}

static void test_reuse_env_x11_fallback_display(void)
{
  struct reuse_env_fixture fx;
  fixture_open(&fx, 0);
  _mock_dirents[0] = (struct mock_dirent) { "X0", DT_SOCK };
  _mock_dirent_count = 1;

  ASSERT_EQ(reuse_setup_env(&fx.env, &(struct reuse_req) { &fx.cfg, &fx.user, fx.root, NULL, 1000 }), 0);
  ASSERT_STR_EQ(env_map_get(&fx.env.map, "DISPLAY"), ":0.0");
  ASSERT_NULL(env_map_get(&fx.env.map, "XDG_RUNTIME_DIR"));
  ASSERT_EQ(_mock_mount_count, 0);
  ASSERT_EQ(_mock_setxattr_count, 0);

  fixture_close(&fx);
}

static void test_reuse_env_e_xdg_wins(void)
{
  struct reuse_env_fixture fx;
  fixture_open(&fx, 1);

  char evar[] = "XDG_RUNTIME_DIR=/custom";
  fx.cfg.envvars[0] = evar;
  fx.cfg.envvar_count = 1;

  ASSERT_EQ(reuse_setup_env(&fx.env, &(struct reuse_req) { &fx.cfg, &fx.user, fx.root, NULL, 1000 }), 0);
  ASSERT_STR_EQ(env_map_get(&fx.env.map, "XDG_RUNTIME_DIR"), "/custom");

  fixture_close(&fx);
}

/* ── -G re-entry inherits the instance GUI env (lifecycle rule 5) ── */

static void test_reuse_env_inherits_instance_gui(void)
{
  struct reuse_env_fixture fx;
  fixture_open(&fx, 1);

  static char blob[] =
      "DISPLAY=:1\0XDG_RUNTIME_DIR=/run/user/999\0WAYLAND_DISPLAY=wayland-1\0" "HOME=/inst\0PATH=/instbin\0";
  ASSERT_EQ(reuse_setup_env(&fx.env, &(struct reuse_req) { &fx.cfg, &fx.user, fx.root, blob, 1000 }), 0);
  ASSERT_STR_EQ(env_map_get(&fx.env.map, "DISPLAY"), ":1");
  ASSERT_STR_EQ(env_map_get(&fx.env.map, "XDG_RUNTIME_DIR"), "/run/user/999");
  ASSERT_STR_EQ(env_map_get(&fx.env.map, "WAYLAND_DISPLAY"), "wayland-1");
  /* non-GUI keys of the instance are not inherited */
  ASSERT_STR_EQ(env_map_get(&fx.env.map, "HOME"), "/home/testuser");
  ASSERT_EQ(_mock_mount_count, 0);
  ASSERT_EQ(_mock_setxattr_count, 0);

  fixture_close(&fx);
}

static void test_reuse_env_e_overrides_instance(void)
{
  struct reuse_env_fixture fx;
  fixture_open(&fx, 0);

  static char blob[] = "DISPLAY=:1\0XDG_RUNTIME_DIR=/run/user/999\0";
  char evar[] = "DISPLAY=:9";
  fx.cfg.envvars[0] = evar;
  fx.cfg.envvar_count = 1;

  ASSERT_EQ(reuse_setup_env(&fx.env, &(struct reuse_req) { &fx.cfg, &fx.user, fx.root, blob, 1000 }), 0);
  ASSERT_STR_EQ(env_map_get(&fx.env.map, "DISPLAY"), ":9");
  ASSERT_STR_EQ(env_map_get(&fx.env.map, "XDG_RUNTIME_DIR"), "/run/user/999");

  fixture_close(&fx);
}

/* ── Direct function tests ── */

static void test_spawn_existing_chdir_fails(void)
{
  mock_sys_reset();
  _mock_chdir_ret = -1;
  _mock_chdir_errno = ENOENT;

  cage_config_t cfg;
  cage_config_init(&cfg);

  ASSERT_EQ(spawn_existing(&cfg, 123, getuid()), -1);
  ASSERT_EQ(_mock_chdir_count, 1);
  ASSERT_EQ(_mock_setns_count, 0);
  ASSERT_EQ(_mock_fork_count, 0);
}

static void test_env_read_pid_environ(void)
{
  mock_sys_reset();
  static const char blob[] = "DISPLAY=:1\0HOME=/home/x\0WAYLAND_DISPLAY=wayland-0\0";
  _mock_environ = blob;
  _mock_environ_len = sizeof(blob) - 1;

  char *b = env_read_pid_environ(123);
  ASSERT_NOT_NULL(b);
  env_map_t map;
  env_map_init(&map);
  env_inherit_gui(&map, b);
  ASSERT_STR_EQ(env_map_get(&map, "DISPLAY"), ":1");
  ASSERT_STR_EQ(env_map_get(&map, "WAYLAND_DISPLAY"), "wayland-0");
  ASSERT_NULL(env_map_get(&map, "HOME"));
  env_map_free(&map);
  mem_free(b);

  _mock_environ = NULL;
  _mock_environ_len = 0;
  ASSERT_NULL(env_read_pid_environ(-1));
}

static void test_spawn_existing_gui_reads_env(void)
{
  mock_sys_reset();
  _mock_chdir_ret = 0;
  _mock_setns_ret = 0;
  _mock_fork_ret = 123;
  _mock_waitpid_ret = 123;
  static const char blob[] = "DISPLAY=:1\0";
  _mock_environ = blob;
  _mock_environ_len = sizeof(blob) - 1;

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.graphical = 1;

  ASSERT_EQ(spawn_existing(&cfg, getpid(), getuid()), 0);
  ASSERT_EQ(_mock_fork_count, 1);
  ASSERT_EQ(_mock_waitpid_count, 1);
}

static void test_spawn_existing_child_failure(void)
{
  mock_sys_reset();
  _mock_chdir_ret = 0;
  _mock_setns_ret = 0;
  static const char blob[] = "DISPLAY=:1\0";
  _mock_environ = blob;
  _mock_environ_len = sizeof(blob) - 1;

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.graphical = 1;
  cfg.command[0] = "/nonexistent/cage-test-command";
  cfg.command_count = 1;

  ASSERT_EQ(spawn_existing(&cfg, getpid(), getuid()), 1);
}

static void test_wait_status_ok(void)
{
  pid_t child = fork();
  if (child == 0)
    _exit(0);
  ASSERT_EQ(wait_status(child), 0);
}

static void test_wait_status_forwards_code(void)
{
  pid_t child = fork();
  if (child == 0)
    _exit(7);
  ASSERT_EQ(wait_status(child), 7);
}

SUITE(reuse)
{
  RUN_TEST(spawn_existing_join_ns_ok);
  RUN_TEST(spawn_existing_fork_fails);
  RUN_TEST(spawn_existing_join_ns_fails);
  RUN_TEST(spawn_existing_chdir_fails);
  RUN_TEST(reuse_env_no_socket_no_xdg);
  RUN_TEST(reuse_env_wayland_sets_xdg);
  RUN_TEST(reuse_env_x11_fallback_display);
  RUN_TEST(reuse_env_e_xdg_wins);
  RUN_TEST(reuse_env_inherits_instance_gui);
  RUN_TEST(reuse_env_e_overrides_instance);
  RUN_TEST(env_read_pid_environ);
  RUN_TEST(spawn_existing_gui_reads_env);
  RUN_TEST(spawn_existing_child_failure);
  RUN_TEST(wait_status_ok);
  RUN_TEST(wait_status_forwards_code);
}

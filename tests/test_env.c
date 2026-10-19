#include "framework.h"
#include "util/env.h"
#include "util/mem.h"
#include "mock.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>

static void test_recognizes_known_envs(void)
{
  char path[] = "PATH=/custom/bin";
  char term[] = "TERM=xterm";
  char lang[] = "LANG=en_US";
  char home[] = "HOME=/myhome";
  char shell[] = "SHELL=/bin/zsh";
  char user[] = "USER=testusr";
  char *envs[] = { path, term, lang, home, shell, user };

  env_t e;
  ASSERT_EQ(parse_envs(&e, envs, 6), 0);
  ASSERT_STR_EQ(e.path, "PATH=/custom/bin");
  ASSERT_STR_EQ(e.term, "TERM=xterm");
  ASSERT_STR_EQ(e.lang, "LANG=en_US");
  ASSERT_STR_EQ(e.home, "HOME=/myhome");
  ASSERT_STR_EQ(e.shell, "SHELL=/bin/zsh");
  ASSERT_STR_EQ(e.user, "USER=testusr");
  ASSERT_EQ(e.map.len, 6);
  ASSERT_STR_EQ(env_map_get(&e.map, "PATH"), "/custom/bin");
  ASSERT_STR_EQ(env_map_get(&e.map, "SHELL"), "/bin/zsh");
  env_map_free(&e.map);
}

static void test_unknown_envs_collected(void)
{
  char a[] = "MYVAR=hello";
  char b[] = "ANOTHER=world";
  char *envs[] = { a, b };

  env_t e;
  ASSERT_EQ(parse_envs(&e, envs, 2), 0);
  ASSERT_EQ(e.map.len, 5);
  ASSERT_STR_EQ(env_map_get(&e.map, "MYVAR"), "hello");
  ASSERT_STR_EQ(env_map_get(&e.map, "ANOTHER"), "world");
  env_map_free(&e.map);
}

static void test_path_default(void)
{
  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  ASSERT_NOT_NULL(e.path);
  ASSERT_EQ(memcmp(e.path, "PATH=", 5), 0);
  ASSERT_NOT_NULL(env_map_get(&e.map, "PATH"));
  env_map_free(&e.map);
}

static void test_term_default(void)
{
  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  ASSERT_NOT_NULL(e.term);
  ASSERT_EQ(memcmp(e.term, "TERM=", 5), 0);
  ASSERT_NOT_NULL(env_map_get(&e.map, "TERM"));
  env_map_free(&e.map);
}

static void test_lang_default(void)
{
  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  ASSERT_NOT_NULL(e.lang);
  ASSERT_EQ(memcmp(e.lang, "LANG=", 5), 0);
  ASSERT_NOT_NULL(env_map_get(&e.map, "LANG"));
  env_map_free(&e.map);
}

static void test_invalid_env_format(void)
{
  char bad[] = "NOEQUALS";
  char *envs[] = { bad };
  env_t e;
  ASSERT_EQ(parse_envs(&e, envs, 1), -1);
}

static void test_mixed_known_unknown(void)
{
  char path[] = "PATH=/bin";
  char custom[] = "CUSTOM_VAR=value";
  char term[] = "TERM=linux";
  char *envs[] = { path, custom, term };

  env_t e;
  ASSERT_EQ(parse_envs(&e, envs, 3), 0);
  ASSERT_STR_EQ(e.path, "PATH=/bin");
  ASSERT_STR_EQ(e.term, "TERM=linux");
  ASSERT_EQ(e.map.len, 4);
  ASSERT_STR_EQ(env_map_get(&e.map, "CUSTOM_VAR"), "value");
  env_map_free(&e.map);
}

static void test_gui_captures_xdg(void)
{
  setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
  setenv("HOME", "/home/test", 1);

  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  ASSERT_NULL(e.host_xdg_runtime_dir);
  ASSERT_NULL(e.host_home);
  env_map_free(&e.map);
}

static void test_term_default_when_unset(void)
{
  char *old = getenv("TERM");
  unsetenv("TERM");

  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  ASSERT_STR_EQ(e.term, "TERM=linux");
  ASSERT_STR_EQ(env_map_get(&e.map, "TERM"), "linux");

  if (old)
    setenv("TERM", old, 1);
  env_map_free(&e.map);
}

static void test_lang_from_environment(void)
{
  setenv("LANG", "en_US.UTF-8", 1);

  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  ASSERT_STR_EQ(e.lang, "LANG=en_US.UTF-8");
  ASSERT_STR_EQ(env_map_get(&e.map, "LANG"), "en_US.UTF-8");
  env_map_free(&e.map);
}

static void test_gui_envs_with_x11(void)
{
  int created = 0;
  if (access("/tmp/.X11-unix", F_OK) != 0) {
    if (mkdir("/tmp/.X11-unix", 0755) == 0)
      created = 1;
  }

  setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
  setenv("HOME", "/home/test", 1);
  unsetenv("DISPLAY");

  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  env_set_gui_envs(&e, getenv("DISPLAY"), 0, getenv("WAYLAND_DISPLAY"), getuid());
  const char *d = env_map_get(&e.map, "DISPLAY");
  ASSERT_NOT_NULL(d);
  ASSERT_STR_EQ(d, ":0.0");

  if (created)
    rmdir("/tmp/.X11-unix");
  env_map_free(&e.map);
  mem_free(e.host_xdg_runtime_dir);
}

static void test_gui_envs_with_wayland(void)
{
  char tmpdir[] = "/tmp/wl_test_XXXXXX";
  if (!mkdtemp(tmpdir))
    return;

  char sock[128];
  snprintf(sock, sizeof(sock), "%s/wayland-0", tmpdir);
  int fd = creat(sock, 0644);
  if (fd >= 0)
    close(fd);

  char *old_wl = getenv("WAYLAND_DISPLAY");
  char *old_xdg = getenv("XDG_RUNTIME_DIR");
  char *old_home = getenv("HOME");
  unsetenv("WAYLAND_DISPLAY");
  setenv("XDG_RUNTIME_DIR", tmpdir, 1);
  setenv("HOME", "/home/test", 1);

  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  env_set_gui_envs(&e, getenv("DISPLAY"), -1, getenv("WAYLAND_DISPLAY"), getuid());
  ASSERT_STR_EQ(env_map_get(&e.map, "WAYLAND_DISPLAY"), "wayland-0");
  ASSERT_STR_EQ(env_map_get(&e.map, "XDG_SESSION_TYPE"), "wayland");
  ASSERT_NOT_NULL(env_map_get(&e.map, "XDG_CURRENT_DESKTOP"));

  unlink(sock);
  rmdir(tmpdir);
  if (old_wl)
    setenv("WAYLAND_DISPLAY", old_wl, 1);
  else
    unsetenv("WAYLAND_DISPLAY");
  if (old_xdg)
    setenv("XDG_RUNTIME_DIR", old_xdg, 1);
  else
    unsetenv("XDG_RUNTIME_DIR");
  if (old_home)
    setenv("HOME", old_home, 1);
  else
    unsetenv("HOME");
  env_map_free(&e.map);
  mem_free(e.host_xdg_runtime_dir);
}

static void test_gui_envs_xdg_fallback(void)
{
  char *old_xdg = getenv("XDG_RUNTIME_DIR");
  char *old_sudo = getenv("SUDO_UID");
  unsetenv("XDG_RUNTIME_DIR");
  unsetenv("SUDO_UID");

  char buf[64];
  const char *result = get_host_xdg_dir(buf, sizeof(buf), getuid());
  ASSERT_NOT_NULL(result);
  ASSERT(memcmp(result, "/run/user/", 10) == 0);

  if (old_xdg)
    setenv("XDG_RUNTIME_DIR", old_xdg, 1);
  if (old_sudo)
    setenv("SUDO_UID", old_sudo, 1);
}

static void test_gui_envs_xdg_sudo_fallback(void)
{
  char *old_xdg = getenv("XDG_RUNTIME_DIR");
  unsetenv("XDG_RUNTIME_DIR");
  setenv("SUDO_UID", "2000", 1);

  char buf[64];
  const char *result = get_host_xdg_dir(buf, sizeof(buf), 0);
  ASSERT_STR_EQ(result, "/run/user/2000");

  if (old_xdg)
    setenv("XDG_RUNTIME_DIR", old_xdg, 1);
  unsetenv("SUDO_UID");
}

static void test_gui_envs_xdg_empty_is_unset(void)
{
  char *old_xdg = getenv("XDG_RUNTIME_DIR");
  setenv("XDG_RUNTIME_DIR", "", 1);
  setenv("SUDO_UID", "2000", 1);

  char buf[64];
  const char *result = get_host_xdg_dir(buf, sizeof(buf), 0);
  ASSERT_STR_EQ(result, "/run/user/2000");

  unsetenv("SUDO_UID");
  result = get_host_xdg_dir(buf, sizeof(buf), 1234);
  ASSERT_STR_EQ(result, "/run/user/1234");

  setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
  result = get_host_xdg_dir(buf, sizeof(buf), 1234);
  ASSERT_STR_EQ(result, "/run/user/1000");

  if (old_xdg)
    setenv("XDG_RUNTIME_DIR", old_xdg, 1);
  else
    unsetenv("XDG_RUNTIME_DIR");
}

static void test_mock_gui_no_envs(void)
{
  _mock_access_ret = -1;
  char *old_xdg = getenv("XDG_RUNTIME_DIR");
  unsetenv("XDG_RUNTIME_DIR");

  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  env_set_gui_envs(&e, getenv("DISPLAY"), -1, getenv("WAYLAND_DISPLAY"), getuid());
  ASSERT_NULL(env_map_get(&e.map, "DISPLAY"));

  if (old_xdg)
    setenv("XDG_RUNTIME_DIR", old_xdg, 1);
  _mock_access_ret = 0;
  env_map_free(&e.map);
  mem_free(e.host_xdg_runtime_dir);
}

static void test_gui_x11_host_display(void)
{
  mock_sys_reset();
  char *old_disp = getenv("DISPLAY");
  char *old_xdg = getenv("XDG_RUNTIME_DIR");
  setenv("DISPLAY", ":2", 1);
  setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
  setenv("HOME", "/home/test", 1);

  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  env_set_gui_envs(&e, getenv("DISPLAY"), 2, getenv("WAYLAND_DISPLAY"), getuid());
  const char *d = env_map_get(&e.map, "DISPLAY");
  ASSERT_NOT_NULL(d);
  ASSERT_STR_EQ(d, ":2");

  if (old_disp)
    setenv("DISPLAY", old_disp, 1);
  else
    unsetenv("DISPLAY");
  if (old_xdg)
    setenv("XDG_RUNTIME_DIR", old_xdg, 1);
  else
    unsetenv("XDG_RUNTIME_DIR");
  env_map_free(&e.map);
  mem_free(e.host_xdg_runtime_dir);
}

static void test_gui_x11_socket_scan(void)
{
  char *old_disp = getenv("DISPLAY");
  char *old_xdg = getenv("XDG_RUNTIME_DIR");
  unsetenv("DISPLAY");
  setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
  setenv("HOME", "/home/test", 1);

  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  env_set_gui_envs(&e, getenv("DISPLAY"), 5, getenv("WAYLAND_DISPLAY"), getuid());
  const char *d = env_map_get(&e.map, "DISPLAY");
  ASSERT_NOT_NULL(d);
  ASSERT_STR_EQ(d, ":5.0");

  if (old_disp)
    setenv("DISPLAY", old_disp, 1);
  else
    unsetenv("DISPLAY");
  if (old_xdg)
    setenv("XDG_RUNTIME_DIR", old_xdg, 1);
  else
    unsetenv("XDG_RUNTIME_DIR");
  env_map_free(&e.map);
  mem_free(e.host_xdg_runtime_dir);
}

static void test_gui_x11_socket_lowest(void)
{
  char *old_disp = getenv("DISPLAY");
  char *old_xdg = getenv("XDG_RUNTIME_DIR");
  unsetenv("DISPLAY");
  setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
  setenv("HOME", "/home/test", 1);

  env_t e;
  ASSERT_EQ(parse_envs(&e, NULL, 0), 0);
  env_set_gui_envs(&e, getenv("DISPLAY"), 1, getenv("WAYLAND_DISPLAY"), getuid());
  const char *d = env_map_get(&e.map, "DISPLAY");
  ASSERT_NOT_NULL(d);
  ASSERT_STR_EQ(d, ":1.0");

  if (old_disp)
    setenv("DISPLAY", old_disp, 1);
  else
    unsetenv("DISPLAY");
  if (old_xdg)
    setenv("XDG_RUNTIME_DIR", old_xdg, 1);
  else
    unsetenv("XDG_RUNTIME_DIR");
  env_map_free(&e.map);
  mem_free(e.host_xdg_runtime_dir);
}

static void test_env_map_override(void)
{
  env_t e;
  memset(&e, 0, sizeof(e));
  env_map_init(&e.map);
  env_map_put(&e.map, "XDG_RUNTIME_DIR", "/run/user/1000");
  env_map_put(&e.map, "DISPLAY", ":0");
  env_map_put(&e.map, "XDG_RUNTIME_DIR", "/custom/path");
  ASSERT_STR_EQ(env_map_get(&e.map, "XDG_RUNTIME_DIR"), "/custom/path");
  ASSERT_EQ(e.map.len, 2);
  env_map_free(&e.map);
}

SUITE(env)
{
  RUN_TEST(recognizes_known_envs);
  RUN_TEST(unknown_envs_collected);
  RUN_TEST(path_default);
  RUN_TEST(term_default);
  RUN_TEST(lang_default);
  RUN_TEST(invalid_env_format);
  RUN_TEST(mixed_known_unknown);
  RUN_TEST(gui_captures_xdg);
  RUN_TEST(term_default_when_unset);
  RUN_TEST(lang_from_environment);
  RUN_TEST(gui_envs_with_x11);
  RUN_TEST(gui_envs_with_wayland);
  RUN_TEST(gui_envs_xdg_fallback);
  RUN_TEST(gui_envs_xdg_sudo_fallback);
  RUN_TEST(gui_envs_xdg_empty_is_unset);
  RUN_TEST(mock_gui_no_envs);
  RUN_TEST(gui_x11_host_display);
  RUN_TEST(gui_x11_socket_scan);
  RUN_TEST(gui_x11_socket_lowest);
  RUN_TEST(env_map_override);
}

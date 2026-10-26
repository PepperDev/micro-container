#include "framework.h"
#include "test_util.h"
#include "util/env.h"
#include "util/mem.h"
#include "core/launch.h"
#include "user/user_priv.h"
#include "core/proc.h"
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static void setup_basic(env_t *e)
{
  memset(e, 0, sizeof(*e));
  e->guest_shell_override = 1;
  env_map_init(&e->map);
  env_map_put(&e->map, "PATH", "/bin");
  env_map_put(&e->map, "TERM", "vt100");
  env_map_put(&e->map, "LANG", "C");
}

static void test_nonexistent_command(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = "/bin/sh";

    setup_basic(&e);

    char *args[] = { "/nonexistent/command", NULL };
    int rc = launch_cage(&u, &e, "/tmp", args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static void test_null_argv(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = "/nonexistent/shell";

    setup_basic(&e);

    int rc = launch_cage(&u, &e, "/tmp", NULL);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static void test_envs_preset(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = "/nonexistent/shell";

    setup_basic(&e);
    env_map_put(&e.map, "HOME", "/custom");
    env_map_put(&e.map, "USER", "customusr");
    env_map_put(&e.map, "SHELL", "/nonexistent/sh");
    env_map_put(&e.map, "MYVAR", "hello");

    char *args[] = { "/nonexistent/command", NULL };
    int rc = launch_cage(&u, &e, "/tmp", args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static void test_no_path_env(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = "/nonexistent/shell";

    memset(&e, 0, sizeof(e));
    env_map_init(&e.map);
    env_map_put(&e.map, "TERM", "vt100");
    env_map_put(&e.map, "LANG", "C");

    char *args[] = { "/nonexistent/command", NULL };
    int rc = launch_cage(&u, &e, "/tmp", args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static void test_empty_home(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "";
    u.shell = "/nonexistent/shell";

    setup_basic(&e);
    env_map_put(&e.map, "HOME", "/tmp");

    char *args[] = { "/nonexistent/command", NULL };
    int rc = launch_cage(&u, &e, "/tmp", args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static void test_empty_cwd(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = "/nonexistent/shell";

    setup_basic(&e);

    char *args[] = { "/nonexistent/command", NULL };
    int rc = launch_cage(&u, &e, NULL, args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static void test_gui_envs_with_ngui(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = "/nonexistent/shell";

    setup_basic(&e);
    env_map_put(&e.map, "DISPLAY", ":0");

    char *args[] = { "/nonexistent/command", NULL };
    int rc = launch_cage(&u, &e, "/tmp", args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static void test_setup_cwd_creates_missing_path(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *cwd = mem_path(root, "cwd-missing");

  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = "/nonexistent/shell";

    setup_basic(&e);

    char *args[] = { "/nonexistent/command", NULL };
    int rc = launch_cage(&u, &e, cwd, args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
  mem_free(cwd);
  io_rmtree(root);
  mem_free(root);
}

static void test_path_without_equals(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = "/nonexistent/shell";

    memset(&e, 0, sizeof(e));
    env_map_init(&e.map);
    env_map_put(&e.map, "TERM", "vt100");
    env_map_put(&e.map, "LANG", "C");

    char *args[] = { "/nonexistent/command", NULL };
    int rc = launch_cage(&u, &e, "/tmp", args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static void test_dash_command(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = "/nonexistent/shell";

    setup_basic(&e);

    char *args[] = { "-", NULL };
    int rc = launch_cage(&u, &e, "/tmp", args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static void test_dash_command_with_shell_env(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = "/nonexistent/shell";

    setup_basic(&e);
    env_map_put(&e.map, "SHELL", "/nonexistent/sh");

    char *args[] = { "-", NULL };
    int rc = launch_cage(&u, &e, "/tmp", args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

static void test_dash_command_fallback_sh(void)
{
  pid_t pid = pidfork();
  if (pid == 0) {
    user_t u;
    env_t e;

    memset(&u, 0, sizeof(u));
    u.uid_filled = 1;
    u.gid_filled = 1;
    u.passwd_parsed = 1;
    u.group_parsed = 1;
    u.uid = 1000;
    u.gid = 1000;
    u.groups = &u.gid;
    u.groups_count = 1;
    u.name = "test";
    u.home = "/tmp";
    u.shell = NULL;

    setup_basic(&e);

    char *args[] = { "-", NULL };
    int rc = launch_cage(&u, &e, "/tmp", args);
    env_map_free(&e.map);
    exit(rc == -1 ? 0 : 1);
  }

  int status;
  ASSERT_EQ(pidwait(pid, &status), 0);
  ASSERT(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

SUITE(launch)
{
  RUN_TEST(nonexistent_command);
  RUN_TEST(null_argv);
  RUN_TEST(envs_preset);
  RUN_TEST(no_path_env);
  RUN_TEST(empty_home);
  RUN_TEST(empty_cwd);
  RUN_TEST(gui_envs_with_ngui);
  RUN_TEST(setup_cwd_creates_missing_path);
  RUN_TEST(path_without_equals);
  RUN_TEST(dash_command);
  RUN_TEST(dash_command_with_shell_env);
  RUN_TEST(dash_command_fallback_sh);
}

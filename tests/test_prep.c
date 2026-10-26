#include "framework.h"
#include "test_util.h"
#include "core/prep.h"
#include "util/io.h"
#include "util/mem.h"
#include <stdio.h>
#include <string.h>

static void write_file(const char *path, const char *content)
{
  FILE *f = fopen(path, "w");
  if (f) {
    fprintf(f, "%s", content);
    fclose(f);
  }
}

static void test_resolve_script_guest_absolute(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char path[512];
  snprintf(path, sizeof(path), "%s/init.sh", root);
  write_file(path, "echo hi\n");

  char buf[512];
  ASSERT_EQ(resolve_script("/init.sh", root, buf, sizeof(buf)), 0);
  ASSERT_STR_EQ(buf, "/init.sh");
  io_rmtree(root);
  mem_free(root);
}

static void test_resolve_script_relative(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char path[512];
  snprintf(path, sizeof(path), "%s/init.sh", root);
  write_file(path, "echo hi\n");

  char buf[512];
  ASSERT_EQ(resolve_script("init.sh", root, buf, sizeof(buf)), 0);
  ASSERT_STR_EQ(buf, "init.sh");
  io_rmtree(root);
  mem_free(root);
}

static void test_resolve_script_nested(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char dir[512];
  snprintf(dir, sizeof(dir), "%s/scripts", root);
  ASSERT_EQ(io_mkdir(dir), 0);
  char path[512];
  snprintf(path, sizeof(path), "%s/scripts/init.sh", root);
  write_file(path, "echo hi\n");

  char buf[512];
  ASSERT_EQ(resolve_script("/scripts/init.sh", root, buf, sizeof(buf)), 0);
  ASSERT_STR_EQ(buf, "/scripts/init.sh");
  io_rmtree(root);
  mem_free(root);
}

static void test_resolve_script_missing(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  char buf[512];
  ASSERT_EQ(resolve_script("/nope.sh", root, buf, sizeof(buf)), -1);
  io_rmtree(root);
  mem_free(root);
}

static void test_resolve_script_truncated(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "init-with-a-long-name.sh");
  ASSERT_NOT_NULL(path);
  write_file(path, "echo hi\n");

  char buf[8];
  ASSERT_EQ(resolve_script(path, root, buf, sizeof(buf)), -1);
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

static void test_resolve_script_host_path_as_is(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *path = mem_path(root, "init.sh");
  ASSERT_NOT_NULL(path);
  write_file(path, "echo hi\n");

  char buf[512];
  ASSERT_EQ(resolve_script(path, root, buf, sizeof(buf)), 0);
  ASSERT_STR_EQ(buf, path);
  mem_free(path);
  io_rmtree(root);
  mem_free(root);
}

static void test_resolve_host_home_valid(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);

  setenv("SUDO_HOME", root, 1);
  env_t env;
  memset(&env, 0, sizeof(env));
  resolve_host_home(&env);
  ASSERT_STR_EQ(env.host_home, root);

  unsetenv("SUDO_HOME");
  io_rmtree(root);
  mem_free(root);
}

static void test_resolve_host_home_dotdot_rejected(void)
{
  setenv("SUDO_HOME", "/tmp/../etc", 1);
  env_t env;
  memset(&env, 0, sizeof(env));
  resolve_host_home(&env);
  ASSERT_NULL(env.host_home);
  unsetenv("SUDO_HOME");
}

static void test_resolve_host_home_relative_rejected(void)
{
  setenv("SUDO_HOME", "relative/path", 1);
  env_t env;
  memset(&env, 0, sizeof(env));
  resolve_host_home(&env);
  ASSERT_NULL(env.host_home);
  unsetenv("SUDO_HOME");
}

static void test_resolve_host_home_missing_dir_rejected(void)
{
  setenv("SUDO_HOME", "/nonexistent-cage-home-xyz", 1);
  env_t env;
  memset(&env, 0, sizeof(env));
  resolve_host_home(&env);
  ASSERT_NULL(env.host_home);
  unsetenv("SUDO_HOME");
}

static void test_find_envvar_empty_is_unset(void)
{
  cage_config_t cfg;
  cage_config_init(&cfg);

  cfg.envvars[0] = "PULSE_SERVER=";
  cfg.envvar_count = 1;
  ASSERT_NULL(find_envvar(&cfg, "PULSE_SERVER"));

  cfg.envvars[0] = "XDG_RUNTIME_DIR=";
  ASSERT_NULL(find_envvar(&cfg, "XDG_RUNTIME_DIR"));

  cfg.envvars[0] = "PULSE_SERVERX=1";
  ASSERT_NULL(find_envvar(&cfg, "PULSE_SERVER"));

  cfg.envvars[0] = "PULSE_SERVER=unix:/run/x";
  ASSERT_STR_EQ(find_envvar(&cfg, "PULSE_SERVER"), "unix:/run/x");

  cfg.envvar_count = 0;
  ASSERT_NULL(find_envvar(&cfg, "PULSE_SERVER"));
}

SUITE(prep)
{
  RUN_TEST(resolve_script_guest_absolute);
  RUN_TEST(resolve_script_relative);
  RUN_TEST(resolve_script_nested);
  RUN_TEST(resolve_script_missing);
  RUN_TEST(resolve_script_truncated);
  RUN_TEST(resolve_script_host_path_as_is);
  RUN_TEST(find_envvar_empty_is_unset);
  RUN_TEST(resolve_host_home_valid);
  RUN_TEST(resolve_host_home_dotdot_rejected);
  RUN_TEST(resolve_host_home_relative_rejected);
  RUN_TEST(resolve_host_home_missing_dir_rejected);
}

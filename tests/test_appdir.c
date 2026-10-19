#include "framework.h"
#include "sandbox/appdir.h"
#include <string.h>

static void test_defaults_no_name(void)
{
  cage_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));

  appdir_t d;
  ASSERT_EQ(resolve_appdir(&cfg, &d), 0);
  ASSERT_STR_EQ(d.appdir, "/var/lib/cage/default");
  ASSERT_STR_EQ(d.upperdir, "/var/lib/cage/default/upper");
  ASSERT_STR_EQ(d.workdir, "/var/lib/cage/default/work");
  ASSERT_STR_EQ(d.lowerdir, "/");
}

static void test_with_name(void)
{
  cage_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.name = "myapp";

  appdir_t d;
  ASSERT_EQ(resolve_appdir(&cfg, &d), 0);
  ASSERT_STR_EQ(d.appdir, "/var/lib/cage/app-myapp");
  ASSERT_STR_EQ(d.upperdir, "/var/lib/cage/app-myapp/upper");
  ASSERT_STR_EQ(d.workdir, "/var/lib/cage/app-myapp/work");
}

static void test_custom_appdir(void)
{
  cage_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.appdir = "/custom/path";

  appdir_t d;
  ASSERT_EQ(resolve_appdir(&cfg, &d), 0);
  ASSERT_STR_EQ(d.appdir, "/custom/path");
  ASSERT_STR_EQ(d.upperdir, "/custom/path/upper");
  ASSERT_STR_EQ(d.workdir, "/custom/path/work");
}

static void test_custom_upper_work_lower(void)
{
  cage_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.name = "test";
  cfg.upperdir = "/custom/upper";
  cfg.workdir = "/custom/work";
  cfg.lowerdir = "/custom/lower";

  appdir_t d;
  ASSERT_EQ(resolve_appdir(&cfg, &d), 0);
  ASSERT_STR_EQ(d.appdir, "/var/lib/cage/app-test");
  ASSERT_STR_EQ(d.upperdir, "/custom/upper");
  ASSERT_STR_EQ(d.workdir, "/custom/work");
  ASSERT_STR_EQ(d.lowerdir, "/custom/lower");
}

static void test_all_custom(void)
{
  cage_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.appdir = "/my/app";
  cfg.upperdir = "/my/upper";
  cfg.workdir = "/my/work";
  cfg.lowerdir = "/my/lower";

  appdir_t d;
  ASSERT_EQ(resolve_appdir(&cfg, &d), 0);
  ASSERT_STR_EQ(d.appdir, "/my/app");
  ASSERT_STR_EQ(d.upperdir, "/my/upper");
  ASSERT_STR_EQ(d.workdir, "/my/work");
  ASSERT_STR_EQ(d.lowerdir, "/my/lower");
}

static void test_name_overrides_default_appdir(void)
{
  cage_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.appdir = "/custom";
  cfg.name = "ignored";

  appdir_t d;
  ASSERT_EQ(resolve_appdir(&cfg, &d), 0);
  ASSERT_STR_EQ(d.appdir, "/custom");
  ASSERT_STR_EQ(d.upperdir, "/custom/upper");
}

SUITE(appdir)
{
  RUN_TEST(defaults_no_name);
  RUN_TEST(with_name);
  RUN_TEST(custom_appdir);
  RUN_TEST(custom_upper_work_lower);
  RUN_TEST(all_custom);
  RUN_TEST(name_overrides_default_appdir);
}

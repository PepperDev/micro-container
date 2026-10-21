#include "framework.h"
#include "cli/args.h"
#include <string.h>

static void test_defaults(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 2, argv), 0);
  ASSERT_STR_EQ(cfg.currentdir, "/");
  ASSERT_STR_EQ(cfg.lowerdir, "/");
  ASSERT_EQ(cfg.group, (void *)NULL);
  ASSERT_STR_EQ(cfg.user, "0");
  ASSERT_EQ(cfg.unshare_cgroup, 0);
  ASSERT_EQ(cfg.unshare_net, 0);
  ASSERT_EQ(cfg.graphical, 0);
  ASSERT_EQ(cfg.kill_mode, 0);
  ASSERT_EQ(cfg.show_help, 0);
  ASSERT_EQ(cfg.show_version, 0);
  ASSERT_EQ(cfg.envvar_count, 0);
  ASSERT_EQ(cfg.volume_count, 0);
}

static void test_command_only(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 2, argv), 0);
  ASSERT_EQ(cfg.command_count, 1);
  ASSERT_STR_EQ(cfg.command[0], "/bin/sh");
}

static void test_command_with_args(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "/bin/ls", "-la", "/tmp", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_EQ(cfg.command_count, 3);
  ASSERT_STR_EQ(cfg.command[0], "/bin/ls");
  ASSERT_STR_EQ(cfg.command[1], "-la");
  ASSERT_STR_EQ(cfg.command[2], "/tmp");
}

static void test_dash_dash(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--", "/bin/sh", "-x", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_EQ(cfg.command_count, 2);
  ASSERT_STR_EQ(cfg.command[0], "/bin/sh");
  ASSERT_STR_EQ(cfg.command[1], "-x");
  ASSERT_EQ(cfg.has_dashdash, 1);
}

static void test_dash_dash_bare(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 2, argv), 0);
  ASSERT_EQ(cfg.command_count, 0);
  ASSERT_EQ(cfg.has_dashdash, 1);
}

static void test_dash_dash_after_flag(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-C", "--", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_EQ(cfg.command_count, 1);
  ASSERT_EQ(cfg.has_dashdash, 1);
}

static void test_dash_dash_as_option_arg(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-a", "--", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 3, argv), 0);
  ASSERT_STR_EQ(cfg.appdir, "--");
  ASSERT_EQ(cfg.command_count, 0);
  ASSERT_EQ(cfg.has_dashdash, 0);
}

static void test_dash_dash_as_option_arg_then_separator(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-a", "--", "--", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 5, argv), 0);
  ASSERT_STR_EQ(cfg.appdir, "--");
  ASSERT_EQ(cfg.command_count, 1);
  ASSERT_EQ(cfg.has_dashdash, 1);
}

static void test_option_a_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-a", "/my/app", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.appdir, "/my/app");
  ASSERT_EQ(cfg.command_count, 1);
  ASSERT_STR_EQ(cfg.command[0], "/bin/sh");
}

static void test_option_a_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--appdir", "/my/app", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.appdir, "/my/app");
  ASSERT_EQ(cfg.command_count, 1);
  ASSERT_STR_EQ(cfg.command[0], "/bin/sh");
}

static void test_option_c_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-c", "/home", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.currentdir, "/home");
}

static void test_option_c_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--cwd", "/home", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.currentdir, "/home");
}

static void test_option_C_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-C", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 3, argv), 0);
  ASSERT_EQ(cfg.unshare_cgroup, 1);
}

static void test_option_C_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--cgroup", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 3, argv), 0);
  ASSERT_EQ(cfg.unshare_cgroup, 1);
}

static void test_option_e_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-e", "PATH=/usr/bin", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_EQ(cfg.envvar_count, 1);
  ASSERT_STR_EQ(cfg.envvars[0], "PATH=/usr/bin");
}

static void test_option_e_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--env", "HOME=/root", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_EQ(cfg.envvar_count, 1);
  ASSERT_STR_EQ(cfg.envvars[0], "HOME=/root");
}

static void test_option_e_multiple(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-e", "A=1", "-e", "B=2", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 6, argv), 0);
  ASSERT_EQ(cfg.envvar_count, 2);
  ASSERT_STR_EQ(cfg.envvars[0], "A=1");
  ASSERT_STR_EQ(cfg.envvars[1], "B=2");
}

static void test_option_G_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-G", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 3, argv), 0);
  ASSERT_EQ(cfg.graphical, 1);
}

static void test_option_G_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--gui", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 3, argv), 0);
  ASSERT_EQ(cfg.graphical, 1);
}

static void test_option_g_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-g", "1000", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.group, "1000");
}

static void test_option_g_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--group", "1000", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.group, "1000");
}

static void test_option_h_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-h", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 2, argv), 0);
  ASSERT_EQ(cfg.show_help, 1);
}

static void test_option_h_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--help", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 2, argv), 0);
  ASSERT_EQ(cfg.show_help, 1);
}

static void test_option_i_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-i", "/tmp/init.sh", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.initscript, "/tmp/init.sh");
}

static void test_option_i_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--init", "/tmp/init.sh", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.initscript, "/tmp/init.sh");
}

static void test_option_k_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-k", "-n", "myapp", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_EQ(cfg.kill_mode, 1);
}

static void test_option_k_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--kill", "-n", "myapp", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_EQ(cfg.kill_mode, 1);
}

static void test_option_l_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-l", "/mnt/root", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.lowerdir, "/mnt/root");
}

static void test_option_l_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--lower", "/mnt/root", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.lowerdir, "/mnt/root");
}

static void test_option_N_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-N", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 3, argv), 0);
  ASSERT_EQ(cfg.unshare_net, 1);
}

static void test_option_N_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--net", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 3, argv), 0);
  ASSERT_EQ(cfg.unshare_net, 1);
}

static void test_option_n_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-n", "myapp", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.name, "myapp");
}

static void test_option_n_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--name", "myapp", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.name, "myapp");
}

static void test_option_n_rejects_path(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-n", "../../tmp/x", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), -1);
}

static void test_option_n_rejects_empty(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-n", "", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), -1);
}

static void test_option_p_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-p", "/run/cage.pid", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.pidfile, "/run/cage.pid");
}

static void test_option_p_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--pidfile", "/run/cage.pid", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.pidfile, "/run/cage.pid");
}

static void test_option_U_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-U", "/custom/upper", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.upperdir, "/custom/upper");
}

static void test_option_U_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--upper", "/custom/upper", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.upperdir, "/custom/upper");
}

static void test_option_u_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-u", "1000", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.user, "1000");
}

static void test_option_u_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--user", "1000", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.user, "1000");
}

static void test_option_V_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-V", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 2, argv), 0);
  ASSERT_EQ(cfg.show_version, 1);
}

static void test_option_V_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--version", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 2, argv), 0);
  ASSERT_EQ(cfg.show_version, 1);
}

static void test_option_v_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-v", "/data:/mnt/data", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_EQ(cfg.volume_count, 1);
  ASSERT_STR_EQ(cfg.volumes[0], "/data:/mnt/data");
}

static void test_option_v_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--volume", "/data:/mnt/data", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_EQ(cfg.volume_count, 1);
  ASSERT_STR_EQ(cfg.volumes[0], "/data:/mnt/data");
}

static void test_option_v_multiple(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-v", "/a:/a", "-v", "/b:/b", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 6, argv), 0);
  ASSERT_EQ(cfg.volume_count, 2);
  ASSERT_STR_EQ(cfg.volumes[0], "/a:/a");
  ASSERT_STR_EQ(cfg.volumes[1], "/b:/b");
}

static void test_option_w_short(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-w", "/custom/work", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.workdir, "/custom/work");
}

static void test_option_w_long(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--work", "/custom/work", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 4, argv), 0);
  ASSERT_STR_EQ(cfg.workdir, "/custom/work");
}

static void test_unknown_short_option(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-X", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 3, argv), -1);
}

static void test_unknown_long_option(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "--unknown", "/bin/sh", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 3, argv), -1);
}

static void test_missing_short_arg(void)
{
  cage_config_t cfg;
  char *argv[] = { "cage", "-a", NULL };

  cage_config_init(&cfg);
  ASSERT_EQ(cage_parse_args(&cfg, 2, argv), -1);
}

SUITE(args)
{
  RUN_TEST(defaults);
  RUN_TEST(command_only);
  RUN_TEST(command_with_args);
  RUN_TEST(dash_dash);
  RUN_TEST(dash_dash_bare);
  RUN_TEST(dash_dash_after_flag);
  RUN_TEST(dash_dash_as_option_arg);
  RUN_TEST(dash_dash_as_option_arg_then_separator);
  RUN_TEST(option_a_short);
  RUN_TEST(option_a_long);
  RUN_TEST(option_c_short);
  RUN_TEST(option_c_long);
  RUN_TEST(option_C_short);
  RUN_TEST(option_C_long);
  RUN_TEST(option_e_short);
  RUN_TEST(option_e_long);
  RUN_TEST(option_e_multiple);
  RUN_TEST(option_G_short);
  RUN_TEST(option_G_long);
  RUN_TEST(option_g_short);
  RUN_TEST(option_g_long);
  RUN_TEST(option_h_short);
  RUN_TEST(option_h_long);
  RUN_TEST(option_i_short);
  RUN_TEST(option_i_long);
  RUN_TEST(option_k_short);
  RUN_TEST(option_k_long);
  RUN_TEST(option_l_short);
  RUN_TEST(option_l_long);
  RUN_TEST(option_N_short);
  RUN_TEST(option_N_long);
  RUN_TEST(option_n_short);
  RUN_TEST(option_n_long);
  RUN_TEST(option_n_rejects_path);
  RUN_TEST(option_n_rejects_empty);
  RUN_TEST(option_p_short);
  RUN_TEST(option_p_long);
  RUN_TEST(option_U_short);
  RUN_TEST(option_U_long);
  RUN_TEST(option_u_short);
  RUN_TEST(option_u_long);
  RUN_TEST(option_V_short);
  RUN_TEST(option_V_long);
  RUN_TEST(option_v_short);
  RUN_TEST(option_v_long);
  RUN_TEST(option_v_multiple);
  RUN_TEST(option_w_short);
  RUN_TEST(option_w_long);
  RUN_TEST(unknown_short_option);
  RUN_TEST(unknown_long_option);
  RUN_TEST(missing_short_arg);
}

#include "framework.h"
#include "test_util.h"
#include "core/super.h"
#include "mock.h"
#include <sys/stat.h>

static void set_root_suid(void)
{
  _mock_stat_ret = 0;
  _mock_stat_uid = 0;
  _mock_stat_gid = 0;
  _mock_stat_mode = S_IFREG | 04755;
}

static void test_fix_stat_fails(void)
{
  mock_sys_reset();
  _mock_stat_ret = 1;
  ASSERT_EQ(super_fix(), -1);
}

static void test_fix_already_ok(void)
{
  mock_sys_reset();
  set_root_suid();
  ASSERT_EQ(super_fix(), 0);
  ASSERT_EQ(_mock_chown_count, 0);
  ASSERT_EQ(_mock_super_chmod_count, 0);
}

static void test_fix_chown_fails(void)
{
  mock_sys_reset();
  set_root_suid();
  _mock_stat_uid = 1000;
  _mock_chown_ret = 1;
  ASSERT_EQ(super_fix(), -1);
  ASSERT_EQ(_mock_chown_count, 1);
  ASSERT_EQ(_mock_super_chmod_count, 0);
}

static void test_fix_chown_then_chmod(void)
{
  mock_sys_reset();
  _mock_stat_ret = 0;
  _mock_stat_uid = 1000;
  _mock_stat_gid = 1000;
  _mock_stat_mode = S_IFREG | 0755;
  _mock_chown_ret = 0;
  _mock_super_chmod_ret = 0;
  ASSERT_EQ(super_fix(), 0);
  ASSERT_EQ(_mock_chown_count, 1);
  ASSERT_EQ(_mock_super_chmod_count, 1);
}

static void test_fix_chmod_fails(void)
{
  mock_sys_reset();
  set_root_suid();
  _mock_stat_mode = S_IFREG | 0755;
  _mock_super_chmod_ret = -1;
  ASSERT_EQ(super_fix(), -1);
  ASSERT_EQ(_mock_super_chmod_count, 1);
}

static void test_escalate_suid_rejected(void)
{
  mock_sys_reset();
  set_root_suid();
  char *argv[] = { "cage", NULL };
  ASSERT_EQ(super_escalate(argv, 1000, 1000, 1000, 1000), -1);
  ASSERT_EQ(_mock_super_vfork_count, 0);
}

static void test_escalate_vfork_fails(void)
{
  mock_sys_reset();
  _mock_stat_ret = 0;
  _mock_stat_uid = 1000;
  _mock_stat_gid = 1000;
  _mock_stat_mode = S_IFREG | 0755;
  _mock_super_vfork_ret = -1;

  char *argv[] = { "cage", NULL };
  ASSERT_EQ(super_escalate(argv, 1000, 1000, 1000, 1000), -1);
  ASSERT_EQ(_mock_super_vfork_count, 1);
  ASSERT_EQ(_mock_super_execvp_count, 0);
}

static void test_escalate_sudo_fails(void)
{
  mock_sys_reset();
  _mock_stat_ret = 0;
  _mock_stat_uid = 1000;
  _mock_stat_gid = 1000;
  _mock_stat_mode = S_IFREG | 0755;
  _mock_super_vfork_ret = 123;
  _mock_super_waitpid_ret = 123;
  _mock_super_waitpid_status = 1 << 8;

  char *argv[] = { "cage", NULL };
  ASSERT_EQ(super_escalate(argv, 1000, 1000, 1000, 1000), -1);
  ASSERT_EQ(_mock_super_waitpid_count, 1);
  ASSERT_EQ(_mock_super_execvp_count, 0);
}

static void test_escalate_execvp_fails(void)
{
  mock_sys_reset();
  _mock_stat_ret = 0;
  _mock_stat_uid = 1000;
  _mock_stat_gid = 1000;
  _mock_stat_mode = S_IFREG | 0755;
  _mock_super_vfork_ret = 123;
  _mock_super_waitpid_ret = 123;
  _mock_super_execvp_ret = -1;

  char *argv[] = { "cage", NULL };
  ASSERT_EQ(super_escalate(argv, 1000, 1000, 1000, 1000), -1);
  ASSERT_EQ(_mock_super_execvp_count, 1);
}

static void test_escalate_as_root(void)
{
  mock_sys_reset();
  set_root_suid();
  _mock_setreuid_ret = 0;
  _mock_setregid_ret = 0;

  char *argv[] = { "cage", NULL };
  ASSERT_EQ(super_escalate(argv, 0, 1000, 0, 1000), 0);
  ASSERT_EQ(_mock_setreuid_count, 1);
  ASSERT_EQ(_mock_setregid_count, 1);
}

static void test_is_suid_binary_check(void)
{
  mock_sys_reset();
  set_root_suid();
  ASSERT_EQ(is_suid_binary(), 1);
  _mock_stat_mode = S_IFREG | 0755;
  ASSERT_EQ(is_suid_binary(), 0);
  _mock_stat_mode = S_IFREG | 04755;
  _mock_stat_uid = 1000;
  ASSERT_EQ(is_suid_binary(), 0);
  _mock_stat_ret = 1;
  ASSERT_EQ(is_suid_binary(), 0);
}

static void test_escalate_setreuid_failure(void)
{
  mock_sys_reset();
  _mock_setreuid_ret = 1;
  _mock_chown_ret = 0;

  char *argv[] = { "cage", NULL };
  ASSERT_EQ(super_escalate(argv, 0, 1000, 0, 0), -1);
  ASSERT_EQ(_mock_setreuid_count, 1);
  ASSERT_EQ(_mock_chown_count, 0);
}

static void test_escalate_setregid_failure(void)
{
  mock_sys_reset();
  _mock_setreuid_ret = 0;
  _mock_setregid_ret = 1;
  _mock_chown_ret = 0;

  char *argv[] = { "cage", NULL };
  ASSERT_EQ(super_escalate(argv, 0, 1000, 0, 1000), -1);
  ASSERT_EQ(_mock_setreuid_count, 1);
  ASSERT_EQ(_mock_setregid_count, 1);
  ASSERT_EQ(_mock_chown_count, 0);
}

SUITE(super)
{
  RUN_TEST(fix_stat_fails);
  RUN_TEST(fix_already_ok);
  RUN_TEST(fix_chown_fails);
  RUN_TEST(fix_chown_then_chmod);
  RUN_TEST(fix_chmod_fails);
  RUN_TEST(escalate_suid_rejected);
  RUN_TEST(escalate_vfork_fails);
  RUN_TEST(escalate_sudo_fails);
  RUN_TEST(escalate_execvp_fails);
  RUN_TEST(escalate_as_root);
  RUN_TEST(is_suid_binary_check);
  RUN_TEST(escalate_setreuid_failure);
  RUN_TEST(escalate_setregid_failure);
}

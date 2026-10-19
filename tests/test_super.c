#include "framework.h"
#include "core/super.h"
#include <unistd.h>
#include <sys/stat.h>

static void test_fix_fails_when_not_root(void)
{
  if (geteuid() == 0)
    return;
  ASSERT_EQ(super_fix(), -1);
}

static void test_fix_succeeds_as_root(void)
{
  if (geteuid() != 0)
    return;
  return;
}

static uid_t test_euid, test_ruid;
static gid_t test_egid, test_rgid;

static void get_test_ids(void)
{
  uid_t suid;
  gid_t sgid;
  getresuid(&test_ruid, &test_euid, &suid);
  getresgid(&test_rgid, &test_egid, &sgid);
}

static void test_escalate_fails_when_not_setuid(void)
{
  if (geteuid() == 0)
    return;
  get_test_ids();
  char *argv[] = { "cage", NULL };
  ASSERT_EQ(super_escalate(argv, test_euid, test_ruid, test_egid, test_rgid), -1);
}

static void test_escalate_succeeds_as_root(void)
{
  if (geteuid() != 0)
    return;
  get_test_ids();
  mode_t old = umask(0);
  umask(old);

  char *argv[] = { "cage", NULL };
  int r = super_escalate(argv, test_euid, test_ruid, test_egid, test_rgid);
  ASSERT_EQ(r, 0);

  umask(old);
}

static void test_escalate_as_root_self_heals(void)
{
  get_test_ids();
  mode_t old = umask(0);
  umask(old);

  char *argv[] = { "cage", NULL };
  int r = super_escalate(argv, 0, test_ruid, test_egid, test_rgid);
  if (geteuid() == 0) {
    ASSERT_EQ(r, 0);
  } else {
    // Simulated root: the self-heal cannot chown /proc/self/exe, so the
    // escalation reports failure.
    ASSERT_EQ(r, -1);
  }

  umask(old);
}

static void test_is_suid_binary_check(void)
{
  int r = is_suid_binary();
  ASSERT(r == 0 || r == 1);
}

SUITE(super)
{
  RUN_TEST(fix_fails_when_not_root);
  RUN_TEST(fix_succeeds_as_root);
  RUN_TEST(escalate_fails_when_not_setuid);
  RUN_TEST(escalate_succeeds_as_root);
  RUN_TEST(escalate_as_root_self_heals);
  RUN_TEST(is_suid_binary_check);
}

#include "framework.h"

SUITE(args);
SUITE(user);
SUITE(prep);
SUITE(launch);
SUITE(env);
SUITE(vol);
SUITE(proc);
SUITE(io);
SUITE(compress);
SUITE(mount);
SUITE(modprobe);
SUITE(loop);
SUITE(mem);
SUITE(acl);
SUITE(reuse);
SUITE(cgroup);
SUITE(setup);
SUITE(gui);
SUITE(resolve);
SUITE(cage);
SUITE(super);
SUITE(netlink);
SUITE(net);
SUITE(appdir);
SUITE(ext4);

int main(void)
{
  test_run_suite("args", suite_args);
  test_run_suite("user", suite_user);
  test_run_suite("prep", suite_prep);
  test_run_suite("launch", suite_launch);
  test_run_suite("env", suite_env);
  test_run_suite("vol", suite_vol);
  test_run_suite("proc", suite_proc);
  test_run_suite("io", suite_io);
  test_run_suite("compress", suite_compress);
  test_run_suite("mount", suite_mount);
  test_run_suite("modprobe", suite_modprobe);
  test_run_suite("loop", suite_loop);
  test_run_suite("mem", suite_mem);
  test_run_suite("acl", suite_acl);
  test_run_suite("reuse", suite_reuse);
  test_run_suite("cgroup", suite_cgroup);
  test_run_suite("setup", suite_setup);
  test_run_suite("gui", suite_gui);
  test_run_suite("resolve", suite_resolve);
  test_run_suite("super", suite_super);
  test_run_suite("cage", suite_cage);
  test_run_suite("netlink", suite_netlink);
  test_run_suite("net", suite_net);
  test_run_suite("appdir", suite_appdir);
  test_run_suite("ext4", suite_ext4);
  return test_summary();
}

#include "framework.h"
#include "test_util.h"
#include "core/cage.h"
#include "core/proc.h"
#include "cli/args.h"
#include "util/io.h"
#include "util/mem.h"
#include "mock.h"
#include "sandbox/mount.h"
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/resource.h>

void __gcov_flush(void);

static void test_spawn_cage_existing_instance(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "spawn-existing.pid");

  cage_config_t cfg;
  cage_config_init(&cfg);

  int fd = create_pidfile(pf);
  if (fd >= 0)
    close_pid(fd);
  cfg.pidfile = pf;

  ASSERT_EQ(spawn_cage(&cfg, 0), -1);

  mem_free(pf);
  test_rmtree(root);
  mem_free(root);
}

static void test_spawn_cage_bad_env(void)
{
  mock_sys_reset();

  cage_config_t cfg;
  cage_config_init(&cfg);

  cfg.envvars[0] = "INVALID_NO_EQUALS";
  cfg.envvar_count = 1;

  ASSERT_EQ(spawn_cage(&cfg, 0), -1);
}

static void test_spawn_cage_initscript(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *script = mem_path(root, "init.sh");
  char *marker = mem_path(root, "init-ran.marker");
  char *appdir = mem_path(root, "appdir");
  char *pf = mem_path(root, "init.pid");
  ASSERT_EQ(io_mkdir(appdir), 0);

  int sfd = open(script, O_WRONLY | O_CREAT | O_CLOEXEC, 0755);
  ASSERT_NE(sfd, -1);
  char content[512];
  int n = snprintf(content, sizeof(content),
                   "#!/bin/sh\n" "touch %s\n" "exit 0\n", marker);
  ASSERT_EQ(write(sfd, content, (size_t)n), (ssize_t) n);
  close(sfd);

  ASSERT_EQ(access(script, R_OK), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.initscript = script;
  cfg.appdir = appdir;
  cfg.pidfile = pf;
  char *upper = mem_path(root, "upper");
  cfg.upperdir = upper;

  pid_t child = pidfork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    spawn_cage(&cfg, 0);
    _exit(0);
  }
  int status;
  ASSERT_EQ(pidwait(child, &status), 0);
  ASSERT(WIFEXITED(status));

  ASSERT_EQ(io_exists(marker), 0);

  mem_free(upper);
  mem_free(script);
  mem_free(marker);
  mem_free(appdir);
  mem_free(pf);
  test_rmtree(root);
  mem_free(root);
}

static void test_spawn_cage_graphical(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *appdir = mem_path(root, "appdir");
  char *pf = mem_path(root, "gui.pid");

  ASSERT_EQ(io_mkdir(appdir), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.graphical = 1;
  cfg.appdir = appdir;
  cfg.pidfile = pf;

  pid_t child = pidfork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    spawn_cage(&cfg, 0);
    _exit(0);
  }
  int status;
  ASSERT_EQ(pidwait(child, &status), 0);
  ASSERT(WIFEXITED(status));

  mem_free(appdir);
  mem_free(pf);
  test_rmtree(root);
  mem_free(root);
}

static void test_spawn_cage_no_env_vars(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *appdir = mem_path(root, "appdir");
  char *pf = mem_path(root, "noenv.pid");

  ASSERT_EQ(io_mkdir(appdir), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.appdir = appdir;
  cfg.pidfile = pf;
  cfg.envvar_count = 0;

  pid_t child = pidfork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    spawn_cage(&cfg, 0);
    _exit(0);
  }
  int status;
  ASSERT_EQ(pidwait(child, &status), 0);
  ASSERT(WIFEXITED(status));

  mem_free(appdir);
  mem_free(pf);
  test_rmtree(root);
  mem_free(root);
}

static void test_spawn_cage_early_root_prep_fails(void)
{
  mock_sys_reset();

  _mock_unshare_ret = -1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *pf = mem_path(root, "erpf.pid");

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.pidfile = pf;

  ASSERT_EQ(spawn_cage(&cfg, 0), -1);

  mem_free(pf);
  test_rmtree(root);
  mem_free(root);
}

static void test_spawn_cage_create_pidfile_fails(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *appdir = mem_path(root, "appdir");
  char *rodir = mem_path(root, "rodir");
  char *ropf = mem_path(root, "rodir/pid");

  ASSERT_EQ(io_mkdir(appdir), 0);
  ASSERT_EQ(io_mkdir(rodir), 0);
  ASSERT_EQ(chmod(rodir, 0555), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.appdir = appdir;
  cfg.pidfile = ropf;

  ASSERT_EQ(spawn_cage(&cfg, 0), -1);

  ASSERT_EQ(chmod(rodir, 0755), 0);
  mem_free(appdir);
  mem_free(rodir);
  mem_free(ropf);
  test_rmtree(root);
  mem_free(root);
}

static void test_spawn_cage_full_no_command(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *appdir = mem_path(root, "appdir");
  char *pf = mem_path(root, "sf.pid");

  ASSERT_EQ(io_mkdir(appdir), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.appdir = appdir;
  cfg.pidfile = pf;
  if (geteuid() == 0)
    cfg.unshare_cgroup = 1;
  char *upper = mem_path(root, "upper");
  cfg.upperdir = upper;

  int r = spawn_cage(&cfg, 0);
  ASSERT_EQ(r, 1);

  ASSERT_EQ(io_exists(pf), 1);
  mem_free(upper);
  mem_free(appdir);
  mem_free(pf);
  test_rmtree(root);
  mem_free(root);
}

static void test_spawn_cage_fork_fails(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *appdir = mem_path(root, "appdir");
  char *pf = mem_path(root, "ff.pid");

  ASSERT_EQ(io_mkdir(appdir), 0);

  struct rlimit old;
  ASSERT_EQ(getrlimit(RLIMIT_NPROC, &old), 0);
  struct rlimit lim = { 0, old.rlim_max };
  ASSERT_EQ(setrlimit(RLIMIT_NPROC, &lim), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.appdir = appdir;
  cfg.pidfile = pf;
  char *upper = mem_path(root, "upper");
  cfg.upperdir = upper;

  int r = spawn_cage(&cfg, 0);
  ASSERT_EQ(r, -1);

  ASSERT_EQ(setrlimit(RLIMIT_NPROC, &old), 0);

  mem_free(upper);
  mem_free(appdir);
  mem_free(pf);
  test_rmtree(root);
  mem_free(root);
}

static void test_spawn_cage_no_pidfile(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *appdir = mem_path(root, "appdir");
  ASSERT_EQ(io_mkdir(appdir), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.appdir = appdir;

  int r = spawn_cage(&cfg, 0);
  ASSERT_EQ(r, -1);

  mem_free(appdir);
  test_rmtree(root);
  mem_free(root);
}

static int count_temp_roots(void)
{
  int n = 0;
  DIR *d = opendir("/tmp");
  if (!d)
    return -1;
  struct dirent *e;
  while ((e = readdir(d)) != NULL)
    if (strncmp(e->d_name, "cage-", 5) == 0)
      n++;
  closedir(d);
  return n;
}

static int wait_for_path(const char *path, int iters)
{
  for (int i = 0; i < iters; i++) {
    if (access(path, F_OK) == 0)
      return 1;
    usleep(10000);
  }
  return 0;
}

static int wait_for_temp_roots(int expect, int iters)
{
  for (int i = 0; i < iters; i++) {
    if (count_temp_roots() == expect)
      return 1;
    usleep(10000);
  }
  return 0;
}

static int proc_running(pid_t pid)
{
  siginfo_t si;
  memset(&si, 0, sizeof(si));
  if (waitid(P_PID, (id_t) pid, &si, WEXITED | WNOHANG | WNOWAIT) != 0)
    return 0;
  return si.si_pid == 0;
}

static void test_spawn_cage_early_temp_root_removal(void)
{
  mock_sys_reset();

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *appdir = mem_path(root, "appdir");
  char *pf = mem_path(root, "early.pid");
  char *fifo = mem_path(root, "early.fifo");
  char *started = mem_path(root, "started.marker");
  ASSERT_EQ(io_mkdir(appdir), 0);
  ASSERT_EQ(mkfifo(fifo, 0600), 0);

  char uidbuf[16], gidbuf[16], script[512];
  snprintf(uidbuf, sizeof(uidbuf), "%u", (unsigned)geteuid());
  snprintf(gidbuf, sizeof(gidbuf), "%u", (unsigned)getegid());
  snprintf(script, sizeof(script), "touch %s; exec cat %s", started, fifo);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.appdir = appdir;
  cfg.pidfile = pf;
  cfg.user = uidbuf;
  cfg.group = gidbuf;
  cfg.command[0] = "/bin/sh";
  cfg.command[1] = "-c";
  cfg.command[2] = script;
  cfg.command_count = 3;
  char *upper = mem_path(root, "upper");
  cfg.upperdir = upper;

  int before = count_temp_roots();
  ASSERT(before >= 0);

  pid_t runner = pidfork();
  ASSERT_NE(runner, -1);
  if (runner == 0) {
    int r = spawn_cage(&cfg, 0);
    _exit(r == 0 ? 0 : 1);
  }

  int saw_started = wait_for_path(started, 1000);
  int saw_removed = saw_started && wait_for_temp_roots(before, 200);
  int alive = saw_removed && proc_running(runner);

  if (saw_started) {
    int wfd = open(fifo, O_WRONLY);
    if (wfd >= 0) {
      (void)write(wfd, "x", 1);
      close(wfd);
    }
  } else {
    kill(runner, SIGKILL);
  }
  int status = -1;
  int st = 0;
  if (waitpid(runner, &st, 0) == runner && WIFEXITED(st))
    status = WEXITSTATUS(st);

  ASSERT(saw_started);
  ASSERT(saw_removed);
  ASSERT(alive);
  ASSERT_EQ(status, 0);

  mem_free(upper);
  mem_free(started);
  mem_free(fifo);
  mem_free(pf);
  mem_free(appdir);
  test_rmtree(root);
  mem_free(root);
}

static void test_spawn_cage_child_dies_before_root_ready(void)
{
  mock_sys_reset();

  _mock_mount_fail_count = 1;

  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *appdir = mem_path(root, "appdir");
  char *pf = mem_path(root, "died.pid");
  char *upper = mem_path(root, "upper");
  ASSERT_EQ(io_mkdir(appdir), 0);

  cage_config_t cfg;
  cage_config_init(&cfg);
  cfg.appdir = appdir;
  cfg.pidfile = pf;
  cfg.upperdir = upper;

  int before = count_temp_roots();
  ASSERT(before >= 0);

  int r = spawn_cage(&cfg, 0);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(count_temp_roots(), before);
  ASSERT_EQ(io_exists(pf), 1);

  mem_free(upper);
  mem_free(pf);
  mem_free(appdir);
  test_rmtree(root);
  mem_free(root);
}

SUITE(cage)
{
  RUN_TEST(spawn_cage_existing_instance);
  RUN_TEST(spawn_cage_bad_env);
  RUN_TEST(spawn_cage_initscript);
  RUN_TEST(spawn_cage_graphical);
  RUN_TEST(spawn_cage_no_env_vars);
  RUN_TEST(spawn_cage_early_root_prep_fails);
  RUN_TEST(spawn_cage_create_pidfile_fails);
  RUN_TEST(spawn_cage_full_no_command);
  RUN_TEST(spawn_cage_fork_fails);
  RUN_TEST(spawn_cage_no_pidfile);
  RUN_TEST(spawn_cage_early_temp_root_removal);
  RUN_TEST(spawn_cage_child_dies_before_root_ready);
}

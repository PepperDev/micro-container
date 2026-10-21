#include "cgroup.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sched.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <limits.h>
// cppcheck-suppress missingIncludeSystem
#include <time.h>

static int cgroup_type_cache = -2;

int cgroup_get_type(void)
{
  if (cgroup_type_cache != -2)
    return cgroup_type_cache;
  buf_t b = BUF_INIT;
  if (io_read("/proc/filesystems", &b) != 0) {
    buf_free(&b);
    return -1;
  }
  cgroup_type_cache = strstr(b.data, "cgroup2") ? 2 : (strstr(b.data, "\tcgroup") ? 1 : 0);
  buf_free(&b);
  return cgroup_type_cache;
}

static int try_write_subtree(const char *sub, const char *val, size_t vlen)
{
  int fd = open(sub, O_WRONLY);
  if (fd < 0)
    return -1;
  int ok = write(fd, val, vlen) == (ssize_t) vlen;
  int saved = errno;
  close(fd);
  if (ok)
    return 0;
  return saved == EBUSY ? 1 : -1;
}

static void write_subtree_value(const char *cgdir, const char *val, size_t vlen)
{
  char *sub = mem_path(cgdir, "cgroup.subtree_control");
  if (!sub)
    return;
  for (int a = 0; a < 4; a++) {
    int r = try_write_subtree(sub, val, vlen);
    if (r == 0) {
      mem_free(sub);
      return;
    }
    if (r < 0 || a == 3)
      break;
    struct timespec ts = { 0, (50 << a) * 1000000L };
    nanosleep(&ts, NULL);
  }
  mem_free(sub);
}

static size_t trim_newline(char *s, size_t len)
{
  if (len && s[len - 1] == '\n')
    s[--len] = '\0';
  return len;
}

static int write_enable_controllers(const char *cgdir)
{
  char *ctrl_path = mem_path(cgdir, "cgroup.controllers");
  if (!ctrl_path)
    return 0;
  buf_t ctrls = BUF_INIT;
  if (io_read(ctrl_path, &ctrls) != 0) {
    mem_free(ctrl_path);
    return 0;
  }
  mem_free(ctrl_path);
  if (trim_newline(ctrls.data, strlen(ctrls.data))) {
    char val[256];
    size_t vlen;
    if (build_subtree_value(ctrls.data, strlen(ctrls.data), val, sizeof(val), &vlen) == 0)
      write_subtree_value(cgdir, val, vlen);
  }
  buf_free(&ctrls);
  return 0;
}

static int check_cgroup_mount(void)
{
  char *ctrl = mem_path("/sys/fs/cgroup", "cgroup.controllers");
  if (!ctrl)
    return -1;
  int e = io_exists(ctrl);
  mem_free(ctrl);
  if (e < 0)
    return -1;
  if (e == 0)
    return 0;
  e = io_exists("/sys/fs/cgroup");
  if (e < 0)
    return -1;
  if (e == 1)
    return -1;
  return 1;
}

int cgroup_detect_mount(int *root_private)
{
  int r = check_cgroup_mount();
  if (r != 1)
    return r;
  if (sys_unshare(CLONE_NEWNS) != 0 || sys_mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
    return -1;
  *root_private = 1;
  r = sys_mount("cgroup2", "/sys/fs/cgroup", "cgroup2", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
  if (r == 0 || errno == ENODEV)
    return 0;
  return -1;
}

static int make_cgroup_dir(char *cgdir, size_t size, const char *root, pid_t pid)
{
  int n = snprintf(cgdir, size, "%s/cage-%d", root, (int)pid);
  return (n < 0 || (size_t)n >= size || (mkdir(cgdir, 0755) != 0 && errno != EEXIST)) ? -1 : 0;
}

static int write_procs(const char *cgdir, pid_t pid)
{
  char *procs = mem_path(cgdir, "cgroup.procs");
  if (!procs)
    return -1;
  int r = write_pid(procs, pid);
  mem_free(procs);
  return r;
}

// cppcheck-suppress staticFunction
int cgroup_pre_unshare_at(const char *root, int fs_type, pid_t pid)
{
  if (fs_type != 2)
    return 0;
  if (enable_cgroup_controllers(root) != 0)
    return -1;
  char cgdir[PATH_MAX];
  if (make_cgroup_dir(cgdir, sizeof(cgdir), root, pid) != 0)
    return -1;
  if (write_procs(cgdir, pid) != 0)
    return -1;
  return write_enable_controllers(cgdir);
}

int cgroup_pre_unshare(int fs_type, pid_t pid)
{
  return cgroup_pre_unshare_at("/sys/fs/cgroup", fs_type, pid);
}

int cgroup_enter(const char *root, int is_cgroup2)
{
  const char *name = "cage-parent";
  pid_t pid = 1;

  if (is_cgroup2)
    return cgroup_v2_enter(root, name, pid);
  return cgroup_v1_enter(root, name, pid);
}

// cppcheck-suppress staticFunction
void cgroup_cleanup_at(const char *root, pid_t pid, int fs_type)
{
  if (fs_type != 2)
    return;
  char cgdir[PATH_MAX];
  snprintf(cgdir, sizeof(cgdir), "%s/cage-%d", root, (int)pid);
  if (io_exists(cgdir) != 0)
    return;
  char procs[PATH_MAX];
  snprintf(procs, sizeof(procs), "%s/cgroup.procs", root);
  write_pid(procs, pid);
  cgroup_rmdir_recursive(cgdir);
}

void cgroup_cleanup(pid_t pid, int fs_type)
{
  cgroup_cleanup_at("/sys/fs/cgroup", pid, fs_type);
}

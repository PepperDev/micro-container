#include "proc.h"
#include "util/io.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <sched.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/wait.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/file.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <signal.h>
// cppcheck-suppress missingIncludeSystem
#include <time.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>

#define PID_DIR "/run/cage"

pid_t pidfork(void)
{
  pid_t pid = fork();
  if (pid < 0)
    fprintf(stderr, "cage: unable to fork process: %s\n", strerror(errno));
  return pid;
}

char *compute_pidfile(const char *name, size_t *len)
{
  if (!name) {
    if (len)
      *len = sizeof(PID_DIR "/pid") - 1;
    return mem_cat(PID_DIR, sizeof(PID_DIR) - 1, "/pid", 4, MEM_CAT_END);
  }

  size_t nl = strlen(name);
  char *pf = mem_cat(PID_DIR, sizeof(PID_DIR) - 1, "/", 1, name, nl, ".pid", 4, MEM_CAT_END);
  if (len)
    *len = sizeof(PID_DIR) - 1 + 1 + nl + 4;
  return pf;
}

static int ensure_dir(const char *dir)
{
  int e = io_exists(dir);
  if (e == 1)
    return io_mkdir(dir);
  if (e != 0)
    return -1;
  if (io_isdir(dir) != 0)
    return -1;
  return 0;
}

static int open_and_lock(const char *path)
{
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0)
    return -1;
  if (flock(fd, LOCK_EX) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

int create_pidfile(const char *path)
{
  const char *slash = strrchr(path, '/');
  if (slash) {
    size_t dlen = slash - path;
    char dir[4096];
    if (dlen >= sizeof(dir))
      return -1;
    memcpy(dir, path, dlen);
    dir[dlen] = '\0';
    if (ensure_dir(dir) != 0)
      return -1;
  }
  int fd = open_and_lock(path);
  if (fd < 0)
    return -1;
  return fd;
}

static long parse_pid(const char *buf)
{
  char *end;
  errno = 0;
  long pid = strtol(buf, &end, 10);
  if (end == buf || *end != '\0' || errno != 0)
    return -1;
  return pid;
}

/* cppcheck-suppress staticFunction */
pid_t readpid(const char *path, int *fd)
{
  int f = open(path, O_RDONLY | O_CLOEXEC);
  if (f < 0)
    return -1;
  if (flock(f, LOCK_EX) != 0) {
    close(f);
    return -1;
  }
  char buf[64];
  ssize_t n = read(f, buf, sizeof(buf) - 1);
  if (n < 0) {
    close(f);
    return -1;
  }
  buf[n] = '\0';
  long pid = parse_pid(buf);
  if (pid < 0) {
    close(f);
    return -1;
  }
  if (fd)
    *fd = f;
  return (pid_t) pid;
}

int close_pid(int fd)
{
  flock(fd, LOCK_UN);
  close(fd);
  return 0;
}

int pidwait(pid_t pid, int *status)
{
  if (waitpid(pid, status, 0) < 0) {
    if (errno == ECHILD)
      return 0;
    fprintf(stderr, "cage: waitpid failed\n");
    return -1;
  }
  return 0;
}

int exit_code_from_status(int status)
{
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return 1;
}

/* cppcheck-suppress staticFunction */
int pidexists(pid_t pid)
{
  if (kill(pid, 0) == 0)
    return 0;
  if (errno == ESRCH)
    return 1;
  return -1;
}

static int waitpid_graceful(pid_t pid)
{
  for (int i = 0; i < 50; i++) {
    int ret = pidexists(pid);
    if (ret == 1)
      return 0;
    if (ret < 0)
      return -1;
    struct timespec ts = { 0, 100000000 };
    nanosleep(&ts, NULL);
  }
  if (kill(pid, SIGKILL) != 0) {
    if (errno == ESRCH)
      return 0;
    return -1;
  }
  return pidwait(pid, NULL);
}

static const char *resolve_pidfile(const char *name, const char *pidfile, char **free_pf)
{
  *free_pf = NULL;
  if (pidfile)
    return pidfile;
  char *resolved = compute_pidfile(name, NULL);
  *free_pf = resolved;
  return resolved;
}

static int kill_gracefully(pid_t pid)
{
  if (kill(pid, SIGTERM) != 0) {
    if (errno == ESRCH)
      return 0;
    return -1;
  }
  return waitpid_graceful(pid);
}

static int ns_flag(const char *ns)
{
  if (strcmp(ns, "pid") == 0)
    return CLONE_NEWPID;
  if (strcmp(ns, "cgroup") == 0)
    return CLONE_NEWCGROUP;
  if (strcmp(ns, "net") == 0)
    return CLONE_NEWNET;
  return CLONE_NEWNS;
}

static int join_ns(pid_t target, const char *ns)
{
  char path[64];
  int n = snprintf(path, sizeof(path), "/proc/%d/ns/%s", (int)target, ns);
  if (n < 0 || (size_t)n >= sizeof(path))
    return -1;
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  if (sys_setns(fd, ns_flag(ns)) != 0) {
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

int join_namespaces(pid_t target)
{
  if (join_ns(target, "pid") != 0)
    return -1;
  if (join_ns(target, "cgroup") != 0)
    return -1;
  if (join_ns(target, "net") != 0)
    return -1;
  return join_ns(target, "mnt");
}

static int pid_zombie(pid_t pid)
{
  char path[64];
  buf_t b = BUF_INIT;
  snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
  if (io_read_quiet(path, &b) != 0 || buf_grow(&b, 1) != 0) {
    buf_free(&b);
    return 1;
  }
  b.data[b.len] = '\0';
  const char *rp = strrchr(b.data, ')');
  int z = 1;
  if (rp && rp[1] == ' ' && rp[2] != '\0')
    z = (rp[2] == 'Z');
  buf_free(&b);
  return z;
}

static int pid_joinable(pid_t pid)
{
  return pidexists(pid) == 0 && !pid_zombie(pid);
}

int killpid(const char *name, const char *pidfile)
{
  char *free_pf = NULL;
  pidfile = resolve_pidfile(name, pidfile, &free_pf);
  if (!pidfile)
    return -1;
  int r = io_exists(pidfile);
  if (r == 1) {
    mem_free(free_pf);
    return 0;
  }
  if (r != 0) {
    mem_free(free_pf);
    return -1;
  }
  int fd;
  pid_t pid = readpid(pidfile, &fd);
  if (pid < 0) {
    mem_free(free_pf);
    return -1;
  }
  io_unlink_quiet(pidfile);
  close_pid(fd);
  if (!pid_zombie(pid))
    kill_gracefully(pid);
  mem_free(free_pf);
  return 0;
}

pid_t detect_existing(const char *name, const char *pidfile)
{
  char *free_pf = NULL;
  if (!pidfile) {
    free_pf = compute_pidfile(name, NULL);
    if (!free_pf)
      return -1;
    pidfile = free_pf;
  }
  int ok = io_exists(pidfile);
  if (ok != 0) {
    mem_free(free_pf);
    return -(ok != 1);
  }
  int fd;
  pid_t pid = readpid(pidfile, &fd);
  if (pid >= 0) {
    close_pid(fd);
    if (pid_joinable(pid)) {
      mem_free(free_pf);
      return pid;
    }
  }
  io_unlink_quiet(pidfile);
  mem_free(free_pf);
  return 0;
}

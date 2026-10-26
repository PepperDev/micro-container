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

#define CGROUP_MAX_CTRL 32

int write_pid(const char *path, pid_t pid)
{
  int fd = open(path, O_WRONLY);
  if (fd < 0) {
    fprintf(stderr, "cage: open %s failed\n", path);
    return -1;
  }
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "%d\n", (int)pid);
  if (n <= 0 || (size_t)n >= sizeof(buf)) {
    close(fd);
    return -1;
  }
  ssize_t w = write(fd, buf, (size_t)n);
  close(fd);
  return w == n ? 0 : -1;
}

// cppcheck-suppress staticFunction
char *advance_line(char *p)
{
  char *nl = strchr(p, '\n');
  return nl ? nl + 1 : p + strlen(p);
}

// cppcheck-suppress staticFunction
int __read_cgroups_file(buf_t *b)
{
  return io_read("/proc/cgroups", b);
}

weak_alias(__read_cgroups_file, read_cgroups_file);

int read_proc_cgroups(char (*names)[64])
{
  buf_t b = BUF_INIT;
  if (read_cgroups_file(&b))
    return -1;
  int n = 0;
  char *p = b.data;
  while (*p) {
    if (*p != '#') {
      const char *t = strchr(p, '\t');
      if (t) {
        size_t l = (size_t)(t - p);
        l = l > 63 ? 63 : l;
        memcpy(names[n], p, l);
        names[n][l] = '\0';
        n++;
      }
    }
    p = advance_line(p);
  }
  buf_free(&b);
  return n;
}

// cppcheck-suppress staticFunction
int cgroup_v1_enter_one(const char *cg, const char *ctrl, const char *name, pid_t pid)
{
  char *dir = mem_path(cg, ctrl);
  if (!dir)
    return -1;
  char *cgdir = mem_path(dir, name);
  if (!cgdir)
    goto cleanup;
  if (io_mkdir(cgdir) != 0)
    goto cleanup;
  char *tasks = mem_path(cgdir, "cgroup.procs");
  if (!tasks)
    goto cleanup;
  int r = write_pid(tasks, pid);
  mem_free(tasks);
  mem_free(cgdir);
  mem_free(dir);
  return r;
 cleanup:
  mem_free(cgdir);
  mem_free(dir);
  return -1;
}

int build_subtree_value(const char *in, size_t len, char *out, size_t cap, size_t *out_len)
{
  if (len == 0 || 2 * len + 2 > cap)
    return -1;
  out[0] = '+';
  size_t w = 1;
  for (size_t i = 0; i < len; i++) {
    if (in[i] == ' ') {
      out[w++] = ' ';
      out[w++] = '+';
    } else {
      out[w++] = in[i];
    }
  }
  out[w] = '\0';
  *out_len = w;
  return 0;
}

// cppcheck-suppress staticFunction
int write_subtree(const char *cg, const char *val, size_t vlen)
{
  char *p = mem_path(cg, "cgroup.subtree_control");
  if (!p)
    return -1;
  int fd = open(p, O_WRONLY);
  int r = -1;
  if (fd >= 0) {
    if (write(fd, val, vlen) == (ssize_t) vlen)
      r = 0;
    close(fd);
  }
  mem_free(p);
  return r;
}

static size_t trim_trailing_newline(char *s)
{
  size_t len = strlen(s);
  if (len && s[len - 1] == '\n')
    s[--len] = '\0';
  return len;
}

int enable_cgroup_controllers(const char *cg)
{
  buf_t ctrls = BUF_INIT;
  char *p = mem_path(cg, "cgroup.controllers");
  if (!p)
    return -1;
  if (io_read(p, &ctrls) != 0) {
    mem_free(p);
    return 0;
  }
  mem_free(p);

  size_t len = trim_trailing_newline(ctrls.data);
  char val[256];
  size_t vlen;
  int r = 0;
  if (build_subtree_value(ctrls.data, len, val, sizeof(val), &vlen) == 0)
    r = write_subtree(cg, val, vlen);
  buf_free(&ctrls);
  return r;
}

int cgroup_v2_enter(const char *root, const char *name, pid_t pid)
{
  char *cg = mem_path(root, "sys/fs/cgroup");
  if (!cg)
    return -1;

  char *dir = mem_path(cg, name);
  mem_free(cg);
  if (!dir)
    return -1;

  if (io_mkdir(dir)) {
    mem_free(dir);
    return -1;
  }
  char *procs = mem_path(dir, "cgroup.procs");
  if (!procs) {
    mem_free(dir);
    return -1;
  }
  int r = write_pid(procs, pid);
  mem_free(procs);
  mem_free(dir);
  return r;
}

int cgroup_v1_enter(const char *root, const char *name, pid_t pid)
{
  char ctrls[CGROUP_MAX_CTRL][64];
  int n = read_proc_cgroups(ctrls);
  if (n < 0)
    return -1;

  char *cg = mem_path(root, "sys/fs/cgroup");
  if (!cg)
    return -1;

  for (int i = 0; i < n; i++) {
    if (strncmp(ctrls[i], "name=", 5) == 0)
      continue;
    if (cgroup_v1_enter_one(cg, ctrls[i], name, pid) != 0) {
      mem_free(cg);
      return -1;
    }
  }
  mem_free(cg);
  return 0;
}

#include "cgroup.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>

#define CGROUP_MAX_CTRL 32

static int ensure_dir(const char *path)
{
  int e = io_exists(path);
  if (e < 0)
    return -1;
  if (e == 1)
    return io_mkdir(path);
  return 0;
}

// cppcheck-suppress staticFunction
int mount_cgroup_v2(const char *root)
{
  char *cg = mem_path(root, "sys/fs/cgroup");
  if (!cg)
    return -1;
  if (ensure_dir(cg) != 0) {
    mem_free(cg);
    return -1;
  }
  if (sys_mount("cgroup2", cg, "cgroup2", MS_NOSUID | MS_NOEXEC | MS_NODEV, "nsdelegate") != 0) {
    fprintf(stderr, "cage: mount cgroup2 failed\n");
    mem_free(cg);
    return -1;
  }
  mem_free(cg);
  return 0;
}

static int mount_one_v1(const char *p, const char *name)
{
  if (io_mkdir(p) != 0)
    return -1;
  if (sys_mount("cgroup", p, "cgroup", MS_NOSUID | MS_NOEXEC | MS_NODEV, name) != 0) {
    fprintf(stderr, "cage: mount cgroup %s failed\n", name);
    return -1;
  }
  return 0;
}

// cppcheck-suppress staticFunction
int mount_cgroup_tmpfs(const char *cg)
{
  if (io_mkdir(cg) != 0)
    return -1;
  if (sys_mount("tmpfs", cg, "tmpfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) != 0) {
    fprintf(stderr, "cage: mount tmpfs for cgroup failed\n");
    return -1;
  }
  return 0;
}

static int mount_one_cgroup_v1(const char *cg, const char *ctrl)
{
  char *p = mem_path(cg, ctrl);
  if (!p)
    return -1;
  int r = mount_one_v1(p, ctrl);
  mem_free(p);
  return r;
}

// cppcheck-suppress staticFunction
int mount_cgroup_v1(const char *root)
{
  char *cg = mem_path(root, "sys/fs/cgroup");
  if (!cg)
    return -1;
  if (mount_cgroup_tmpfs(cg) != 0)
    goto fail;
  char ctrls[CGROUP_MAX_CTRL][64];
  int count = read_proc_cgroups(ctrls);
  if (count < 0)
    goto fail;
  for (int i = 0; i < count; i++) {
    if (mount_one_cgroup_v1(cg, ctrls[i]) != 0)
      goto fail;
  }
  mem_free(cg);
  return 0;
 fail:
  mem_free(cg);
  return -1;
}

int mount_cgroups(const char *root, int has_cgroup)
{
  if (has_cgroup == -2)
    return 0;
  if (has_cgroup >= 2)
    return mount_cgroup_v2(root);
  if (has_cgroup == 1)
    return mount_cgroup_v1(root);
  fprintf(stderr, "cage: no cgroup filesystem found\n");
  return -1;
}

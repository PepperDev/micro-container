#include "mount.h"
#include "setup.h"
#include "appdir.h"
#include "vol.h"
#include "ext4fix.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
#include "sandbox/cgroup.h"
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>

/* cppcheck-suppress staticFunction */
void strip_trailing_slash(char *p)
{
  size_t len = strlen(p);
  while (len > 1 && p[len - 1] == '/')
    p[--len] = '\0';
}

static int ensure_dir(const char *path)
{
  int e = io_exists(path);
  if (e == 1)
    return io_mkdir(path);
  if (e != 0)
    return -1;
  int d = io_isdir(path);
  if (d == 1) {
    fprintf(stderr, "cage: %s exists but is not a directory\n", path);
    return -1;
  }
  if (d != 0)
    return -1;
  return 0;
}

int mount_bind(const char *root, const char *sub, const char *src)
{
  char *p = mem_path(root, sub);
  if (!p)
    return -1;
  if (ensure_dir(p) != 0) {
    mem_free(p);
    return -1;
  }
  int ok = sys_mount(src, p, NULL, MS_BIND | MS_REC, NULL) == 0;
  if (!ok)
    fprintf(stderr, "cage: mount %s failed\n", sub);
  mem_free(p);
  return ok ? 0 : -1;
}

/* cppcheck-suppress staticFunction */
int mount_fs_type(const char *root, const char *sub, const char *type, unsigned long flags, const char *data)
{
  char *p = mem_path(root, sub);
  if (!p)
    return -1;
  if (ensure_dir(p) != 0) {
    mem_free(p);
    return -1;
  }
  int ok = sys_mount(type, p, type, flags, data) == 0;
  if (!ok)
    fprintf(stderr, "cage: mount %s failed\n", sub);
  mem_free(p);
  return ok ? 0 : -1;
}

/* cppcheck-suppress staticFunction */
int mount_volumes(const char *root, const cage_config_t *cfg)
{
  for (size_t i = 0; i < cfg->volume_count; i++) {
    vol_t v;
    if (parse_vol(&v, cfg->volumes[i]) != 0) {
      fprintf(stderr, "cage: invalid volume spec: %s\n", cfg->volumes[i]);
      return -1;
    }
    if (mount_user_volume(&v, root) != 0) {
      fprintf(stderr, "cage: failed to mount volume: %s\n", cfg->volumes[i]);
      return -1;
    }
  }
  return 0;
}

/* cppcheck-suppress staticFunction */
int mount_dev(const char *root)
{
  if (mount_bind(root, "dev", "/dev") != 0)
    return -1;
  return mount_bind(root, "dev/pts", "/dev/pts");
}

static int mount_virtual_fses(const char *root)
{
  unsigned long f = MS_NOSUID | MS_NOEXEC | MS_NODEV;
  if (mount_fs_type(root, "proc", "proc", f, NULL) != 0)
    return -1;
  return mount_fs_type(root, "sys", "sysfs", f, NULL);
}

static int mount_tmpfses(const char *root)
{
  unsigned long nd = MS_NOSUID | MS_NODEV | MS_NOATIME;
  unsigned long ne = MS_NOSUID | MS_NOEXEC;
  if (mount_fs_type(root, "run", "tmpfs", ne, "mode=0755") != 0)
    return -1;
  if (mount_fs_type(root, "tmp", "tmpfs", nd, NULL) != 0)
    return -1;
  return mount_fs_type(root, "var/tmp", "tmpfs", nd, NULL);
}

int mount_all(const char *root, const cage_config_t *cfg, int has_cgroup)
{
  if (mount_virtual_fses(root) != 0)
    return -1;
  if (mount_dev(root) != 0)
    return -1;
  if (mount_tmpfses(root) != 0)
    return -1;
  if (mount_cgroups(root, has_cgroup) != 0)
    return -1;
  if (mount_misc(root, cfg) != 0)
    return -1;
  return mount_volumes(root, cfg);
}

int early_setup_appdir(const cage_config_t *cfg, appdir_t *dirs)
{
  if (resolve_appdir(cfg, dirs) != 0)
    return -1;
  strip_trailing_slash(dirs->lowerdir);
  strip_trailing_slash(dirs->upperdir);
  strip_trailing_slash(dirs->workdir);
  return handle_same_fs(cfg, dirs);
}

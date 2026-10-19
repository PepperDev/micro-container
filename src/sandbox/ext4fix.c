#include "ext4fix.h"
#include "ext4/ext4.h"
#include "pkg/modprobe.h"
#include "sandbox/loop.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

#define LOOP_SUFFIX     "-loop"
#define LOOP_SUFFIX_LEN 5

typedef struct {
  char loopdir[4096];
  char loop_img[4096];
  char loop_appdir[4096];
} loop_t;

/* cppcheck-suppress staticFunction */
char *grandparent(const char *path)
{
  char *p = mem_strdup(path);
  if (!p)
    return NULL;
  char *slash = strrchr(p, '/');
  if (!slash || slash == p) {
    mem_free(p);
    return NULL;
  }
  *slash = '\0';
  slash = strrchr(p, '/');
  if (slash > p)
    *slash = '\0';
  return p;
}

static int resolve_existing_parent(char *path)
{
  for (;;) {
    int r = io_exists(path);
    if (r < 0)
      return -1;
    if (r == 0)
      return 0;
    char *slash = strrchr(path, '/');
    if (!slash)
      return 0;
    if (slash == path) {
      path[1] = '\0';
      return 0;
    }
    *slash = '\0';
  }
}

static int stat_dev(const char *a, const char *b, dev_t *dev)
{
  struct stat sa, sb;
  if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
    return -1;
  if (dev)
    *dev = sa.st_dev;
  return sa.st_dev == sb.st_dev;
}

// cppcheck-suppress staticFunction
int __same_fs_detect(const char *lowerdir, const char *upperdir, dev_t *lower_dev)
{
  char *gp = grandparent(upperdir);
  if (!gp)
    return 0;
  if (resolve_existing_parent(gp) != 0) {
    mem_free(gp);
    return -1;
  }
  int r = stat_dev(lowerdir, gp, lower_dev);
  mem_free(gp);
  return r;
}

weak_alias(__same_fs_detect, same_fs_detect);

static int appdir_parent(const char *appdir, char *parent, size_t size)
{
  const char *slash = strrchr(appdir, '/');
  size_t plen = (!slash || slash == appdir) ? strlen(appdir) : (size_t)(slash - appdir);
  if (plen >= size)
    return -1;
  memcpy(parent, appdir, plen);
  parent[plen] = '\0';
  return 0;
}

/* cppcheck-suppress staticFunction */
int derive_loopdir(const char *appdir, char *loopdir, size_t size)
{
  char parent[4096];
  if (appdir_parent(appdir, parent, sizeof(parent)) != 0)
    return -1;
  const char *sfx = LOOP_SUFFIX;
  size_t plen = strlen(parent);
  if (plen >= LOOP_SUFFIX_LEN && memcmp(parent + plen - LOOP_SUFFIX_LEN, LOOP_SUFFIX, LOOP_SUFFIX_LEN) == 0)
    sfx = "";
  int n = snprintf(loopdir, size, "%s%s", parent, sfx);
  if (n < 0 || (size_t)n >= size)
    return -1;
  return 0;
}

/* cppcheck-suppress staticFunction */
int path_under(const char *path, const char *dir)
{
  size_t dl = strlen(dir);
  if (strncmp(path, dir, dl) != 0)
    return 0;
  return path[dl] == '\0' || path[dl] == '/';
}

static const char *appdir_leaf(const char *appdir)
{
  const char *slash = strrchr(appdir, '/');
  return slash ? slash + 1 : "default";
}

static int loop_build(const char *base, const char *leaf, loop_t *lp)
{
  if (snprintf(lp->loopdir, sizeof(lp->loopdir), "%s", base) >= (int)sizeof(lp->loopdir))
    return -1;
  if (snprintf(lp->loop_img, sizeof(lp->loop_img), "%s.img", lp->loopdir) >= (int)sizeof(lp->loop_img))
    return -1;
  if (snprintf(lp->loop_appdir, sizeof(lp->loop_appdir), "%s/%s", lp->loopdir, leaf) >= (int)sizeof(lp->loop_appdir))
    return -1;
  return 0;
}

static int loop_fill(const char *appdir, loop_t *lp)
{
  char loopdir[4096];
  if (derive_loopdir(appdir, loopdir, sizeof(loopdir)) != 0)
    return -1;
  return loop_build(loopdir, appdir_leaf(appdir), lp);
}

static int mkdir_checked(const char *path)
{
  if (io_mkdir(path) != 0) {
    fprintf(stderr, "cage: unable to mkdir %s\n", path);
    return -1;
  }
  return 0;
}

static int set_loop_appdir(appdir_t *dirs, const loop_t *lp, int keep_work)
{
  if (snprintf(dirs->appdir, sizeof(dirs->appdir), "%s", lp->loop_appdir) >= (int)sizeof(dirs->appdir))
    return -1;
  if (snprintf(dirs->upperdir, sizeof(dirs->upperdir), "%s/upper", dirs->appdir) >= (int)sizeof(dirs->upperdir))
    return -1;
  if (!keep_work
      && snprintf(dirs->workdir, sizeof(dirs->workdir), "%s/work", dirs->appdir) >= (int)sizeof(dirs->workdir))
    return -1;
  return 0;
}

// cppcheck-suppress unusedFunction
int make_relocated_dirs(appdir_t *dirs, const char *mnt)
{
  loop_t lp;
  if (loop_build(mnt, appdir_leaf(dirs->appdir), &lp) != 0)
    return -1;
  if (set_loop_appdir(dirs, &lp, 0) != 0)
    return -1;
  if (mkdir_checked(dirs->appdir) != 0 || mkdir_checked(dirs->upperdir) != 0 || mkdir_checked(dirs->workdir) != 0)
    return -1;
  dirs->dirs_created = 1;
  return 0;
}

static int setup_ext4_img(const char *img)
{
  if (ext4_create_img(img) != 0 && errno != EEXIST) {
    fprintf(stderr, "cage: failed to create ext4 image %s\n", img);
    return -1;
  }
  return 0;
}

static int setup_ext4_loopback(appdir_t * dirs, const loop_t * lp, int keep_work);

static int use_provisioned_loopback(appdir_t *dirs, const loop_t *lp, dev_t root_dev, int keep_work)
{
  struct stat mnt_st;
  if (stat(lp->loopdir, &mnt_st) != 0)
    return -1;
  if (mnt_st.st_dev == root_dev) {
    fprintf(stderr, "cage: ext4 loopback on same filesystem\n");
    return -1;
  }
  if (set_loop_appdir(dirs, lp, keep_work) != 0)
    return -1;
  dirs->dirs_created = 1;
  return 0;
}

static int ensure_ext4_mounted(appdir_t *dirs, const loop_t *lp, dev_t root_dev, int keep_work)
{
  struct stat mnt_st;
  if (stat(lp->loopdir, &mnt_st) == 0 && mnt_st.st_dev != root_dev) {
    return set_loop_appdir(dirs, lp, keep_work);
  }
  if (stat(lp->loopdir, &mnt_st) != 0) {
    if (io_mkdir(lp->loopdir) != 0) {
      fprintf(stderr, "cage: failed to create mount point %s\n", lp->loopdir);
      return -1;
    }
  }
  return setup_ext4_loopback(dirs, lp, keep_work);
}

static int setup_ext4_loopback(appdir_t *dirs, const loop_t *lp, int keep_work)
{
  fprintf(stderr, "cage: lowerdir and upperdir on same filesystem; using ext4 loopback\n");
  if (setup_ext4_img(lp->loop_img) != 0)
    return -1;
  char dev[64];
  int loop_fd = io_loop(dev, sizeof(dev), lp->loop_img);
  if (loop_fd < 0)
    return -1;
  if (sys_mount(dev, lp->loopdir, "ext4", 0, NULL) != 0) {
    fprintf(stderr, "cage: failed to mount ext4 loopback\n");
    close(loop_fd);
    return -1;
  }
  close(loop_fd);
  if (set_loop_appdir(dirs, lp, keep_work) != 0)
    return -1;
  return mkdir_checked(dirs->appdir);
}

static int loop_dirs_set(appdir_t *dirs, const cage_config_t *cfg, const loop_t *lp)
{
  if (cfg->workdir && !path_under(cfg->workdir, lp->loopdir)) {
    fprintf(stderr, "cage: custom workdir %s is not under the derived loopdir %s\n", cfg->workdir, lp->loopdir);
    return -1;
  }
  if (snprintf(dirs->upperdir, sizeof(dirs->upperdir), "%s/upper", lp->loop_appdir) >= (int)sizeof(dirs->upperdir))
    return -1;
  return 0;
}

int handle_same_fs(const cage_config_t *cfg, appdir_t *dirs)
{
  dev_t lower_dev;
  int r = same_fs_detect(dirs->lowerdir, dirs->upperdir, &lower_dev);
  if (r <= 0)
    return r;
  if (cfg->upperdir) {
    fprintf(stderr, "cage: custom upperdir %s shares the lowerdir filesystem\n", cfg->upperdir);
    return -1;
  }
  loop_t lp;
  if (loop_fill(dirs->appdir, &lp) != 0)
    return -1;
  if (loop_dirs_set(dirs, cfg, &lp) != 0)
    return -1;
  if (io_exists(dirs->upperdir) == 0)
    return use_provisioned_loopback(dirs, &lp, lower_dev, cfg->workdir != NULL);
  return ensure_ext4_mounted(dirs, &lp, lower_dev, cfg->workdir != NULL);
}

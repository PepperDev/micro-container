#include "overlay.h"
#include "util/mem.h"
#include "util/io.h"
#include "util/syscall.h"
#include "pkg/modprobe.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/statfs.h>

#ifndef OVERLAYFS_SUPER_MAGIC
#define OVERLAYFS_SUPER_MAGIC 0x794c7630
#endif

static int needs_escape(char c)
{
  return c == '\\' || c == ',';
}

static size_t count_escapes(const char *s, size_t len)
{
  size_t n = 0;
  for (size_t i = 0; i < len; i++)
    if (needs_escape(s[i]))
      n++;
  return n;
}

static char *append_comp(char *p, const char *pfx, const char *s, size_t len)
{
  while (*pfx)
    *p++ = *pfx++;
  for (size_t i = 0; i < len; i++) {
    if (needs_escape(s[i]))
      *p++ = '\\';
    *p++ = s[i];
  }
  return p;
}

/* cppcheck-suppress staticFunction */
char *build_overlay_opts(const char *lower, const char *upper, const char *work)
{
  size_t llen = strlen(lower), ulen = strlen(upper), wlen = strlen(work);
  size_t extra = count_escapes(lower, llen) + count_escapes(upper, ulen) + count_escapes(work, wlen);
  size_t total = 33 + llen + ulen + wlen + extra * 3;
  char *opts = mem_alloc(total);
  if (!opts)
    return NULL;
  char *p = opts;
  p = append_comp(p, "lowerdir=", lower, llen);
  p = append_comp(p, ",upperdir=", upper, ulen);
  p = append_comp(p, ",workdir=", work, wlen);
  *p = '\0';
  return opts;
}

static int try_mount_overlay(const char *root, const char *opts)
{
  int r = sys_mount("overlay", root, "overlay", 0, opts);
  if (r == 0 || errno != ENODEV)
    return r;
  if (modprobe_load("overlay") != 0)
    return -1;
  return sys_mount("overlay", root, "overlay", 0, opts);
}

static int overlay_mount_result(int r, const char *root, const char *upper)
{
  if (r == 0)
    return 0;
  int err = errno;
  struct statfs sfs;
  if (sys_statfs(upper, &sfs) == 0 && ((unsigned long)sfs.f_type == (unsigned long)OVERLAYFS_SUPER_MAGIC))
    fprintf(stderr, "cage: appdir/upperdir %s is on overlayfs; overlay-on-overlay is not supported\n", upper);
  else
    fprintf(stderr, "cage: unable to mount overlay at %s: %s\n", root, strerror(err));
  return -1;
}

int prepare_overlay(const char *root, const appdir_t *dirs)
{
  if (!dirs->dirs_created) {
    char *parent = mem_strdup(dirs->upperdir);
    char *slash = strrchr(parent, '/');
    if (slash)
      *slash = '\0', io_mkdir(parent);
    mem_free(parent);
    if (io_mkdir(dirs->upperdir) != 0 || io_mkdir(dirs->workdir) != 0)
      return -1;
  }
  char *opts = build_overlay_opts(dirs->lowerdir, dirs->upperdir, dirs->workdir);
  if (!opts)
    return -1;
  int r = try_mount_overlay(root, opts);
  mem_free(opts);
  return overlay_mount_result(r, root, dirs->upperdir);
}

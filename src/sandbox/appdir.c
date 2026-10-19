#include "appdir.h"
#include "cli/args.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>

#define APPDIR_BASE  "/var/lib/cage"

static void join(char *dst, size_t size, const char *base, const char *leaf)
{
  size_t blen = strlen(base);
  size_t llen = strlen(leaf);
  size_t need = blen + 1 + llen + 1;

  if (need > size)
    return;

  memcpy(dst, base, blen);
  dst[blen] = '/';
  memcpy(dst + blen + 1, leaf, llen + 1);
}

static int set_appdir(appdir_t *d, const cage_config_t *cfg)
{
  const char *pfx, *sfx;
  if (cfg->appdir) {
    pfx = "";
    sfx = cfg->appdir;
  } else if (cfg->name) {
    pfx = APPDIR_BASE "/app-";
    sfx = cfg->name;
  } else {
    pfx = APPDIR_BASE "/default";
    sfx = "";
  }
  int n = snprintf(d->appdir, sizeof(d->appdir), "%s%s", pfx, sfx);
  if (n < 0 || (size_t)n >= sizeof(d->appdir))
    return -1;
  return 0;
}

static int set_dir(char *dst, size_t dst_size, const char *src, const char *appdir, const char *suffix)
{
  if (src) {
    size_t len = strlen(src);
    if (len >= dst_size)
      return -1;
    memcpy(dst, src, len + 1);
    return 0;
  }
  if (suffix) {
    join(dst, dst_size, appdir, suffix);
  } else {
    memcpy(dst, "/", 2);
  }
  return 0;
}

int resolve_appdir(const cage_config_t *cfg, appdir_t *d)
{
  memset(d, 0, sizeof(*d));
  if (set_appdir(d, cfg) != 0)
    return -1;
  set_dir(d->upperdir, sizeof(d->upperdir), cfg->upperdir, d->appdir, "upper");
  set_dir(d->workdir, sizeof(d->workdir), cfg->workdir, d->appdir, "work");
  set_dir(d->lowerdir, sizeof(d->lowerdir), cfg->lowerdir, d->appdir, NULL);
  return 0;
}

#include "gui.h"
#include "acl.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>

static int mkparent(const char *path, uid_t uid, gid_t gid)
{
  const char *par = strrchr(path, '/');
  if (!par)
    return -1;
  size_t len = (size_t)(par - path);
  char buf[4096];
  if (len >= sizeof(buf))
    return -1;
  memcpy(buf, path, len);
  buf[len] = '\0';
  if (io_mkdir_own(buf, 0, 0, 0755) != 0)
    return -1;
  return io_chown(buf, uid, gid);
}

static int mount_file(const char *root, const char *sub, const char *src, uid_t uid, gid_t gid)
{
  char *p = mem_path(root, sub);
  if (!p)
    return -1;
  if (io_exists(p) != 0 && (mkparent(p, uid, gid) != 0 || io_touch(p) != 0)) {
    mem_free(p);
    return -1;
  }
  if (sys_mount(src, p, NULL, MS_BIND, NULL) != 0) {
    fprintf(stderr, "cage: mount %s failed\n", src);
    mem_free(p);
    return -1;
  }
  mem_free(p);
  return 0;
}

int mount_file_acl(const struct gui_ctx *ctx, const char *target, const char *source)
{
  int r = mount_file(ctx->root, target, source, ctx->uid, ctx->gid);
  if (r)
    return r;
  char *absp = mem_path(ctx->root, target);
  if (!absp)
    return -1;
  acl_guest_set(absp, ctx->uid, ctx->gid, ctx->groups, ctx->ngroups);
  mem_free(absp);
  return 0;
}

static int try_create_own(const char *path, uid_t uid, gid_t gid)
{
  if (io_exists(path) != 0) {
    if (io_mkdir(path) != 0)
      return -1;
    if (io_chown(path, uid, gid) != 0)
      return -1;
  }
  return 0;
}

static int mkdir_parents_own(const char *path, uid_t uid, gid_t gid)
{
  char buf[4096];
  size_t len = strlen(path);
  if (len >= sizeof(buf))
    return -1;
  memcpy(buf, path, len + 1);
  for (char *p = buf + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (try_create_own(buf, uid, gid) != 0)
        return -1;
      *p = '/';
    }
  }
  if (try_create_own(buf, uid, gid) != 0)
    return -1;
  return 0;
}

static char *try_cookie(const char *path)
{
  return (path && io_exists(path) == 0) ? mem_strdup(path) : NULL;
}

static char *cat_and_try(const char *base, const char *suf)
{
  char *p = mem_cat(base, strlen(base), suf, strlen(suf), MEM_CAT_END);
  if (!p)
    return NULL;
  char *found = (io_exists(p) == 0) ? mem_strdup(p) : NULL;
  mem_free(p);
  return found;
}

static char *find_pulse_cookie(const struct gui_ctx *ctx, const char *home)
{
  char *c = try_cookie(ctx->host_pc);
  if (c)
    return c;
  if (ctx->host_xch) {
    c = cat_and_try(ctx->host_xch, "/pulse/cookie");
    if (c)
      return c;
  }
  c = cat_and_try(home, "/.config/pulse/cookie");
  if (c)
    return c;
  return cat_and_try(home, "/.pulse-cookie");
}

static int mount_hfile(const struct gui_ctx *ctx, const char *h, const char *c, const char *f)
{
  char *s = mem_path(h, f);
  if (!s)
    return -1;
  if (io_exists(s) != 0) {
    mem_free(s);
    return 0;
  }
  char *t = mem_path(c, f);
  if (!t) {
    mem_free(s);
    return -1;
  }
  int n = mount_file_acl(ctx, t, s);
  mem_free(s);
  mem_free(t);
  return n;
}

static char *resolve_guest_cookie_path(const struct gui_ctx *ctx, const char *container_home)
{
  if (ctx->guest_pc)
    return mem_strdup(ctx->guest_pc);
  if (ctx->guest_xch)
    return mem_cat(ctx->guest_xch, strlen(ctx->guest_xch), "/pulse/cookie", 13, MEM_CAT_END);
  return mem_path(container_home, "/.config/pulse/cookie");
}

static int mount_xauthority(struct gui_ctx *ctx, const char *host_home, const char *container_home)
{
  char *s = mem_path(host_home, "/.Xauthority");
  if (!s)
    return -1;
  int exists = io_exists(s);
  mem_free(s);
  if (exists != 0)
    return 0;
  ctx->guest_home_used = 1;
  return mount_hfile((const struct gui_ctx *)ctx, host_home, container_home, "/.Xauthority");
}

static int ensure_guest_home(const struct gui_ctx *ctx, const char *home)
{
  char *absp = mem_path(ctx->root, home);
  if (!absp)
    return -1;
  if (io_exists(absp) == 0) {
    mem_free(absp);
    return 0;
  }
  char *tmp = mem_cat(absp, strlen(absp), "/.dummy", 7, MEM_CAT_END);
  mem_free(absp);
  if (!tmp)
    return -1;
  int r = mkparent(tmp, ctx->uid, ctx->gid);
  mem_free(tmp);
  return r;
}

static int ensure_guest_home_if_needed(struct gui_ctx *ctx, const char *h)
{
  if (ctx->guest_home_used || ctx->guest_pc || ctx->guest_xch)
    return 0;
  ctx->guest_home_used = 1;
  return ensure_guest_home(ctx, h);
}

static int prepare_cookie_dirs(const struct gui_ctx *ctx, const char *guest_rel)
{
  char *absp = mem_path(ctx->root, guest_rel);
  if (!absp)
    return -1;
  if (io_exists(absp) == 0) {
    mem_free(absp);
    return 0;
  }
  char *slash = strrchr(absp, '/');
  if (!slash) {
    mem_free(absp);
    return -1;
  }
  *slash = '\0';
  int r = mkdir_parents_own(absp, ctx->uid, ctx->gid);
  *slash = '/';
  if (r == 0)
    r = io_touch(absp);
  mem_free(absp);
  return r;
}

static int mount_cookie(struct gui_ctx *ctx, const char *host_home, const char *container_home)
{
  char *cookie = find_pulse_cookie((const struct gui_ctx *)ctx, host_home);
  if (!cookie)
    return 0;
  char *guest = resolve_guest_cookie_path((const struct gui_ctx *)ctx, container_home);
  if (!guest) {
    mem_free(cookie);
    return -1;
  }
  int r = ensure_guest_home_if_needed(ctx, container_home);
  if (r == 0)
    r = prepare_cookie_dirs(ctx, guest);
  if (r == 0)
    r = mount_file_acl((const struct gui_ctx *)ctx, guest, cookie);
  mem_free(cookie);
  mem_free(guest);
  return r;
}

static int mount_user_files(struct gui_ctx *ctx, const char *host_home, const char *container_home)
{
  int r = mount_xauthority(ctx, host_home, container_home);
  if (r)
    return r;
  return mount_cookie(ctx, host_home, container_home);
}

static int mount_user_home(struct gui_ctx *ctx, const char *container_home)
{
  if (!ctx->host_home || !container_home)
    return 0;
  return mount_user_files(ctx, ctx->host_home, container_home);
}

int setup_gui_mounts(struct gui_ctx *ctx, const char *container_home)
{
  if (mount_x11(ctx) != 0)
    return -1;
  if (mount_xdg_socks(ctx) != 0)
    return -1;
  return mount_user_home(ctx, container_home);
}

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
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>

static char *parent_of(const char *rel)
{
  const char *par = strrchr(rel, '/');
  if (!par || par == rel)
    return NULL;
  size_t len = (size_t)(par - rel);
  char *dir = mem_alloc(len + 1);
  memcpy(dir, rel, len);
  dir[len] = '\0';
  return dir;
}

static int open_root(const struct gui_ctx *ctx)
{
  return open(ctx->root, O_PATH | O_DIRECTORY | O_CLOEXEC);
}

static int ensure_mount_target(const struct gui_ctx *ctx, const char *sub, uid_t uid, gid_t gid)
{
  int rootfd = open_root(ctx);
  if (rootfd < 0)
    return -1;
  char *dir = parent_of(sub);
  int r = dir ? io_mkdir_parents_at(rootfd, dir, uid, gid, 0755) : 0;
  if (r == 0)
    r = io_touch_at(rootfd, sub, uid, gid);
  mem_free(dir);
  close(rootfd);
  return r;
}

static int mount_file(const struct gui_ctx *ctx, const char *sub, const char *src, uid_t uid, gid_t gid)
{
  char *p = mem_path(ctx->root, sub);
  if (!p)
    return -1;
  if (ensure_mount_target(ctx, sub, uid, gid) != 0) {
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
  int r = mount_file(ctx, target, source, ctx->uid, ctx->gid);
  if (r)
    return r;
  char *absp = mem_path(ctx->root, target);
  if (!absp)
    return -1;
  acl_guest_set(absp, ctx->uid, ctx->gid, ctx->groups, ctx->ngroups);
  mem_free(absp);
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

static const char *skip_slashes(const char *p)
{
  while (*p == '/')
    p++;
  return p;
}

static int ensure_guest_home(const struct gui_ctx *ctx, const char *home)
{
  if (mem_path_escapes(home)) {
    fprintf(stderr, "cage: path contains a '..' element: %s\n", home);
    return -1;
  }
  int rootfd = open_root(ctx);
  if (rootfd < 0)
    return -1;
  int r = io_mkdir_parents_at(rootfd, skip_slashes(home), ctx->uid, ctx->gid, 0755);
  close(rootfd);
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
  if (mem_path_escapes(guest_rel)) {
    fprintf(stderr, "cage: path contains a '..' element: %s\n", guest_rel);
    return -1;
  }
  int rootfd = open_root(ctx);
  if (rootfd < 0)
    return -1;
  const char *rel = skip_slashes(guest_rel);
  char *dir = parent_of(rel);
  int r = dir ? io_mkdir_own_at(rootfd, dir, ctx->uid, ctx->gid, 0755) : 0;
  if (r == 0)
    r = io_touch_at(rootfd, rel, ctx->uid, ctx->gid);
  mem_free(dir);
  close(rootfd);
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
  if (container_home && mem_path_escapes(container_home))
    return -1;
  if (mount_x11(ctx) != 0)
    return -1;
  if (mount_xdg_socks(ctx) != 0)
    return -1;
  return mount_user_home(ctx, container_home);
}

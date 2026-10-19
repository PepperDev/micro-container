#include "gui.h"
#include "acl.h"
#include "util/io.h"
#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>

int mount_file_acl(const struct gui_ctx *ctx, const char *target, const char *source);

static int ensure_xdg_parent(const struct gui_ctx *ctx, const char *abspath)
{
  char *dup = mem_strdup(abspath);
  if (!dup)
    return -1;
  char *slash = strrchr(dup, '/');
  if (!slash || slash == dup) {
    mem_free(dup);
    return 0;
  }
  *slash = '\0';
  int r = io_mkdir_own(dup, ctx->uid, ctx->gid, 0700);
  mem_free(dup);
  return r;
}

static void fill_xdg(const struct gui_ctx *ctx, char *buf, size_t sz)
{
  if (ctx->guest_xrd)
    snprintf(buf, sz, "%s", ctx->guest_xrd);
  else
    snprintf(buf, sz, "/run/user/%u", ctx->uid);
}

static int create_guest_xdg_root(struct gui_ctx *ctx, char *buf, size_t sz)
{
  fill_xdg(ctx, buf, sz);
  char *g = mem_path(ctx->root, buf);
  if (!g)
    return -1;
  int r = 0;
  if (!ctx->guest_xrd_env)
    r = ensure_xdg_parent(ctx, g);
  if (r == 0)
    r = io_mkdir_own(g, ctx->uid, ctx->gid, 0700);
  mem_free(g);
  if (r != 0)
    return -1;
  ctx->xrd_created = 1;
  return 0;
}

static int touch_target(const struct gui_ctx *ctx, const char *target)
{
  char *p = mem_path(ctx->root, target);
  if (!p)
    return -1;
  int r = io_touch(p);
  mem_free(p);
  return r;
}

static int ensure_guest_xdg_pulse(struct gui_ctx *ctx)
{
  char xdg[512];
  if (create_guest_xdg_root(ctx, xdg, sizeof(xdg)) != 0)
    return -1;
  char *sub = mem_path(ctx->root, xdg);
  if (!sub)
    return -1;
  char *p = mem_path(sub, "pulse");
  mem_free(sub);
  if (!p)
    return -1;
  int r = io_mkdir_own(p, ctx->uid, ctx->gid, 0700);
  mem_free(p);
  return r;
}

static char *resolve_pulse_src(const char *host_ps, const char *host_xrd)
{
  if (host_ps) {
    if (strncmp(host_ps, "unix:", 5) == 0)
      return mem_strdup(host_ps + 5);
    if (strchr(host_ps, '/'))
      return mem_strdup(host_ps);
    return NULL;
  }
  return mem_cat(host_xrd, strlen(host_xrd), "/pulse/native", 13, MEM_CAT_END);
}

static const char *pulse_target(const struct gui_ctx *ctx, char *buf, size_t sz)
{
  const char *gp = ctx->guest_ps;
  if (gp)
    return strncmp(gp, "unix:", 5) == 0 ? gp + 5 : gp;
  if (ctx->guest_xrd) {
    snprintf(buf, sz, "%s/pulse/native", ctx->guest_xrd);
    return buf;
  }
  snprintf(buf, sz, "run/user/%u/pulse/native", ctx->uid);
  return buf;
}

static int ensure_wayland_target(struct gui_ctx *ctx, const char *wn, char *t, size_t sz)
{
  char xdg[512];
  fill_xdg(ctx, xdg, sizeof(xdg));
  snprintf(t, sz, "%s/%s", xdg, wn);
  char *absp = mem_path(ctx->root, t);
  if (!absp)
    return -1;
  int r = io_exists(absp);
  mem_free(absp);
  if (r != 0) {
    r = create_guest_xdg_root(ctx, xdg, sizeof(xdg));
    if (r == 0)
      r = touch_target(ctx, t);
  }
  if (!ctx->xrd_created)
    ctx->xrd_created = 1;
  return r;
}

static int mount_wayland(struct gui_ctx *ctx, const char *wl)
{
  char *src = mem_path(ctx->host_xrd, wl);
  if (!src)
    return -1;
  if (io_exists(src) != 0) {
    mem_free(src);
    return 0;
  }
  const char *wn = ctx->guest_wl ? ctx->guest_wl : wl;
  char p[1024];
  int r = ensure_wayland_target(ctx, wn, p, sizeof(p));
  if (r == 0)
    r = mount_file_acl(ctx, p, src);
  mem_free(src);
  return r;
}

static int prepare_default_pulse_target(struct gui_ctx *ctx, const char *tgt)
{
  int r = ensure_guest_xdg_pulse(ctx);
  if (r == 0)
    r = touch_target(ctx, tgt);
  return r;
}

static int prepare_custom_pulse_target(const struct gui_ctx *ctx, const char *tgt)
{
  char *p = mem_path(ctx->root, tgt);
  if (!p)
    return -1;
  int r = 0;
  char *slash = strrchr(p, '/');
  if (slash) {
    *slash = '\0';
    r = io_mkdir_own(p, ctx->uid, ctx->gid, 0700);
  }
  mem_free(p);
  if (r == 0)
    r = touch_target(ctx, tgt);
  return r;
}

static int ensure_pulse_target(struct gui_ctx *ctx, const char *tgt)
{
  char *absp = mem_path(ctx->root, tgt);
  if (!absp)
    return -1;
  int exists = io_exists(absp);
  mem_free(absp);
  if (exists != 0) {
    if (!ctx->guest_ps)
      return prepare_default_pulse_target(ctx, tgt);
    return prepare_custom_pulse_target(ctx, tgt);
  }
  if (!ctx->guest_ps && !ctx->xrd_created)
    ctx->xrd_created = 1;
  return 0;
}

static int mount_pulse(struct gui_ctx *ctx)
{
  char *src = resolve_pulse_src(ctx->host_ps, ctx->host_xrd);
  if (!src)
    return 0;
  if (io_exists(src) != 0) {
    mem_free(src);
    return 0;
  }
  char target[512];
  const char *tgt = pulse_target(ctx, target, sizeof(target));
  int r = ensure_pulse_target(ctx, tgt);
  if (r == 0)
    r = mount_file_acl(ctx, tgt, src);
  mem_free(src);
  return r;
}

int mount_xdg_socks(struct gui_ctx *ctx)
{
  if (!ctx->host_xrd)
    return 0;
  const char *wl = ctx->host_wl ? ctx->host_wl : "wayland-0";
  int r = mount_wayland(ctx, wl);
  return (mount_pulse(ctx) != 0) ? -1 : r;
}

static int wayland_sock_present(const struct gui_ctx *ctx)
{
  const char *wl = ctx->host_wl ? ctx->host_wl : "wayland-0";
  char *src = mem_path(ctx->host_xrd, wl);
  if (!src)
    return 0;
  int present = io_exists(src) == 0;
  mem_free(src);
  return present;
}

static int pulse_sock_present(const struct gui_ctx *ctx)
{
  if (ctx->guest_ps)
    return 0;
  char *psrc = resolve_pulse_src(ctx->host_ps, ctx->host_xrd);
  if (!psrc)
    return 0;
  int present = io_exists(psrc) == 0;
  mem_free(psrc);
  return present;
}

void xdg_detect_socks(struct gui_ctx *ctx)
{
  ctx->xrd_created = 0;
  if (!ctx->host_xrd)
    return;
  ctx->xrd_created = wayland_sock_present(ctx) || pulse_sock_present(ctx);
}

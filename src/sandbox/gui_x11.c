#include "gui.h"
#include "mount.h"
#include "acl.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <limits.h>
// cppcheck-suppress missingIncludeSystem
#include <dirent.h>

static int set_sock_acl(const char *name, const struct gui_ctx *ctx)
{
  char path[PATH_MAX];
  if (snprintf(path, sizeof(path), "/tmp/.X11-unix/%s", name) >= (int)sizeof(path))
    return -1;
  return acl_set(path, ctx->uid, ctx->gid, ctx->groups, ctx->ngroups);
}

static int parse_display_n(const char *d)
{
  if (!d || d[0] != ':')
    return -1;
  char *end;
  long n = strtol(d + 1, &end, 10);
  if (end == d + 1 || n < 0)
    return -1;
  return (int)n;
}

static int match_x11_sock(const struct dirent *e, int disp_n)
{
  if (e->d_name[0] != 'X' || e->d_type != DT_SOCK)
    return 0;
  if (disp_n < 0)
    return 1;
  char want[16];
  snprintf(want, sizeof(want), "X%d", disp_n);
  return strcmp(e->d_name, want) == 0;
}

static int set_x11_acls(const struct gui_ctx *ctx, int disp_n)
{
  DIR *dir = sys_opendir("/tmp/.X11-unix");
  if (!dir)
    return 0;
  const struct dirent *e;
  while ((e = sys_readdir(dir)) != NULL) {
    if (!match_x11_sock(e, disp_n))
      continue;
    if (set_sock_acl(e->d_name, ctx) != 0) {
      sys_closedir(dir);
      return -1;
    }
  }
  sys_closedir(dir);
  return 0;
}

static int try_x_display(const char *name, int best)
{
  if (name[0] != 'X')
    return best;
  char *end;
  long n = strtol(name + 1, &end, 10);
  if (*end == '\0' && n >= 0 && (best < 0 || n < best))
    return (int)n;
  return best;
}

static int scan_lowest_x_socket(void)
{
  DIR *dir = sys_opendir("/tmp/.X11-unix");
  if (!dir)
    return -1;
  int best = -1;
  const struct dirent *e;
  while ((e = sys_readdir(dir)) != NULL)
    best = try_x_display(e->d_name, best);
  sys_closedir(dir);
  return best;
}

static int resolve_x11_displays(const char *host_disp, const char *guest_disp, int *host_n, int *guest_n)
{
  *host_n = parse_display_n(host_disp);
  if (guest_disp) {
    *guest_n = parse_display_n(guest_disp);
    if (*guest_n < 0)
      return -1;
    return 0;
  }
  if (*host_n < 0 && host_disp && host_disp[0])
    return -1;
  *guest_n = *host_n;
  return 0;
}

static int resolve_host_x11(int *host_n)
{
  if (*host_n >= 0) {
    char path[64];
    snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", *host_n);
    if (sys_access(path, F_OK) != 0)
      return -1;
    return 0;
  }
  if (sys_access("/tmp/.X11-unix", F_OK) != 0)
    return -1;
  *host_n = scan_lowest_x_socket();
  if (*host_n < 0)
    return -1;
  return 0;
}

static int mount_x11_socket(const struct gui_ctx *ctx, int host_n, int guest_n)
{
  if (host_n == guest_n) {
    if (mount_bind(ctx->root, "tmp/.X11-unix", "/tmp/.X11-unix") != 0)
      return -1;
    return set_x11_acls(ctx, guest_n);
  }
  char t[80], s[80];
  snprintf(t, sizeof(t), "tmp/.X11-unix/X%d", guest_n);
  snprintf(s, sizeof(s), "/tmp/.X11-unix/X%d", host_n);
  return mount_file_acl(ctx, t, s);
}

static int resolve_x11_pair(const struct gui_ctx *ctx, int *host_n, int *guest_n)
{
  if (resolve_x11_displays(ctx->host_display, ctx->guest_display, host_n, guest_n) != 0)
    return -1;
  if (resolve_host_x11(host_n) != 0)
    return -1;
  if (*guest_n < 0)
    *guest_n = *host_n;
  return 0;
}

void x11_resolve_display(struct gui_ctx *ctx)
{
  int host_n, guest_n;
  if (resolve_x11_pair(ctx, &host_n, &guest_n) == 0)
    ctx->guest_x11num = guest_n;
}

int mount_x11(struct gui_ctx *ctx)
{
  int host_n, guest_n;
  if (resolve_x11_pair(ctx, &host_n, &guest_n) != 0)
    return 0;
  if (mount_x11_socket(ctx, host_n, guest_n) != 0)
    return -1;
  ctx->guest_x11num = guest_n;
  return 0;
}

#ifndef CAGE_GUI_H
#define CAGE_GUI_H

// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

struct gui_ctx {
  const char *root;
  uid_t uid;
  gid_t gid;
  const gid_t *groups;
  size_t ngroups;
  const char *host_wl;
  const char *host_ps;
  const char *host_pc;
  const char *host_xch;
  const char *host_xrd;
  const char *host_home;
  const char *host_display;
  const char *guest_wl;
  const char *guest_ps;
  const char *guest_pc;
  const char *guest_xrd;
  int xrd_created;
  int guest_xrd_env;
  const char *guest_xch;
  int guest_home_used;
  const char *guest_display;
  int guest_x11num;
};

int setup_gui_mounts(struct gui_ctx *ctx, const char *container_home);
int mount_file_acl(const struct gui_ctx *ctx, const char *target, const char *source);
int mount_xdg_socks(struct gui_ctx *ctx);
int mount_x11(struct gui_ctx *ctx);
void x11_resolve_display(struct gui_ctx *ctx);
void xdg_detect_socks(struct gui_ctx *ctx);

#endif

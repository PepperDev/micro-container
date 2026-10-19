#ifndef CAGE_MOCK_H
#define CAGE_MOCK_H

#include <sys/types.h>
#include <stddef.h>
// cppcheck-suppress missingIncludeSystem
#include <dirent.h>

extern int _mock_mount_ret;
extern int _mock_mount_count;
extern int _mock_mount_fail_after;
extern int _mock_mount_fail_count;
extern int _mock_mount_errno;
extern int _mock_statfs_ret;
extern unsigned long _mock_statfs_type;
extern int _mock_umount_ret;
extern int _mock_umount_count;
extern int _mock_unshare_ret;
extern int _mock_unshare_count;
extern int _mock_setns_ret;
extern int _mock_setns_count;
extern int _mock_setns_nstype;
extern int _mock_chroot_ret;
extern int _mock_chroot_count;
extern int _mock_finit_module_ret;
extern int _mock_finit_module_count;
extern int _mock_finit_module_errno;
extern int _mock_setxattr_ret;
extern int _mock_setxattr_count;
extern int _mock_loop_get_free_ret;
extern int _mock_loop_get_free_count;
extern int _mock_loop_add_ret;
extern int _mock_loop_add_count;
extern int _mock_loop_get_stat_ret;
extern int _mock_loop_get_stat_count;
extern int _mock_loop_set_fd_ret;
extern int _mock_loop_set_fd_count;
extern int _mock_loop_set_status64_ret;
extern int _mock_loop_set_status64_count;
extern int _mock_loop_clr_fd_count;
extern int _mock_futex_wait_count;
extern int _mock_futex_wake_count;
extern int _mock_same_fs_ret;
extern int _mock_net_ip_forward_ret;
extern int _mock_net_ip6_forward_ret;
extern int _mock_needs_default_dns;
extern int _mock_netfilter_open_ret;
extern int _mock_netfilter_open_fd;
extern int _mock_netlink_open_ret;
extern int _mock_netlink_open_fd;
extern int _mock_netlink_send_ret;
extern int _mock_netlink_recv_ret;
extern int _mock_netlink_recv_type;
extern int _mock_netlink_recv_errno;
extern int _mock_netlink_send_ack_ret;
extern int _mock_netlink_recv_dump_ret;
extern int _mock_chown_ret;
extern int _mock_chown_count;
extern int _mock_access_ret;
extern int _mock_stat_ret;
extern int _mock_opendir_fail;
#define MOCK_DIRENTS_MAX 16
struct mock_dirent {
  char d_name[256];
  unsigned char d_type;
};
extern struct mock_dirent _mock_dirents[MOCK_DIRENTS_MAX];
extern int _mock_dirent_count;
extern int _mock_loop_ctl_fd;
extern const char *_mock_mod_dir;
extern const char *_mock_build_prefix;
extern const char *_mock_builtin_prefix;

void mock_sys_reset(void);

#endif

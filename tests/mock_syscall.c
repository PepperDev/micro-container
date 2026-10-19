#include "util/syscall.h"
#include "mock.h"
#include "sandbox/ext4fix.h"
#include "sandbox/loop.h"
#include "net/net.h"
#include "pkg/resolve.h"

#include <sys/stat.h>

/* real implementations for pass-through in mock overrides */
extern int __same_fs_detect(const char *lowerdir, const char *upperdir, dev_t * lower_dev);
extern int __needs_default_dns(void);
extern int __net_enable_ip_forward(void);
extern int __net_enable_ip6_forward(void);
extern int __sys_stat(const char *path, struct stat *buf);
extern int __sys_chown(const char *path, uid_t owner, gid_t group);
extern int __loop_open_control(void);
extern const char *__resolve_mod_dir(void);
extern const char *__resolve_build_prefix(void);
extern const char *__modprobe_builtin_prefix(void);

// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/netlink.h>
// cppcheck-suppress missingIncludeSystem
#include <stdint.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/statfs.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

int _mock_mount_ret;
int _mock_mount_count;
int _mock_mount_fail_after;
int _mock_mount_fail_count;
int _mock_mount_errno;
int _mock_statfs_ret;
unsigned long _mock_statfs_type;
int _mock_umount_ret;
int _mock_umount_count;
int _mock_unshare_ret;
int _mock_unshare_count;
int _mock_setns_ret;
int _mock_setns_count;
int _mock_setns_nstype;
int _mock_chroot_ret;
int _mock_chroot_count;
int _mock_finit_module_ret;
int _mock_finit_module_count;
int _mock_finit_module_errno;
int _mock_setxattr_ret;
int _mock_setxattr_count;
int _mock_loop_get_free_ret;
int _mock_loop_get_free_count;
int _mock_loop_add_ret;
int _mock_loop_add_count;
int _mock_loop_get_stat_ret;
int _mock_loop_get_stat_count;
int _mock_loop_set_fd_ret;
int _mock_loop_set_fd_count;
int _mock_loop_set_status64_ret;
int _mock_loop_set_status64_count;
int _mock_loop_clr_fd_count;
int _mock_futex_wait_count;
int _mock_futex_wake_count;
int _mock_netfilter_open_ret;
int _mock_netfilter_open_fd;
int _mock_netlink_open_ret;
int _mock_netlink_open_fd;
int _mock_netlink_send_ret;
int _mock_netlink_recv_ret;
int _mock_netlink_recv_type;
int _mock_netlink_recv_errno;
int _mock_netlink_send_ack_ret;
int _mock_netlink_recv_dump_ret;
int _mock_chown_ret = -1;
int _mock_chown_count;
int _mock_access_ret;
int _mock_stat_ret;
int _mock_opendir_fail;
struct mock_dirent _mock_dirents[MOCK_DIRENTS_MAX];
int _mock_dirent_count;
static int _mock_readdir_idx;
int _mock_same_fs_ret;
int _mock_net_ip_forward_ret;
int _mock_net_ip6_forward_ret;
int _mock_needs_default_dns;
int _mock_loop_ctl_fd;
const char *_mock_mod_dir;
const char *_mock_build_prefix;
const char *_mock_builtin_prefix;

int sys_futex_wait(volatile int *uaddr, int val)
{
  (void)uaddr;
  (void)val;
  _mock_futex_wait_count++;
  return 0;
}

int sys_setgroups(size_t size, const gid_t *list)
{
  (void)size;
  (void)list;
  return 0;
}

int sys_futex_wake(volatile int *uaddr, int n)
{
  (void)uaddr;
  (void)n;
  _mock_futex_wake_count++;
  return n;
}

int sys_netlink_open(int *fd)
{
  *fd = _mock_netlink_open_fd;
  return _mock_netlink_open_ret;
}

void sys_netlink_close(int fd)
{
  (void)fd;
}

int sys_netlink_send(int fd, const void *buf, size_t len)
{
  (void)fd;
  (void)buf;
  (void)len;
  return _mock_netlink_send_ret;
}

int sys_netlink_recv(int fd, void *buf, size_t cap, size_t *len)
{
  if (_mock_netlink_recv_ret < 0)
    return _mock_netlink_recv_ret;
  (void)fd;
  size_t mlen = sizeof(struct nlmsghdr) + sizeof(struct nlmsgerr);
  if (cap < mlen)
    return -1;
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  memset(h, 0, mlen);
  int type = _mock_netlink_recv_type ? _mock_netlink_recv_type : NLMSG_ERROR;
  h->nlmsg_type = (uint16_t) type;
  h->nlmsg_len = (uint32_t) mlen;
  if (type == NLMSG_ERROR) {
    struct nlmsgerr *e = (struct nlmsgerr *)NLMSG_DATA(h);
    e->error = _mock_netlink_recv_errno;
  }
  *len = mlen;
  return 0;
}

int sys_netlink_send_ack(int fd, const void *buf, size_t len)
{
  (void)fd;
  (void)buf;
  (void)len;
  return _mock_netlink_send_ack_ret;
}

int sys_netlink_recv_dump(int fd, void *buf, size_t cap, size_t *len)
{
  if (_mock_netlink_recv_dump_ret < 0)
    return _mock_netlink_recv_dump_ret;
  (void)fd;
  size_t mlen = sizeof(struct nlmsghdr) + sizeof(struct nlmsgerr);
  if (cap < mlen)
    return -1;
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  memset(h, 0, mlen);
  h->nlmsg_type = NLMSG_DONE;
  h->nlmsg_len = (uint32_t) mlen;
  *len = mlen;
  return 0;
}

int sys_netfilter_open(int *fd)
{
  *fd = _mock_netfilter_open_fd;
  return _mock_netfilter_open_ret;
}

void mock_sys_reset(void)
{
  _mock_mount_ret = 0;
  _mock_mount_count = 0;
  _mock_mount_fail_after = 0;
  _mock_mount_fail_count = 0;
  _mock_mount_errno = 0;
  _mock_statfs_ret = 0;
  _mock_statfs_type = 0;
  _mock_umount_ret = 0;
  _mock_umount_count = 0;
  _mock_unshare_ret = 0;
  _mock_unshare_count = 0;
  _mock_setns_ret = 0;
  _mock_setns_count = 0;
  _mock_setns_nstype = 0;
  _mock_chroot_ret = 0;
  _mock_chroot_count = 0;
  _mock_finit_module_ret = 0;
  _mock_finit_module_count = 0;
  _mock_finit_module_errno = 0;
  _mock_setxattr_ret = 0;
  _mock_setxattr_count = 0;
  _mock_loop_get_free_ret = 0;
  _mock_loop_get_free_count = 0;
  _mock_loop_add_ret = 0;
  _mock_loop_add_count = 0;
  _mock_loop_get_stat_ret = 0;
  _mock_loop_get_stat_count = 0;
  _mock_loop_set_fd_ret = 0;
  _mock_loop_set_fd_count = 0;
  _mock_loop_set_status64_ret = 0;
  _mock_loop_set_status64_count = 0;
  _mock_loop_clr_fd_count = 0;
  _mock_futex_wait_count = 0;
  _mock_futex_wake_count = 0;
  _mock_same_fs_ret = 0;
  _mock_net_ip_forward_ret = -1;
  _mock_net_ip6_forward_ret = -1;
  _mock_needs_default_dns = -1;
  _mock_loop_ctl_fd = -1;
  _mock_mod_dir = NULL;
  _mock_build_prefix = NULL;
  _mock_builtin_prefix = NULL;
  _mock_netfilter_open_ret = 0;
  _mock_netfilter_open_fd = 43;
  _mock_netlink_open_ret = 0;
  _mock_netlink_open_fd = 42;
  _mock_netlink_send_ret = 0;
  _mock_netlink_recv_ret = 0;
  _mock_netlink_recv_type = 0;
  _mock_netlink_recv_errno = 0;
  _mock_netlink_send_ack_ret = 0;
  _mock_netlink_recv_dump_ret = 0;
  _mock_chown_ret = -1;
  _mock_chown_count = 0;
  _mock_access_ret = 0;
  _mock_stat_ret = -1;
  _mock_opendir_fail = 0;
  _mock_dirent_count = 0;
  _mock_readdir_idx = 0;
}

int same_fs_detect(const char *lowerdir, const char *upperdir, dev_t *lower_dev)
{
  if (_mock_same_fs_ret >= 0)
    return _mock_same_fs_ret;
  return __same_fs_detect(lowerdir, upperdir, lower_dev);
}

int needs_default_dns(void)
{
  if (_mock_needs_default_dns >= 0)
    return _mock_needs_default_dns;
  return __needs_default_dns();
}

int net_enable_ip_forward(void)
{
  if (_mock_net_ip_forward_ret >= 0)
    return _mock_net_ip_forward_ret;
  return __net_enable_ip_forward();
}

int net_enable_ip6_forward(void)
{
  if (_mock_net_ip6_forward_ret >= 0)
    return _mock_net_ip6_forward_ret;
  return __net_enable_ip6_forward();
}

int loop_open_control(void)
{
  if (_mock_loop_ctl_fd >= 0)
    return _mock_loop_ctl_fd;
  return __loop_open_control();
}

const char *resolve_mod_dir(void)
{
  if (_mock_mod_dir)
    return _mock_mod_dir;
  return __resolve_mod_dir();
}

const char *resolve_build_prefix(void)
{
  if (_mock_build_prefix)
    return _mock_build_prefix;
  return __resolve_build_prefix();
}

const char *modprobe_builtin_prefix(void)
{
  if (_mock_builtin_prefix)
    return _mock_builtin_prefix;
  return __modprobe_builtin_prefix();
}

int sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data)
{
  (void)source;
  (void)target;
  (void)fstype;
  (void)flags;
  (void)data;
  _mock_mount_count++;
  if (_mock_mount_fail_count > 0) {
    _mock_mount_fail_count--;
    errno = _mock_mount_errno;
    return -1;
  }
  if (_mock_mount_fail_after > 0 && _mock_mount_count >= _mock_mount_fail_after) {
    errno = _mock_mount_errno;
    return -1;
  }
  return _mock_mount_ret;
}

int sys_statfs(const char *path, struct statfs *buf)
{
  (void)path;
  if (_mock_statfs_ret != 0) {
    errno = _mock_statfs_ret;
    return -1;
  }
  buf->f_type = _mock_statfs_type;
  return 0;
}

int sys_umount(const char *target)
{
  (void)target;
  _mock_umount_count++;
  return _mock_umount_ret;
}

int sys_unshare(int flags)
{
  (void)flags;
  _mock_unshare_count++;
  return _mock_unshare_ret;
}

int sys_setns(int fd, int nstype)
{
  (void)fd;
  _mock_setns_nstype = nstype;
  _mock_setns_count++;
  return _mock_setns_ret;
}

int sys_chroot(const char *path)
{
  (void)path;
  _mock_chroot_count++;
  return _mock_chroot_ret;
}

int sys_finit_module(int fd, const char *args, int flags)
{
  (void)fd;
  (void)args;
  (void)flags;
  _mock_finit_module_count++;
  if (_mock_finit_module_ret != 0)
    errno = _mock_finit_module_errno;
  return _mock_finit_module_ret;
}

int sys_setxattr(const char *path, const char *name, const void *value, size_t size, int flags)
{
  (void)path;
  (void)name;
  (void)value;
  (void)size;
  (void)flags;
  _mock_setxattr_count++;
  return _mock_setxattr_ret;
}

int sys_loop_get_free(int ctl_fd)
{
  (void)ctl_fd;
  _mock_loop_get_free_count++;
  return _mock_loop_get_free_ret;
}

int sys_loop_add(int ctl_fd)
{
  (void)ctl_fd;
  _mock_loop_add_count++;
  return _mock_loop_add_ret;
}

int sys_loop_get_stat(int loop_fd)
{
  (void)loop_fd;
  _mock_loop_get_stat_count++;
  return _mock_loop_get_stat_ret;
}

int sys_loop_set_fd(int loop_fd, int img_fd)
{
  (void)loop_fd;
  (void)img_fd;
  _mock_loop_set_fd_count++;
  return _mock_loop_set_fd_ret;
}

int sys_loop_set_status64(int loop_fd, const void *info)
{
  (void)loop_fd;
  (void)info;
  _mock_loop_set_status64_count++;
  return _mock_loop_set_status64_ret;
}

int sys_loop_clr_fd(int loop_fd)
{
  (void)loop_fd;
  _mock_loop_clr_fd_count++;
  return 0;
}

int sys_access(const char *path, int mode)
{
  (void)path;
  (void)mode;
  return _mock_access_ret;
}

int sys_stat(const char *path, struct stat *buf)
{
  if (_mock_stat_ret < 0)
    return __sys_stat(path, buf);
  if (_mock_stat_ret == 0 && buf) {
    memset(buf, 0, sizeof(*buf));
    buf->st_uid = 0;
    buf->st_mode = S_IFSOCK | 0600;
  }
  return _mock_stat_ret;
}

int sys_chown(const char *path, uid_t owner, gid_t group)
{
  if (_mock_chown_ret < 0)
    return __sys_chown(path, owner, group);
  _mock_chown_count++;
  return _mock_chown_ret;
}

DIR *sys_opendir(const char *path)
{
  (void)path;
  if (_mock_opendir_fail)
    return NULL;
  _mock_readdir_idx = 0;
  return (DIR *) (intptr_t) 0xDEAD;
}

struct dirent *sys_readdir(DIR *dir)
{
  (void)dir;
  if (_mock_readdir_idx >= _mock_dirent_count)
    return NULL;
  static struct dirent result;
  size_t len = strlen(_mock_dirents[_mock_readdir_idx].d_name);
  if (len >= sizeof(result.d_name))
    len = sizeof(result.d_name) - 1;
  memcpy(result.d_name, _mock_dirents[_mock_readdir_idx].d_name, len);
  result.d_name[len] = '\0';
  result.d_type = _mock_dirents[_mock_readdir_idx].d_type;
  _mock_readdir_idx++;
  return &result;
}

int sys_closedir(DIR *dir)
{
  (void)dir;
  _mock_readdir_idx = 0;
  return 0;
}

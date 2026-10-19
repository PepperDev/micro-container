#ifndef CAGE_SYSCALL_H
#define CAGE_SYSCALL_H

#define weak_alias(old, new) \
  extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))

// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>
// cppcheck-suppress missingIncludeSystem
#include <stddef.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <dirent.h>
// cppcheck-suppress missingIncludeSystem
#include <time.h>

struct statfs;
int sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data);
int sys_umount(const char *target);
int sys_statfs(const char *path, struct statfs *buf);
int sys_unshare(int flags);
int sys_setns(int fd, int nstype);
int sys_chroot(const char *path);
int sys_finit_module(int fd, const char *args, int flags);
int sys_setxattr(const char *path, const char *name, const void *value, size_t size, int flags);
int sys_loop_get_free(int ctl_fd);
int sys_loop_add(int ctl_fd);
int sys_loop_get_stat(int loop_fd);
int sys_loop_set_fd(int loop_fd, int img_fd);
int sys_loop_set_status64(int loop_fd, const void *info);
int sys_loop_clr_fd(int loop_fd);
int sys_futex_wait(volatile int *uaddr, int val);
int sys_futex_wait_to(volatile int *uaddr, int val, const struct timespec *timeout);
int sys_futex_wake(volatile int *uaddr, int n);
int sys_setgroups(size_t size, const gid_t * list);
int sys_netlink_open(int *fd);
void sys_netlink_close(int fd);
int sys_netlink_send(int fd, const void *buf, size_t len);
int sys_netlink_recv(int fd, void *buf, size_t cap, size_t *len);
int sys_netlink_recv_nb(int fd, void *buf, size_t cap, size_t *len);
int sys_netlink_send_ack(int fd, const void *buf, size_t len);
int sys_netlink_recv_dump(int fd, void *buf, size_t cap, size_t *len);
int sys_chown(const char *path, uid_t owner, gid_t group);
int sys_netfilter_open(int *fd);
int sys_access(const char *path, int mode);
int sys_stat(const char *path, struct stat *buf);
DIR *sys_opendir(const char *path);
struct dirent *sys_readdir(DIR * dir);
int sys_closedir(DIR * dir);

#endif

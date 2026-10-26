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
int sys_kill(pid_t pid, int sig);
int sys_finit_module(int fd, const char *args, int flags);
int sys_setxattr(const char *path, const char *name, const void *value, size_t size, int flags);
#define SYS_OPEN_NOFOLLOW 1
int sys_open_rdonly(const char *path, int flags);
int sys_openat(int dirfd, const char *path, int flags, mode_t mode);
int sys_mkdirat(int dirfd, const char *path, mode_t mode);
int sys_fstatat(int dirfd, const char *path, struct stat *buf, int flags);
int sys_fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags);
int sys_fstat(int fd, struct stat *buf);
int sys_isdir_nofollow(const char *path);
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
int sys_chown(const char *path, uid_t owner, gid_t group);
int sys_setreuid(uid_t ruid, uid_t euid);
int sys_setregid(gid_t rgid, gid_t egid);
int sys_access(const char *path, int mode);
int sys_open_loop_control(void);
int sys_open_loop_dev(int n);
int sys_chdir(const char *path);
pid_t sys_fork(void);
pid_t sys_waitpid(pid_t pid, int *status, int options);
int sys_stat(const char *path, struct stat *buf);
DIR *sys_opendir(const char *path);
struct dirent *sys_readdir(DIR * dir);
int sys_closedir(DIR * dir);

#endif

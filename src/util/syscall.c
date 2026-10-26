#include "syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/statfs.h>
// cppcheck-suppress missingIncludeSystem
#include <sched.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/ioctl.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/loop.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/futex.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/syscall.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/xattr.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <signal.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/wait.h>
// cppcheck-suppress missingIncludeSystem
#include <grp.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>

/* weak defaults — overridden by tests/mock_syscall.c in test binary */

// cppcheck-suppress staticFunction
int __sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data)
{
  return mount(source, target, fstype, flags, data);
}

weak_alias(__sys_mount, sys_mount);

// cppcheck-suppress staticFunction
int __sys_umount(const char *target)
{
  return umount(target);
}

weak_alias(__sys_umount, sys_umount);

// cppcheck-suppress staticFunction
int __sys_statfs(const char *path, struct statfs *buf)
{
  return statfs(path, buf);
}

weak_alias(__sys_statfs, sys_statfs);

// cppcheck-suppress staticFunction
int __sys_unshare(int flags)
{
  return unshare(flags);
}

weak_alias(__sys_unshare, sys_unshare);

// cppcheck-suppress staticFunction
int __sys_setns(int fd, int nstype)
{
  return setns(fd, nstype);
}

weak_alias(__sys_setns, sys_setns);

// cppcheck-suppress staticFunction
int __sys_chroot(const char *path)
{
  return chroot(path);
}

weak_alias(__sys_chroot, sys_chroot);

// cppcheck-suppress staticFunction
int __sys_kill(pid_t pid, int sig)
{
  return kill(pid, sig);
}

weak_alias(__sys_kill, sys_kill);

// cppcheck-suppress staticFunction
int __sys_finit_module(int fd, const char *args, int flags)
{
  return (int)syscall(SYS_finit_module, fd, args, flags);
}

weak_alias(__sys_finit_module, sys_finit_module);

// cppcheck-suppress staticFunction
int __sys_setxattr(const char *path, const char *name, const void *value, size_t size, int flags)
{
  return setxattr(path, name, value, size, flags);
}

weak_alias(__sys_setxattr, sys_setxattr);

// cppcheck-suppress staticFunction
int __sys_open_rdonly(const char *path, int flags)
{
  int fd;
  if ((flags & SYS_OPEN_NOFOLLOW) && (fd = open(path, O_PATH | O_NOFOLLOW | O_CLOEXEC)) >= 0) {
    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISLNK(st.st_mode)) {
      close(fd);
      return -1;
    }
  }
  return open(path, O_PATH | O_CLOEXEC);
}

weak_alias(__sys_open_rdonly, sys_open_rdonly);

// cppcheck-suppress staticFunction
int __sys_openat(int dirfd, const char *path, int flags, mode_t mode)
{
  return openat(dirfd, path, flags, mode);
}

weak_alias(__sys_openat, sys_openat);

// cppcheck-suppress staticFunction
int __sys_mkdirat(int dirfd, const char *path, mode_t mode)
{
  return mkdirat(dirfd, path, mode);
}

weak_alias(__sys_mkdirat, sys_mkdirat);

// cppcheck-suppress staticFunction
int __sys_fstatat(int dirfd, const char *path, struct stat *buf, int flags)
{
  return fstatat(dirfd, path, buf, flags);
}

weak_alias(__sys_fstatat, sys_fstatat);

// cppcheck-suppress staticFunction
int __sys_fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags)
{
  return fchownat(dirfd, path, owner, group, flags);
}

weak_alias(__sys_fchownat, sys_fchownat);

// cppcheck-suppress staticFunction
int __sys_isdir_nofollow(const char *path)
{
  struct stat st;
  if (lstat(path, &st) != 0)
    return -1;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

weak_alias(__sys_isdir_nofollow, sys_isdir_nofollow);

// cppcheck-suppress staticFunction
int __sys_fstat(int fd, struct stat *buf)
{
  return fstat(fd, buf);
}

weak_alias(__sys_fstat, sys_fstat);

// cppcheck-suppress staticFunction
int __sys_loop_get_free(int ctl_fd)
{
  return (int)ioctl(ctl_fd, LOOP_CTL_GET_FREE);
}

weak_alias(__sys_loop_get_free, sys_loop_get_free);

// cppcheck-suppress staticFunction
int __sys_loop_add(int ctl_fd)
{
  return (int)ioctl(ctl_fd, LOOP_CTL_ADD, 0);
}

weak_alias(__sys_loop_add, sys_loop_add);

// cppcheck-suppress staticFunction
int __sys_loop_get_stat(int loop_fd)
{
  return (int)ioctl(loop_fd, LOOP_GET_STATUS, NULL);
}

weak_alias(__sys_loop_get_stat, sys_loop_get_stat);

// cppcheck-suppress staticFunction
int __sys_loop_set_fd(int loop_fd, int img_fd)
{
  return (int)ioctl(loop_fd, LOOP_SET_FD, img_fd);
}

weak_alias(__sys_loop_set_fd, sys_loop_set_fd);

// cppcheck-suppress staticFunction
int __sys_loop_set_status64(int loop_fd, const void *info)
{
  return (int)ioctl(loop_fd, LOOP_SET_STATUS64, info);
}

weak_alias(__sys_loop_set_status64, sys_loop_set_status64);

// cppcheck-suppress staticFunction
int __sys_loop_clr_fd(int loop_fd)
{
  return (int)ioctl(loop_fd, LOOP_CLR_FD, 0);
}

weak_alias(__sys_loop_clr_fd, sys_loop_clr_fd);

// cppcheck-suppress staticFunction
int __sys_futex_wait(volatile int *uaddr, int val)
{
  return (int)syscall(SYS_futex, uaddr, FUTEX_WAIT, val, NULL, NULL, 0);
}

weak_alias(__sys_futex_wait, sys_futex_wait);

// cppcheck-suppress staticFunction
int __sys_futex_wait_to(volatile int *uaddr, int val, const struct timespec *timeout)
{
  return (int)syscall(SYS_futex, uaddr, FUTEX_WAIT, val, timeout, NULL, 0);
}

weak_alias(__sys_futex_wait_to, sys_futex_wait_to);

// cppcheck-suppress staticFunction
int __sys_futex_wake(volatile int *uaddr, int n)
{
  return (int)syscall(SYS_futex, uaddr, FUTEX_WAKE, n, NULL, NULL, 0);
}

weak_alias(__sys_futex_wake, sys_futex_wake);

// cppcheck-suppress staticFunction
int __sys_setgroups(size_t size, const gid_t *list)
{
  return setgroups(size, list);
}

weak_alias(__sys_setgroups, sys_setgroups);

// cppcheck-suppress staticFunction
int __sys_chdir(const char *path)
{
  return chdir(path);
}

weak_alias(__sys_chdir, sys_chdir);

// cppcheck-suppress staticFunction
pid_t __sys_fork(void)
{
  return fork();
}

weak_alias(__sys_fork, sys_fork);

// cppcheck-suppress staticFunction
pid_t __sys_waitpid(pid_t pid, int *status, int options)
{
  return waitpid(pid, status, options);
}

weak_alias(__sys_waitpid, sys_waitpid);

// cppcheck-suppress staticFunction
int __sys_open_loop_control(void)
{
  return open("/dev/loop-control", O_RDWR | O_CLOEXEC);
}

weak_alias(__sys_open_loop_control, sys_open_loop_control);

// cppcheck-suppress staticFunction
int __sys_open_loop_dev(int n)
{
  char path[32];
  snprintf(path, sizeof(path), "/dev/loop%d", n);
  return open(path, O_RDWR | O_CLOEXEC);
}

weak_alias(__sys_open_loop_dev, sys_open_loop_dev);

// cppcheck-suppress staticFunction
int __sys_access(const char *path, int mode)
{
  return access(path, mode);
}

weak_alias(__sys_access, sys_access);

// cppcheck-suppress staticFunction
int __sys_stat(const char *path, struct stat *buf)
{
  return stat(path, buf);
}

weak_alias(__sys_stat, sys_stat);

// cppcheck-suppress staticFunction
DIR *__sys_opendir(const char *path)
{
  return opendir(path);
}

weak_alias(__sys_opendir, sys_opendir);

// cppcheck-suppress staticFunction
struct dirent *__sys_readdir(DIR *dir)
{
  return readdir(dir);
}

weak_alias(__sys_readdir, sys_readdir);

// cppcheck-suppress staticFunction
int __sys_closedir(DIR *dir)
{
  return closedir(dir);
}

weak_alias(__sys_closedir, sys_closedir);

// cppcheck-suppress staticFunction
int __sys_chown(const char *path, uid_t owner, gid_t group)
{
  return chown(path, owner, group);
}

weak_alias(__sys_chown, sys_chown);

// cppcheck-suppress staticFunction
int __sys_setreuid(uid_t ruid, uid_t euid)
{
  return setreuid(ruid, euid);
}

weak_alias(__sys_setreuid, sys_setreuid);

// cppcheck-suppress staticFunction
int __sys_setregid(gid_t rgid, gid_t egid)
{
  return setregid(rgid, egid);
}

weak_alias(__sys_setregid, sys_setregid);

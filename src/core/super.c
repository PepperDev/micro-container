#include "super.h"
#include "util/io.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/wait.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>

static int fix_owner(const struct stat *st)
{
  if (st->st_uid == 0 && st->st_gid == 0)
    return 0;
  if (io_chown("/proc/self/exe", 0, 0) != 0) {
    fprintf(stderr, "cage: chown /proc/self/exe: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

// cppcheck-suppress staticFunction
int __super_chmod(const char *path, mode_t mode)
{
  return chmod(path, mode);
}

weak_alias(__super_chmod, super_chmod);

static int fix_mode(const struct stat *st)
{
  if ((st->st_mode & 07777) == 04755)
    return 0;
  if (super_chmod("/proc/self/exe", 04755) != 0) {
    fprintf(stderr, "cage: chmod /proc/self/exe: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

int super_fix(void)
{
  struct stat st;
  if (sys_stat("/proc/self/exe", &st) != 0) {
    fprintf(stderr, "cage: stat /proc/self/exe: %s\n", strerror(errno));
    return -1;
  }
  if (fix_owner(&st) != 0)
    return -1;
  return fix_mode(&st);
}

// cppcheck-suppress staticFunction
pid_t __super_vfork(void)
{
  /* fork, not vfork: a vfork child that fails execve in this static-PIE build
     leaves the parent unable to resume and it segfaults in startup code. This
     repair path is rare, so a full fork is acceptable. */
  return sys_fork();
}

weak_alias(__super_vfork, super_vfork);

// cppcheck-suppress staticFunction
pid_t __super_waitpid(pid_t pid, int *status, int options)
{
  return waitpid(pid, status, options);
}

weak_alias(__super_waitpid, super_waitpid);

// cppcheck-suppress staticFunction
int __super_execvp(const char *file, char *const argv[])
{
  return execvp(file, argv);
}

weak_alias(__super_execvp, super_execvp);

static void close_std_fds(void)
{
  close(0);
  close(1);
  close(2);
}

static int wait_exit_ok(pid_t pid)
{
  int status = 0;
  pid_t w;
  do {
    w = super_waitpid(pid, &status, 0);
  } while (w < 0 && errno == EINTR);
  if (w < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    return -1;
  return 0;
}

static int try_sudo_reexec(char **argv)
{
  pid_t pid = super_vfork();
  if (pid == -1) {
    fprintf(stderr, "cage: fork: %s\n", strerror(errno));
    return -1;
  }
  if (pid == 0) {
    close_std_fds();
    execlp("sudo", "sudo", "--", argv[0], "--fix", (char *)NULL);
    _exit(127);
  }
  if (wait_exit_ok(pid) != 0) {
    fprintf(stderr, "cage: sudo --fix failed\n");
    return -1;
  }
  super_execvp(argv[0], argv);
  fprintf(stderr, "cage: re-exec failed: %s\n", strerror(errno));
  return -1;
}

// cppcheck-suppress staticFunction
int is_suid_binary(void)
{
  struct stat st;
  return sys_stat("/proc/self/exe", &st) == 0 && st.st_uid == 0 && st.st_gid == 0 && (st.st_mode & S_ISUID);
}

static int drop_saved_ids(uid_t ruid, gid_t egid, gid_t rgid)
{
  if (ruid != 0 && sys_setreuid(0, 0) != 0) {
    fprintf(stderr, "cage: setreuid: %s\n", strerror(errno));
    return -1;
  }
  if ((egid != 0 || rgid != 0) && sys_setregid(0, 0) != 0) {
    fprintf(stderr, "cage: setregid: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

int super_escalate(char **argv, uid_t euid, uid_t ruid, gid_t egid, gid_t rgid)
{
  if (euid != 0) {
    if (is_suid_binary()) {
      fprintf(stderr, "cage: filesystem may be mounted nosuid\n");
      return -1;
    }
    return try_sudo_reexec(argv);
  }
  if (drop_saved_ids(ruid, egid, rgid) != 0)
    return -1;
  umask(0);
  return super_fix();
}

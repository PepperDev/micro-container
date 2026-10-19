#ifndef CAGE_PROC_H
#define CAGE_PROC_H

// cppcheck-suppress missingIncludeSystem
#include <stddef.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

pid_t pidfork(void);
char *compute_pidfile(const char *name, size_t *len);
int create_pidfile(const char *path);
pid_t readpid(const char *path, int *fd);
int close_pid(int fd);
int pidwait(pid_t pid, int *status);
int exit_code_from_status(int status);
int pidexists(pid_t pid);
int killpid(const char *name, const char *pidfile);
int join_namespaces(pid_t target);
pid_t detect_existing(const char *name, const char *pidfile);

#endif

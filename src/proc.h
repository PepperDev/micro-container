#ifndef PROC_H
#define PROC_H

#include <stdbool.h>
#include <sys/types.h>

pid_t pidfork();

int killpid(char *, char *);

char *compute_pidfile(char *, size_t, size_t *);

pid_t readpid(char *, int *);

int create_pidfile(char *, size_t);

int writepid(int, pid_t);

int close_pid(int);

int pidwait(pid_t, int *);

int pidexists(pid_t);

int fork_and_exec(char **);

#endif

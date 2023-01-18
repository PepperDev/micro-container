#ifndef PROC_H
#define PROC_H

#include <stdbool.h>
#include <sys/types.h>

int killpid(char *, char *);

char *compute_pidfile(char *, size_t);

pid_t readpid(char *, int *);

int close_pid(int);

#endif

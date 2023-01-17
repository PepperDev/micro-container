#ifndef PROC_H
#define PROC_H

#include <stdbool.h>
#include <sys/types.h>

int killpid(char *, char *);

char *compute_pidfile(char *, size_t);

#endif

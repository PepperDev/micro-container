#ifndef PROC_H
#define PROC_H

#include <unistd.h>

char proc_get_user(pid_t, uid_t*, gid_t*);

pid_t proc_get_parent(pid_t);

#endif

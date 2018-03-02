#ifndef USER_H
#define USER_H

#include <sys/types.h>

void calc_user(const char*, char**, uid_t*, gid_t*);

void create_home(const char*, const char*, uid_t, gid_t);

#endif

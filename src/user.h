#ifndef USER_H
#define USER_H

#include <stddef.h>
#include <unistd.h>

extern uid_t user_real_uid,
	user_effective_uid,
	user_caller_uid;

extern gid_t user_real_gid,
	user_effective_gid,
	user_caller_gid;

extern char *user_home;
extern size_t user_home_size;

void user_collect();

#endif

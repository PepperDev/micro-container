#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>

#include "user.h"

#define USER_HOME "HOME"
#define USER_SUDO_UID "SUDO_UID"
#define USER_SUDO_GID "SUDO_GID"

uid_t user_real_uid = 0,
	user_effective_uid = 0,
	user_caller_uid = 0;

gid_t user_real_gid = 0,
	user_effective_gid = 0,
	user_caller_gid = 0;

char *user_home = NULL;
size_t user_home_size = 0;

char user_sudo = 0;

static void user_collect_caller();

void user_collect()
{
	user_real_uid = getuid();
	user_effective_uid = geteuid();
	user_real_gid = getgid();
	user_effective_gid = getegid();

	user_collect_caller();
}

static void user_collect_caller()
{
	if (user_effective_uid != user_real_uid && user_effective_uid != 0)
	{
		user_caller_uid = user_effective_uid;
		user_caller_gid = user_effective_gid;
		return;
	}

	char *uid, *gid;
	uid = getenv(USER_SUDO_UID);
	if (uid != NULL && *uid != 0)
	{
		gid = getenv(USER_SUDO_GID);
		if (gid != NULL && *gid != 0)
		{
			user_caller_uid = strtoul(uid, NULL, 10);
			user_caller_gid = strtoul(gid, NULL, 10);
			if (user_caller_uid != 0)
			{
				return;
			}
		}
	}

	// TODO: pid_t ppid = getppid(); ...read /proc/%d/status

	fprintf(
		stderr,
		"Warning: unable to get caller uid\n"
	);
}

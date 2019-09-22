#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>

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

char user_incomplete_caller = 0;

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
	if (user_effective_uid != user_real_uid && user_real_uid != 0)
	{
		user_caller_uid = user_real_uid;
		user_caller_gid = user_real_gid;
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

	user_incomplete_caller = 1;
}

void user_require_home()
{
	if (user_home != NULL)
	{
		return;
	}

	char *home = getenv(USER_HOME);
	if (home != NULL && *home != 0)
	{
		user_home = strdup(home);
		user_home_size = strlen(user_home);
		return;
	}

	user_require_caller();

	struct passwd *pwd = getpwuid(user_caller_uid);
	if (pwd != NULL && pwd->pw_dir != NULL && *pwd->pw_dir != 0)
	{
		user_home = strdup(pwd->pw_dir);
		user_home_size = strlen(user_home);
		fprintf(
			stderr,
			"Warning: home not set, assuming caller home \"%s\"!\n",
			user_home
		);
		return;
	}

	fprintf(
		stderr,
		"Fatal: unable to get caller home!\n"
	);
	exit(1);
}

void user_require_caller()
{
	if (!user_incomplete_caller)
	{
		return;
	}

	// TODO: pid_t ppid = getppid(); ...read /proc/%d/status

	if (user_real_uid != 0)
	{
		user_caller_uid = user_real_gid;
		user_caller_gid = user_real_gid;
	}
	else
	{
		user_caller_uid = user_effective_uid;
		user_caller_gid = user_effective_gid;
	}

	fprintf(
		stderr,
		"Warning: unable to get caller uid, assuming %d!\n",
		user_caller_uid
	);

	user_incomplete_caller = 0;
}

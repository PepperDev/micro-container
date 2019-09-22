#include <unistd.h>

#include "proc.h"

char proc_get_user(pid_t pid, uid_t *uid, gid_t *gid)
{
	// TODO: pid_t ppid = getppid(); ...read /proc/%d/status
	return 0;
}

pid_t proc_get_parent(pid_t pid)
{
	return 0;
}

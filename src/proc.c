#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "proc.h"
#include "io.h"

char proc_get_user(pid_t pid, uid_t *uid, gid_t *gid)
{
	size_t size, left, line_len;
	char file[32], *buf = NULL, *p, *next, found_uid = 0, found_gid = 0;
	unsigned int val1, val2;

	snprintf(file, 32, "/proc/%d/status", pid);
	size = io_readfile(file, &buf);
	if (size == 0 || buf == NULL)
	{
		return 0;
	}

	left = size;
	p = buf;
	while (left > 0 && p != NULL)
	{
		next = memchr(p, '\n', left);
		if (next)
		{
			line_len = next - p;
			*next = 0;
			next++;
		}
		else
		{
			line_len = left;
		}

		if (line_len > 4)
		{
			if (memcmp(p, "Uid:", 4) == 0)
			{
				sscanf(p, "Uid: %u %u %*s", &val1, &val2);
				if (val1 == 0)
				{
					val1 = val2;
				}
				*uid = val1;
				found_uid = 1;
				if (found_gid)
				{
					break;
				}
			}
			else if (memcmp(p, "Gid:", 4) == 0)
			{
				sscanf(p, "Gid: %u %u %*s", &val1, &val2);
				if (val1 == 0)
				{
					val1 = val2;
				}
				*gid = val1;
				found_gid = 1;
				if (found_uid)
				{
					break;
				}
			}
		}

		p = next;
		left -= line_len + 1;
	}
	free(buf);
	return found_uid && found_gid;
}

pid_t proc_get_parent(pid_t pid)
{
	char path[32], *buf = NULL;
	size_t size;
	pid_t ppid;

	snprintf(path, 32, "/proc/%d/stat", pid);

	size = io_readfile(path, &buf);
	if (size == 0 || buf == NULL)
	{
		return 0;
	}

	sscanf(buf, "%*d %*s %*c %d %*s", &ppid);
	free(buf);
	return ppid;
}

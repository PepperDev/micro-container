#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <libgen.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <sys/sysmacros.h>

#include "io.h"

#define IO_BUFSIZE  4096
#define IO_STEPSIZE 1024

static char overlay_read = 0,
	overlay_supported = 0;

char* io_realpath(const char *path)
{
	char *resolved = malloc(PATH_MAX);
	assert(resolved != NULL);
	char *result = realpath(path, resolved);
	//errno == ENAMETOOLONG
	if (result != resolved)
	{
		free(resolved);
		return NULL;
	}
	return resolved;
}

char io_exists(const char *path)
{
	return !access(path, F_OK);
}

char io_isdir(const char *path)
{
	struct stat fst;
	return !access(path, X_OK) &&
		!stat(path, &fst) &&
		S_ISDIR(fst.st_mode);
}

char io_isrunnable(const char *path)
{
	struct stat fst;
	return !access(path, X_OK) &&
		!stat(path, &fst) &&
		!S_ISDIR(fst.st_mode);
}

char io_islink(const char *path)
{
	struct stat fst;
	return !access(path, F_OK) &&
		!stat(path, &fst) &&
		S_ISLNK(fst.st_mode);
}

char io_isrotational(const char *path)
{
	struct stat fst;
	if (access(path, F_OK) || stat(path, &fst))
	{
		return 0;
	}
	char *rpath = malloc(65);
	assert(rpath != NULL);
	assert(snprintf(
		rpath,
		65,
		"/sys/dev/block/%d:%d/../queue/rotational",
		major(fst.st_dev),
		minor(fst.st_dev)
	) < 65);
	char *buf;
	return access(rpath, F_OK) || io_readfile(rpath, &buf) < 1 ||
		buf == NULL || buf[0] == 0 || buf[0] != '0';
}

char io_symlink(const char *target, const char *path)
{
	return !symlink(target, path);
}

char io_mkdir(
	const char *path,
	char usermode,
	uid_t uid,
	gid_t gid
)
{
	mode_t old_mask;
	char *copy, *parent, ret;

	if (!access(path, F_OK))
	{
		return io_isdir(path);
	}

	copy = strdup(path);
	parent = dirname(copy);
	ret = io_mkdir(parent, usermode, uid, gid);
	free(copy);
	if (!ret)
	{
		return 0;
	}

	if (!usermode)
	{
		old_mask = umask(0);
	}
	ret = 1;
	if (mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH))
	{
		ret = 0;
	}
	if (usermode)
	{
		chown(path, uid, gid);
	}
	else
	{
		umask(old_mask);
	}
	return ret;
}

char io_addmod(
	const char *path,
	mode_t mode,
	uid_t uid,
	gid_t gid
)
{
	struct stat fst;

	if (stat(path, &fst))
	{
		return 0;
	}

	if (fst.st_uid != uid || fst.st_gid != gid)
	{
		if (chown(path, uid, gid))
		{
			return 0;
		}
	}

	if ((fst.st_mode & mode) == mode)
	{
		return 1;
	}

	if (chmod(path, fst.st_mode | mode))
	{
		return 0;
	}
	return 1;
}

size_t io_readfile(const char *file, char **buffer)
{
	int fd;
	char *buf, *p, fullsize = 0;
	off_t max, left;
	ssize_t bread;
	struct stat st;

	*buffer = NULL;
	for (;;)
	{
		errno = 0;
		fd = open(file, O_RDONLY);
		if (fd >= 0)
		{
			break;
		}
		else if (errno != EINTR)
		{
			return 0;
		}
	}

	if (fstat(fd, &st) || st.st_size == 0)
	{
		max = IO_BUFSIZE;
	}
	else
	{
		max = st.st_size;
		fullsize = 1;
	}

	left = max;
	buf = malloc(max + 1);
	p = buf;
	for (;;)
	{
		errno = 0;
		bread = read(fd, p, left);
		if (bread > 0)
		{
			p += bread;
			left -= bread;
			if (left <= 0)
			{
				if (fullsize)
				{
					break;
				}
				left += IO_STEPSIZE;
				max += IO_STEPSIZE;
				buf = realloc(buf, max + 1);
				p = buf + max - left;
			}
		}
		else if (errno != 0 && errno != EINTR && errno != EAGAIN)
		{
			free(buf);
			close(fd);
			return 0;
		}
		else if (bread == 0 && errno == 0)
		{
			break;
		}
	}
	close(fd);
	*p = 0;
	*buffer = buf;
	return p - buf;
}

char io_isoverlaysupported()
{
	if (!overlay_read)
	{
		struct utsname suname;
		if (!uname(&suname))
		{
			int major, minor;
			sscanf(suname.release, "%d.%d.%*s", &major, &minor);
			overlay_supported = major > 3 || (major == 3 && minor >= 18);
			overlay_read = 1;
		}
	}
	return overlay_supported;
}

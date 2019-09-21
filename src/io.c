#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <libgen.h>

#include "io.h"

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

char io_isdir(const char *path)
{
	struct stat fst;
	return !access(path, X_OK) &&
		!stat(path, &fst) &&
		S_ISDIR(fst.st_mode);
}

char io_mkdir(const char *path, char usermode)
{
	mode_t old_mask;
	char *copy, *parent, ret;

	if (io_isdir(path))
	{
		return 1;
	}
	if (!access(path, F_OK))
	{
		return 0;
	}

	copy = strdup(path);
	parent = dirname(copy);
	ret = io_mkdir(parent, usermode);
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
	if (!usermode)
	{
		umask(old_mask);
	}
	return ret;
}

char io_addmod(const char *path, unsigned int mode)
{
	struct stat fst;

	if (stat(path, &fst))
	{
		return 0;
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

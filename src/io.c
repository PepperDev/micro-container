#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

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

char io_isrfile(const char *path)
{
	// TODO: ...
}

char io_mkdir(const char *path)
{
	// TODO: ...
}

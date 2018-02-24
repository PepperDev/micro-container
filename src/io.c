#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>

char mkdirr(const char *path) {
	mode_t old_mask;
	char *copy, *parent, ret;
	if (!access(path, F_OK)) {
		return 0;
	}
	copy = strdup(path);
	parent = dirname(copy);
	mkdirr(parent);
	free(copy);
	old_mask = umask(0);
	ret = (char)mkdir(path, 0755);
	umask(old_mask);
	return ret;
}

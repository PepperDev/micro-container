#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>

static char is_dir(const char *);
static char is_exe(const char *);

char mkdirr(const char *path) {
	mode_t old_mask;
	char *copy, *parent, ret;
	if (is_dir(path)) {
		return 0;
	}
	if (!access(path, F_OK)) {
		return -1;
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

char* which(const char*name) {
	char *path, *current, *next, *found, *p;
	size_t len, max;
	path = getenv("PATH");
	if (path == NULL) {
		path = "/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin";
	}
	path = strdup(path);
	found = NULL;
	len = strlen(name);
	for (current = path; current; current = next) {
		next = strchr(current, ':');
		if (next) {
			*next = 0;
			if (next == current) {
				next++;
				continue;
			}
			max = next - current;
			next++;
		} else {
			max = strlen(current);
		}
		found = malloc(max + len + 2);
		memcpy(found, current, max);
		p = found + max;
		*p = '/';
		p++;
		memcpy(p, name, len + 1);
		if (is_exe(found)) {
			break;
		}
		free(found);
		found = NULL;
	}
	free(path);
	return found;
}

static char is_dir(const char *path) {
	struct stat fst;
	return !access(path, X_OK) &&
		!stat(path, &fst) &&
		S_ISDIR(fst.st_mode);
}

static char is_exe(const char *path) {
	struct stat fst;
	return !access(path, X_OK) &&
		!stat(path, &fst) &&
		S_ISREG(fst.st_mode);
}

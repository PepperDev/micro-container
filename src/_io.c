#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#define BUFSIZE  4096
#define STEPSIZE 1024

char is_dir(const char *);
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

char is_dir(const char *path) {
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

char* readfile(const char *file) {
	int fd;
	char *buf, *p;
	off_t max, left;
	ssize_t bread;
	struct stat st;
	for (;;) {
		errno = 0;
		fd = open(file, O_RDONLY);
		if (fd >= 0) {
			break;
		} else if (errno != EINTR) {
			return NULL;
		}
	}
	if (fstat(fd, &st)) {
		max = BUFSIZE;
	} else {
		max = st.st_size;
		if (max == 0) {
			max = BUFSIZE;
		}
	}
	left = max;
	buf = malloc(max + 1);
	p = buf;
	for (;;) {
		errno = 0;
		bread = read(fd, p, left);
		if (bread > 0) {
			p += bread;
			left -= bread;
			if (left <= 0) {
				left += STEPSIZE;
				max += STEPSIZE;
				buf = realloc(buf, max + 1);
				p = buf + max - left;
			}
		} else if (errno != 0 && errno != EINTR && errno != EAGAIN) {
			free(buf);
			close(fd);
			return NULL;
		} else {
			break;
		}
	}
	close(fd);
	*p = 0;
	return buf;
}

void stream_copy(int fd0, int fd1) {
	ssize_t size, len;
	char buf[BUFSIZE], *p;
	// TODO: check for read intr too
	while ((size = read(fd0, buf, BUFSIZE)) > 0) {
		p = buf;
		do {
			errno = 0;
			len = write(fd1, p, size);
			if (len >= 0) {
				size -= len;
				p += len;
			} else if (errno != 0 && errno != EINTR && errno != EAGAIN) {
				fprintf(stderr, "Could not write to file\n");
				close(fd0);
				close(fd1);
				exit(-1);
				return;
			}
		} while (size > 0);
	}
}

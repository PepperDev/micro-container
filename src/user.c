#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "uid.h"
#include "var.h"

static uid_t my_getpuid(const char*, pid_t);
static pid_t my_getppid(const char*, pid_t);

void calc_user(const char *root, char **home, uid_t *uid, gid_t *gid) {
	size_t len;
	FILE *file;
	pid_t pid;
	char *etc_passwd;
	struct passwd *passwd;
	len = strlen(root);
	etc_passwd = malloc(len + 12);
	memcpy(etc_passwd, root, len);
	memcpy(etc_passwd + len, "/etc/passwd", 12);
	file = fopen(etc_passwd, "r");
	if (file == NULL) {
		user = "root";
		*uid = 0;
		*gid = 0;
		*home = strdup("/root");
	} else {
		if (user != NULL && *user != 0) {
			*home = NULL;
			while ((passwd = fgetpwent(file)) != NULL) {
				if (strcmp(passwd->pw_name, user) == 0) {
					*uid = passwd->pw_uid;
					*gid = passwd->pw_gid;
					*home = strdup(passwd->pw_dir);
					break;
				}
			}
			if (*home == NULL) {
				user = NULL;
				rewind(file);
			}
		}
		if (user == NULL || *user == 0) {
			if (old_uid == 0) {
				pid = getppid();
				old_uid = my_getpuid(root, pid);
				if (old_uid == 0) {
					pid = my_getppid(root, pid);
					old_uid = my_getpuid(root, pid);
				}
			}
			*uid = old_uid;
			*home = NULL;
			while ((passwd = fgetpwent(file)) != NULL) {
				if (passwd->pw_uid == *uid) {
					user = strdup(passwd->pw_name);
					*gid = passwd->pw_gid;
					*home = strdup(passwd->pw_dir);
					break;
				}
			}
			if (*home == NULL) {
				user = "root";
				*uid = 0;
				*gid = 0;
				*home = strdup("/root");
			}
		}
		fclose(file);
	}
}

static uid_t my_getpuid(const char *root, pid_t pid) {
	size_t len;
	int fd;
	char *file, *buf, *p;
	struct stat st;
	off_t left;
	ssize_t bread;
	len = strlen(root);
	file = malloc(len + 32);
	snprintf(file, len + 32, "%s/proc/%d/status", root, pid);
	fd = open(file, O_RDONLY);
	free(file);
	if (fd == -1) {
		return 0;
	}
	if (fstat(fd, &st)) {
		close(fd);
		return 0;
	}
	left = st.st_size;
	buf = malloc(left + 1);
	p = buf;
	while (left > 0) {
		bread = read(fd, p, left);
		if (bread > 0) {
			p += bread;
			left -= bread;
		} else if (errno != EINTR) {
			free(buf);
			close(fd);
			return 0;
		}
	}
	close(fd);
	*p = 0;
	// TODO: Seek Uid:
	free(buf);
	return 0;
}

static pid_t my_getppid(const char *root, pid_t pid) {
	// TODO: get parent of parent -> read 4th /proc/pid/stat
	return pid;
}

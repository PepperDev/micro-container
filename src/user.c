#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "uid.h"
#include "var.h"
#include "io.h"

static void find_user(const char*, const char*, const uid_t*, char**,
	uid_t*, gid_t*, char**);
static uid_t my_getpuid(const char*, pid_t);
static pid_t my_getppid(const char*, pid_t);

void calc_user(const char *root, char **home, uid_t *uid, gid_t *gid) {
	size_t len;
	pid_t pid;
	char *file, *buf;
	len = strlen(root);
	file = malloc(len + 12);
	memcpy(file, root, len);
	memcpy(file + len, "/etc/passwd", 12);
	buf = readfile(file);
	if (buf == NULL) {
		user = "root";
		*uid = 0;
		*gid = 0;
		*home = strdup("/root");
		return;
	}
	if (user != NULL && *user != 0) {
		*home = NULL;
		find_user(buf, user, NULL, NULL, uid, gid, home);
		if (*home == NULL) {
			user = NULL;
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
		find_user(buf, NULL, uid, &user, NULL, gid, home);
	}
	free(buf);
	if (*home == NULL) {
		user = "root";
		*uid = 0;
		*gid = 0;
		*home = strdup("/root");
	}
}

static void find_user(const char *obuf, const char *user, const uid_t *uid,
	char **found_user, uid_t *found_uid, gid_t *gid, char **home) {
	char *buf, *p, *next, *tok, found;
	size_t len;
	unsigned int line_uid;
	buf = strdup(obuf);
	if (user != NULL) {
		len = strlen(user);
	}
	for (p = buf; p != NULL; p = next) {
		next = strchr(p, '\n');
		if (next) {
			*next = 0;
			next++;
		}
		if (next - p <= 1) {
			continue;
		}
		found = 1;
		if (user != NULL) {
			tok = strchr(p, ':');
			if (tok - p != len || memcmp(user, p, len) != 0) {
				found = 0;
			}
		}
		if (uid != NULL) {
			sscanf(p, "%*[^:]:%*[^:]:%u:%*s", &line_uid);
			if (line_uid != *uid) {
				found = 0;
			}
		}
		if (found) {
			if (found_user != NULL) {
				if (user != NULL) {
					if (*found_user != user) {
						*found_user = strdup(user);
					}
				} else {
					tok = strchr(p, ':');
					len = tok - p;
					*found_user = malloc(len + 1);
					memcpy(*found_user, p, len);
					(*found_user)[len] = 0;
				}
			}
			if (found_uid != NULL) {
				if (uid != NULL) {
					*found_uid = *uid;
				} else {
					sscanf(p, "%*[^:]:%*[^:]:%d:%*s", found_uid);
				}
			}
			if (gid != NULL) {
				sscanf(p, "%*[^:]:%*[^:]:%*[^:]:%d:%*s", gid);
			}
			if (home != NULL) {
				for (int i = 5; i--;) {
					p = strchr(p, ':');
					if (p == NULL) {
						break;
					}
					p++;
				}
				if (p != NULL) {
					next = strchr(p, ':');
					if (next != NULL) {
						len = next - p;
						*home = malloc(len + 1);
						memcpy(*home, p, len);
						(*home)[len] = 0;
					}
				}
			}
			break;
		}
	}
	free(buf);
}

static uid_t my_getpuid(const char *root, pid_t pid) {
	size_t len;
	char *file, *buf, *p, *next;
	uid_t uid, euid;
	len = strlen(root);
	file = malloc(len + 32);
	snprintf(file, len + 32, "%s/proc/%d/status", root, pid);
	buf = readfile(file);
	free(file);
	if (buf == NULL) {
		return 0;
	}
	uid = 0;
	for (p = buf; p != NULL; p = next) {
		next = strchr(p, '\n');
		if (next) {
			*next = 0;
			next++;
		}
		if (strlen(p) > 4 && memcmp(p, "Uid:", 4) == 0) {
			sscanf(p, "Uid: %d %d %*s", &uid, &euid);
			if (uid == 0) {
				uid = euid;
			}
			break;
		}
	}
	free(buf);
	return uid;
}

static pid_t my_getppid(const char *root, pid_t pid) {
	char *path, *buf;
	size_t len;
	pid_t ppid;
	len = strlen(root);
	path = malloc(len + 32);
	snprintf(path, len + 32, "%s/proc/%d/stat", root, pid);
	buf = readfile(path);
	free(path);
	if (buf == NULL) {
		return pid;
	}
	sscanf(buf, "%*d %*s %*c %d %*s", &ppid);
	free(buf);
	return ppid;
}

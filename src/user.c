#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <errno.h>
#include <dirent.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>

#include "uid.h"
#include "var.h"
#include "io.h"

static void find_user(const char*, const char*, const uid_t*, char**,
	uid_t*, gid_t*, char**);
static uid_t my_getpuid(const char*, pid_t);
static pid_t my_getppid(const char*, pid_t);
static void copy_dir(const char*, const char*, uid_t, gid_t);
static void copy_home_file(const char*, const char*, const char*, uid_t uid,
	gid_t gid);
static void copy_file(const char*, const char*, uid_t, gid_t);

void calc_user(const char *root, char **home, uid_t *uid, gid_t *gid) {
	size_t len;
	pid_t pid;
	char *file, *buf;
	len = strlen(root);
	if (old_uid == 0) {
		pid = getppid();
		old_uid = my_getpuid(root, pid);
		if (old_uid == 0) {
			pid = my_getppid(root, pid);
			old_uid = my_getpuid(root, pid);
		}
	}
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

void create_home(const char *root, const char *home, uid_t uid,
	gid_t gid) {
	size_t len0, len1;
	char *roothome, *rootskel, *duphome, *parent, *buf, *userhome;
	len0 = strlen(root);
	len1 = strlen(home);
	roothome = malloc(len0 + len1 + 1);
	memcpy(roothome, root, len0);
	memcpy(roothome + len0, home, len1 + 1);
	if (is_dir(roothome)) {
		free(roothome);
		return;
	}
	rootskel = malloc(len0 + 10);
	memcpy(rootskel, root, len0);
	memcpy(rootskel + len0, "/etc/skel", 10);
	if (is_dir(rootskel)) {
		duphome = strdup(roothome);
		parent = dirname(duphome);
		if (!is_dir(parent)) {
			if (mkdirr(parent)) {
				fprintf(stderr, "Could not create dir \"%s\"\n", parent);
				exit(-1);
			}
		}
		free(duphome);
		copy_dir(rootskel, roothome, uid, gid);
	} else {
		if (mkdirr(roothome)) {
			fprintf(stderr, "Could not create dir \"%s\"\n", roothome);
			exit(-1);
		}
	}
	free(rootskel);
	buf = readfile("/etc/passwd");
	if (buf) {
		find_user(buf, NULL, &old_uid, NULL, NULL, NULL, &userhome);
		free(buf);
		if (userhome == NULL) {
			userhome = strdup(home);
		}
	} else {
		userhome = strdup(home);
	}
	copy_home_file(userhome, roothome, ".inputrc", uid, gid);
	copy_home_file(userhome, roothome, ".vimrc", uid, gid);
	copy_home_file(userhome, roothome, ".gitconfig", uid, gid);
	copy_home_file(userhome, roothome, ".screenrc", uid, gid);
	free(roothome);
	free(userhome);
}

static void copy_dir(const char *from, const char *to, uid_t uid,
	gid_t gid) {
	struct stat st;
	mode_t mod, mask;
	DIR *dir;
	struct dirent *dent, *ref;
	char *from_file, *to_file, *p;
	long max;
	size_t len0, len1, len_name;
	if (stat(from, &st)) {
		mod = 0755;
	} else {
		mod = st.st_mode;
	}
	mask = umask(0);
	if (mkdir(to, mod)) {
		fprintf(stderr, "Could not create directory \"%s\"\n", to);
		exit(-1);
	}
	umask(mask);
	if (chown(to, uid, gid)) {
		fprintf(stderr, "Could set owner for directory \"%s\"\n", to);
		exit(-1);
	}
	dir = opendir(from);
	if (dir == NULL) {
		fprintf(stderr, "Could not open directory \"%s\"\n", from);
	}
	max = pathconf(from, _PC_NAME_MAX);
	if (max < 0) {
		max = 255;
	}
	len0 = offsetof(struct dirent, d_name) + max + 1;
	dent = malloc(len0);
	len0 = strlen(from);
	len1 = strlen(to);
	while (!readdir_r(dir, dent, &ref) && ref != NULL) {
		if (dent->d_name[0] == '.' && (dent->d_name[1] == 0 ||
			(dent->d_name[1] == '.' && dent->d_name[2] == 0))) {
			continue;
		}
		len_name = strlen(dent->d_name);
		from_file = malloc(len0 + len_name + 2);
		memcpy(from_file, from, len0);
		p = from_file + len0;
		*p = '/';
		p++;
		memcpy(p, dent->d_name, len_name + 1);
		to_file = malloc(len1 + len_name + 2);
		memcpy(to_file, to, len1);
		p = to_file + len1;
		*p = '/';
		p++;
		memcpy(p, dent->d_name, len_name + 1);
		if (is_dir(from_file)) {
			copy_dir(from_file, to_file, uid, gid);
		} else {
			copy_file(from_file, to_file, uid, gid);
		}
		free(from_file);
		free(to_file);
	}
	free(dent);
	closedir(dir);
}

static void copy_home_file(const char *from, const char *to,
	const char *file, uid_t uid, gid_t gid) {
	char *from_file, *to_file, *p;
	size_t len0, len1, len2;
	len0 = strlen(from);
	len1 = strlen(to);
	len2 = strlen(file);
	from_file = malloc(len0 + len2 + 2);
	memcpy(from_file, from, len0);
	p = from_file + len0;
	*p = '/';
	p++;
	memcpy(p, file, len2 + 1);
	to_file = malloc(len1 + len2 + 2);
	memcpy(to_file, to, len1);
	p = to_file + len1;
	*p = '/';
	p++;
	memcpy(p, file, len2 + 1);
	if (!access(from_file, F_OK)) {
		if (access(to_file, F_OK)) {
			copy_file(from_file, to_file, uid, gid);
		}
	}
	free(from_file);
	free(to_file);
}

static void copy_file(const char *from, const char *to, uid_t uid, gid_t gid) {
	int fd0, fd1;
	mode_t mode;
	fd0 = open(from, O_RDONLY);
	if (fd0 < 0) {
		fprintf(stderr, "Could not open file \"%s\" for read.\n", from);
		exit(-1);
		return;
	}
	mode = umask(0);
	fd1 = open(to, O_WRONLY | O_CREAT | O_EXCL, 0644);
	umask(mode);
	if (fd1 < 0) {
		fprintf(stderr, "Could not open file \"%s\" for write.\n", to);
		close(fd0);
		exit(-1);
		return;
	}
	if (fchown(fd1, uid, gid)) {
		fprintf(stderr, "Could not set owner %d:%d to file \"%s\"\n", (int)uid, (int)gid, to);
		close(fd0);
		close(fd1);
		exit(-1);
		return;
	}
	stream_copy(fd0, fd1);
	close(fd0);
	close(fd1);
}

#include <sys/mount.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "var.h"
#include "uid.h"
#include "config_default.h"
#include "user.h"
#include "io.h"

// TODO: volumes, x11, pulse
// /proc/self/mountinfo \040 -> space

static char* make_point(const char*, const char*);
static void mount_bind(const char*, const char*);
static void mount_type(const char*, const char*, const char*, const char*);
static void clear_dir(const char*, const char*);
static char is_ssd(const char*);
static void mount_tmp(const char*, const char*, const char*);
static void root_mkdir(const char*, const char*);
static void root_install(const char*, const char*, const char*, const char*,
	mode_t);
static void create_shm(const char*);
static void copy_file(const char*, const char*, const char*);

void container(const char *root) {
	uid_t uid;
	gid_t gid;
	char *home;
	calc_user(root, &home, &uid, &gid);
	create_home(root, home, uid, gid);
	mount_bind(root, "/dev");
	mount_bind(root, "/dev/pts");
	if (shared) {
		mount_type(root, "proc", "none", "/proc");
	} else {
		// TODO: ...
	}
	mount_type(root, "sysfs", "sys", "/sys");
	clear_dir(root, "/tmp");
	clear_dir(root, "/var/tmp");
	if (is_ssd(root)) {
		mount_tmp(root, "nodev,nosuid,noatime", "/tmp");
		mount_tmp(root, "nodev,nosuid,noatime", "/var/tmp");
	}
	mount_tmp(root, "rw,noexec,nosuid,mode=0755", "/run");
	root_mkdir(root, "/run/lock");
	root_mkdir(root, "/run/user");
	root_mkdir(root, "/run/resolvconf");
	create_shm(root);
	mount_tmp(root, "rw,noexec,nosuid,nodev", "/run/lock");
	// TODO: if systemd create dir /.. and mount tmpfs
	root_install(root, "/run/screen", "root", "utmp", 0775);
	copy_file("/etc/resolv.conf", root, "/run/resolvconf/resolv.conf");
	// TODO: create dir 755 root:utmp /run/screen
	// TODO: copy from host:/etc/resolv.conf to container:/run/resolvconf/resolv.conf and create link if it does not exists
	// TODO: write pid and lock files
	// TODO: perform chroot, unbind and launch application, if it is the first instance clone2 and use execv, if is not use just execv
	// TODO: to unbind use setns
	// TODO: to launch use setuid and setgid
	// TODO: for the first instance use signals (sigaction) and the lock file to control other instances
}

static char* make_point(const char *root, const char *point) {
	size_t len0, len1;
	char *path;
	len0 = strlen(root);
	len1 = strlen(point);
	path = malloc(len0 + len1 + 1);
	memcpy(path, root, len0);
	memcpy(path + len0, point, len1 + 1);
	return path;
}

static void mount_bind(const char *root, const char *point) {
	char *path;
	path = make_point(root, point);
	if (mount(point, path, NULL, MS_BIND, NULL)) {
		fprintf(stderr, "Could not mount bind of \"%s\" on \"%s\".\n", point, root);
		exit(-1);
	}
	// TODO: --make-private --make-unbindable
	free(path);
}

static void mount_type(const char *root, const char *type, const char *source,
	const char *point) {
	char *path;
	path = make_point(root, point);
	if (mount(source, path, type, 0, NULL)) {
		fprintf(stderr, "Could not mount type \"%s\" on \"%s\".\n", type, path);
		exit(-1);
	}
	free(path);
}

static void clear_dir(const char *root, const char *dir) {
	// TODO: recursive delete
}

static char is_ssd(const char *root) {
	/*
		disk=$(findmnt / | awk 'NR > 1 { gsub(/^.*\/|[0-9]*$/, "", $2); print $2
 }')
        file="/sys/block/${disk:-sda}/queue/rotational"
        if [ ! -e "$file" -a "${disk%p}p" = "$disk" ]; then
                file="/sys/block/${disk%p}/queue/rotational"
        fi
        if [ -e "$file" ] && [ "$(cat "$file")" = 0 ]; then
	*/
	return 0;
}

static void mount_tmp(const char *root, const char *opt, const char *point) {
	char *path;
	path = make_point(root, dir);
	if (mount("tmpfs", path, "tmpfs", 0, opt)) {
		fprintf(stderr, "Could not mount tmpfs on \"%s\".\n", point);
		exit(-1);
	}
	free(path);
}

static void root_mkdir(const char *root, const char *dir) {
	char *path;
	path = make_point(root, dir);
	mkdirr(path);
	free(path);
}

static void root_install(const char *root, const char *dir, const char *user,
	const char *group, 	mode_t mode) {
	// TODO: do it
}

static void create_shm(const char *root) {
	/*if (is_link("/dev/shm")) {
	// TODO: if /dev/shm is link resolve it till the end, if the result does not exists create dir, if is not link create it to /run/shm, on the end mount tmpfs
	}*/
}

static void copy_file(const char *from, const char *root, const char *to) {
	// TODO: ...
}

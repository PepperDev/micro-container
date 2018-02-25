#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <string.h>
#include <sys/mount.h>

#include "var.h"
#include "io.h"
#include "config.h"

// TODO: volumes, x11, pulse
// /proc/self/mountinfo \040 -> space
// mount , -> \,

static char* check_library();
static char is_overlay_supported();
static char* make_component(char*, size_t, char*, size_t);

void mount_container() {
	char *app, *upper, *work, *root, *opt, *p;
	size_t len, len_comp, len_dir, len_name, max;
	app = check_library();
	len = strlen(app);

	if (!hardcp) {
		// Upper
		upper = make_component(app, len,
			UPPER_COMPONENT, sizeof(UPPER_COMPONENT) - 1);

		// Work
		if (is_overlay_supported()) {
			work = make_component(app, len,
				WORK_COMPONENT, sizeof(WORK_COMPONENT) - 1);
		} else {
			work = NULL;
		}
	}

	// Lower
	if (lower == NULL || *lower == 0) {
		len_dir = strlen(dir);
		if (name == NULL || *name == 0) {
			len_name = 0;
		} else {
			len_name = strlen(name);
		}
		len_comp = sizeof(ROOT_PREFFIX) - 1;
		lower = malloc(len_dir + len_name + len_comp + 2);
		memcpy(lower, dir, len_dir);
		p = lower + len_dir;
		*p = '/';
		p++;
		memcpy(p, ROOT_PREFFIX, len_comp);
		p += len_comp;
		if (len_name) {
			memcpy(p, name, len_name + 1);
		} else {
			*p = 0;
		}
		if (!is_dir(lower)) {
			free(lower);
			lower = DEFAULT_LOWER;
		}
	}

	// Root
	root = make_component(app, len, MOUNT_POINT, sizeof(MOUNT_POINT) - 1);

	// TODO: write PID_FILE, lock LOCK_FILE
	if (hardcp) {
		if (!is_dir(root)) {
			// TODO: perform copy of lower instead of overlay
		}
	} else {
		if (mkdirr(root)) {
			fprintf(stderr, "Could not create root dir \"%s\"!\n", root);
			exit(-1);
		}
		if (mkdirr(upper)) {
			fprintf(stderr, "Could not create upper dir \"%s\"!\n", upper);
			exit(-1);
		}
		if (work == NULL) {

		} else {
			if (mkdirr(work)) {
				fprintf(stderr, "Could not create work dir \"%s\"!\n", work);
				exit(-1);
			}
			len_comp = strlen(lower);
			len_dir = strlen(upper);
			len_name = strlen(work);
			max = len_comp + len_dir + len_name + 29;
			opt = malloc(max);
			snprintf(opt, max, "lowerdir=%s,upperdir=%s,workdir=%s", lower, upper, work);
			if (mount("overlay", root, "overlay", 0, opt)) {
				fprintf(stderr, "Could not mount container with opt \"%s\"!\n", opt);
				exit(-1);
			}
		}
	}
	//clone2
	//execve
	//setns(int fd, int nstype);
	//sigaction
}

static char* check_library() {
	char *app, *p;
	size_t len, len_name, len_preffix, len_separator, max;
	if (dir == NULL || *dir == 0) {
		dir = DEFAULT_DIR;
	}
	len = strlen(dir);
	if (name == NULL || *name == 0) {
		len_name = 0;
	} else {
		len_name = strlen(name);
	}
	len_preffix = sizeof(PREFFIX) - 1;
	len_separator = sizeof(PREFFIX_SEPARATOR) - 1;
	max = len + len_name + len_preffix + len_separator + 2;
	app = malloc(max);
	memcpy(app, dir, len);
	p = app + len;
	*p = '/';
	p++;
	if (len_name) {
		memcpy(p, PREFFIX, len_preffix);
		p += len_preffix;
		memcpy(p, PREFFIX_SEPARATOR, len_separator);
		p += len_separator;
		memcpy(p, name, len_name + 1);
	} else {
		memcpy(p, PREFFIX, len_preffix + 1);
	}
	if (mkdirr(app)) {
		fprintf(stderr, "Could not create container dir \"%s\"!\n", app);
		exit(-1);
	}
	return app;
}

static char is_overlay_supported() {
	struct utsname suname;
	int major, minor;
	if (!uname(&suname)) {
		sscanf(suname.release, "%d.%d.%*s", &major, &minor);
		return major > 3 || major == 3 && minor >= 18;
	}
	return 0;
}

static char* make_component(char *dir, size_t len_dir,
	char *comp, size_t len_comp) {
	char *path, *p;
	path = malloc(len_dir + len_comp + 2);
	memcpy(path, dir, len_dir);
	p = path + len_dir;
	*p = '/';
	p++;
	memcpy(p, comp, len_comp + 1);
	return path;
}

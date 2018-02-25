#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <string.h>

#include "var.h"
#include "io.h"
#include "config.h"

// TODO: volumes, x11, pulse
// /proc/self/mountinfo \040 -> space
// mount , -> \,

static char* check_library();
static char is_overlay_supported();

void mount_container() {
	char *app, *upper, *work, *root, *p;
	size_t len, len_comp, len_dir, len_name;
	app = check_library();
	len = strlen(app);

	// Upper
	len_comp = sizeof(UPPER_COMPONENT);
	upper = malloc(len + len_comp + 1);
	memcpy(upper, app, len);
	p = upper + len;
	*p = '/';
	p++;
	memcpy(p, UPPER_COMPONENT, len_comp);

	// Work
	if (!hardcp && is_overlay_supported()) {
		len_comp = sizeof(WORK_COMPONENT);
		work = malloc(len + len_comp + 1);
		memcpy(work, app, len);
		p = work + len;
		*p = '/';
		p++;
		memcpy(p, WORK_COMPONENT, len_comp);
	} else {
		work = NULL;
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
	len_comp = sizeof(MOUNT_POINT);
	root = malloc(len + len_comp + 1);
	memcpy(root, app, len);
	p = root + len;
	*p = '/';
	p++;
	memcpy(p, MOUNT_POINT, len_comp);

	// TODO: write PID_FILE, lock LOCK_FILE
	// TODO: if (hardcp) copy root instead of overlay
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

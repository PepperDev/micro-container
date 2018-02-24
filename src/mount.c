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
	char *app, *upper, *work, *root;
	size_t len;
	app = check_library();
	len = strlen(app);
	if (lower == NULL || *lower == 0) {
		lower = DEFAULT_LOWER;
	}
	upper = malloc(len + sizeof(UPPER_COMPONENT) + 1);
	memcpy(upper, app, len);
	upper[len] = '/';
	memcpy(&upper[len + 1], UPPER_COMPONENT, sizeof(UPPER_COMPONENT));
	if (is_overlay_supported()) {
		work = malloc(len + sizeof(WORK_COMPONENT) + 1);
		memcpy(work, app, len);
		work[len] = '/';
		memcpy(&work[len + 1], WORK_COMPONENT, sizeof(WORK_COMPONENT));
	} else {
		work = NULL;
	}
	//clone2
	//execve
	//setns(int fd, int nstype);
	//sigaction
}

static char* check_library() {
	char *app;
	size_t len, len_name, max;
	if (dir == NULL || *dir == 0) {
		dir = DEFAULT_DIR;
	}
	len = strlen(dir);
	if (name == NULL || *name == 0) {
		len_name = 0;
	} else {
		len_name = strlen(name);
	}
	max = len + len_name + sizeof(PREFFIX) + sizeof(PREFFIX_SEPARATOR);
	app = malloc(max);
	if (len_name) {
		sprintf(app, "%s/%s%s%s", dir, PREFFIX, PREFFIX_SEPARATOR, name);
	} else {
		sprintf(app, "%s/%s", dir, PREFFIX);
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

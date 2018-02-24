#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include "var.h"
#include "io.h"
#include "config.h"

// TODO: volumes, x11, pulse
// /proc/self/mountinfo \040 -> space
// mount , -> \,

static void check_credential();
static void check_library();

void mount_container() {
	check_credential();
	check_library();
	//clone2
	//execve
	//setns(int fd, int nstype);
	//sigaction
}

static void check_credential() {
	char *sudo;
	if (getuid() == 0) {
		return;
	}
	sudo = "/usr/bin/sudo";
	if (access(sudo, X_OK)) {
		sudo = "/usr/sbin/sudo";
		if (access(sudo, X_OK)) {
			sudo = NULL;
		}
	}
	if (sudo) {
	}
	// check sudo
	fprintf(stderr, "It requires to be run as root to work.\n");
	exit(-1);
}

static void check_library() {
	if (dir == NULL || *dir == 0) {
		dir = DEFAULT_DIR;
	}
	if (mkdirr(dir)) {
		fprintf(stderr, "Could not create container library dir \"%s\"!\n", dir);
		exit(-1);
	}
}

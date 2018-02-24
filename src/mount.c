#include <stdio.h>
#include <stdlib.h>

#include "var.h"
#include "io.h"
#include "config.h"

// TODO: volumes, x11, pulse
// /proc/self/mountinfo \040 -> space
// mount , -> \,

static void check_library();

void mount_container() {
	check_library();
	//clone2
	//execve
	//setns(int fd, int nstype);
	//sigaction
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

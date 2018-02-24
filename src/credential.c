#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include "io.h"

void check_credential(int argc, char* argv[]) {
	char *sudo;
	if (getuid() == 0) {
		return;
	}
	sudo = which("sudo");
	if (sudo) {
		// exec sudo
	}
	fprintf(stderr, "It requires to be run as root to work.\n");
	exit(-1);
}

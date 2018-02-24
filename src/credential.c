#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include "io.h"

void check_credential(int argc, char* argv[]) {
	char *sudo;
	if (getuid() == 0) {
		return;
	}
	sudo = which("sudo");
	if (sudo) {
		char **args = malloc((argc + 3) * sizeof(char*));
		args[0] = "sudo";
		args[1] = "--";
		memcpy(&args[2], argv, argc * sizeof(char*));
		args[argc + 2] = NULL;
		execv(sudo, args);
	}
	fprintf(stderr, "It requires to be run as root to work.\n");
	exit(-1);
}

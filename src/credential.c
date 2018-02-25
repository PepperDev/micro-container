#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#include "io.h"
#include "uid.h"

uid_t old_uid;

void check_credential(int argc, char* argv[]) {
	char *sudo;
	old_uid = getuid();
	if (old_uid == 0 || (setuid(0) == 0 && setgid(0) == 0)) {
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "launch.h"
#include "config.h"

void launch()
{
	// TODO: call initscript
	if (chroot(computed_rootdir))
	{
		fprintf(
			stderr,
			"Fatal: unable to change root to \"%s\"!\n",
			computed_rootdir
		);
		exit(1);
	}
	chdir("/");
	// TODO: compute user
	// TODO: change user
	// TODO: compute shell if no command
	// TODO: reset env? or compute env?
	char *argv[] = {"-bash", NULL};
	execve("/bin/bash", argv, NULL);
	fprintf(
		stderr,
		"Warning: unable to launch command!\n"
	);
}

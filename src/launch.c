#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>

#include "launch.h"
#include "config.h"

void launch()
{
	// TODO: call initscript
	chdir(computed_rootdir);
	// TODO: stat("..", &st) // st.st_dev major() minor()
	if (mount("/", NULL, NULL, MS_PRIVATE, NULL))
	{
		fprintf(
			stderr,
			"Warning: unable to make parent private!\n"
		);
	}
	if (mount(".", "/", NULL, MS_MOVE, NULL))
	{
		fprintf(
			stderr,
			"Warning: unable to move root!\n"
		);
	}
	if (chroot("."))
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

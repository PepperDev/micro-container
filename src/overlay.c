#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <string.h>

void overlay(const char *root, const char *lower, const char *upper,
	const char *work) {
	char *opt;
	size_t max;
	if (work == NULL) {
		// TODO: old kernel use overlayfs instead overlay
	} else {
		max = strlen(lower) + strlen(upper) + strlen(work) + 29;
		opt = malloc(max);
		// TODO: escape , to \,
		snprintf(opt, max, "lowerdir=%s,upperdir=%s,workdir=%s",
			lower, upper, work);
		// TODO: recursive mount overlay on /etc/* /usr/* /var/* /bin/* /sbin/* /lib/* /lib64/*
		if (mount("overlay", root, "overlay", 0, opt)) {
			fprintf(stderr,
				"Could not mount container on \"%s\" with opt \"%s\" run dmesg to see the problem!\n",
				root, opt);
			exit(-1);
		}
		free(opt);
	}
}

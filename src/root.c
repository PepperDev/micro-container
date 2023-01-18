#include "root.h"
#define _GNU_SOURCE             // required for chroot, vfork
#include <unistd.h>
#undef _GNU_SOURCE
#include <stdio.h>
#include <sys/mount.h>

int changeroot(char *root)
{
    if (chdir(root)) {
        fprintf(stderr, "Unable to change directory to %s.\n", root);
        return -1;
    }

    if (mount("/", NULL, NULL, MS_PRIVATE, NULL)) {
        fprintf(stderr, "Unable to make parent private.\n");
        // return -1;
    }

    if (mount(".", "/", NULL, MS_MOVE, NULL)) {
        fprintf(stderr, "Unable to move root.\n");
        return -1;
    }

    if (chroot(".")) {
        fprintf(stderr, "Unable to change root to %s.\n", root);
        return -1;
    }

    if (chdir("/")) {
        fprintf(stderr, "Unable to change directory to new root.\n");
        return -1;
    }

    return 0;
}

int changeroot_pid(pid_t pid)
{
    //nsenter pid, cgroup, mount, root
    //setns(fd, type)
    return -1;
}

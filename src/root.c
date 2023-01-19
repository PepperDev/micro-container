#include "root.h"
#define _GNU_SOURCE             // required for chroot, vfork, setns
#include <unistd.h>
#include <sched.h>
#undef _GNU_SOURCE
#include <stdio.h>
#include <sys/mount.h>
#include <fcntl.h>

static int nsenter(pid_t, char *, int);

int changeroot(char *root)
{
    if (chdir(root)) {
        fprintf(stderr, "Unable to change directory to %s.\n", root);
        return -1;
    }

    if (mount("/", NULL, NULL, MS_PRIVATE, NULL)) {
        fprintf(stderr, "Unable to make parent private.\n");
        return -1;
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
    if (nsenter(pid, "mnt", CLONE_NEWNS)) {
        return -1;
    }
    if (nsenter(pid, "pid", CLONE_NEWPID)) {
        return -1;
    }
    if (nsenter(pid, "cgroup", CLONE_NEWCGROUP)) {
        return -1;
    }
    // TODO: may need to fork to obtain new pid in the new ns
    return -1;
}

static int nsenter(pid_t pid, char *ns, int type)
{
    char buf[64];
    if (snprintf(buf, 64, "/proc/%d/ns/%s", pid, ns) == -1) {
        return -1;
    }
    int fd = open(buf, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Unable to open %s namespace of %d.\n", ns, pid);
        return -1;
    }
    if (setns(fd, type)) {
        fprintf(stderr, "Unable to set %s namespace of %d.\n", ns, pid);
        return -1;
    }
    if (close(fd)) {
        fprintf(stderr, "Unable to close %s namespace of %d.\n", ns, pid);
        return -1;
    }
    return 0;
}

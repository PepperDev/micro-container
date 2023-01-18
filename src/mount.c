#include "mount.h"
#define _GNU_SOURCE             // required for vfork, unshare
#include <unistd.h>
#include <sched.h>
#undef _GNU_SOURCE
#include <sys/mount.h>
#include <stdio.h>

static const char TYPE_TMPFS[] = "tmpfs";

static int mount_type(const char *, char *, unsigned long, void *);
static int mount_bind(char *, char *);

int prepare_mounts(mount_t * mounts)
{
    if (unshare(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWCGROUP)) {
        fprintf(stderr, "Unable to unshare mount and pid.\n");
        return -1;
    }

    // use clone with CLONE_VM | CLONE_VFORK
    pid_t pid = vfork();
    if (pid == -1) {
        fprintf(stderr, "Unable to fork process.\n");
        return -1;
    }
    if (pid != 0) {
        // write pidfile using this pid and exit
        mounts->pid = pid;
        return 0;
    }

    // TODO: check if it will be required multiple overlays
    if (mount_type(mounts->overlay_type, mounts->root, 0, mounts->overlay_opts)) {
        return -1;
    }

    if (mount_bind(mounts->dev, mounts->root_dev)) {
        return -1;
    }

    if (mount_bind(mounts->dev_pts, mounts->root_dev_pts)) {
        return -1;
    }

    if (mount_type("proc", mounts->root_proc, MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL)) {
        return -1;
    }

    if (mount_type("sysfs", mounts->root_sys, MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL)) {
        return -1;
    }

    if (mount_type(TYPE_TMPFS, mounts->root_tmp, MS_NOSUID | MS_NODEV | MS_NOATIME, NULL)) {
        return -1;
    }

    if (mount_type(TYPE_TMPFS, mounts->root_var_tmp, MS_NOSUID | MS_NODEV | MS_NOATIME, NULL)) {
        return -1;
    }

    if (mount_type(TYPE_TMPFS, mounts->root_run, MS_NOSUID | MS_NOEXEC, "mode=0755")) {
        return -1;
    }

    // TODO: mount_bind (conditionally) /sys/fs/cgroup /sys/firmware/efi/efivars

    //mkdir /run/lock /run/user
    //mount_bind /etc/resolv.conf (touch if not exists)
    //mount_type tmpfs /run/shm or /dev/shm/
    //maybe mount /run/user/$id/pulse and wayland-0
    //mount user volumes... mkdir if not exists...
    return 0;
}

static int mount_type(const char *type, char *target, unsigned long flags, void *data)
{
    if (mount(type, target, type, flags, data)) {
        fprintf(stderr, "Unable to mount %s at %s.\n", type, target);
        return -1;
    }

    if (mount(NULL, target, NULL, MS_PRIVATE, NULL)) {
        fprintf(stderr, "Unable to make %s at %s private.\n", type, target);
        return -1;
    }

    return 0;
}

static int mount_bind(char *host, char *target)
{
    if (mount(host, target, NULL, MS_BIND, NULL)) {
        fprintf(stderr, "Unable to mount bind %s.\n", host);
        return -1;
    }

    // maybe MS_UNBINDABLE instead?
    if (mount(NULL, target, NULL, MS_PRIVATE, NULL)) {
        fprintf(stderr, "Unable to make bind %s private.\n", host);
        return -1;
    }

    return 0;
}

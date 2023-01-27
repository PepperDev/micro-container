#include "mount.h"
#include "mem.h"
#include "io.h"
#include "proc.h"
#define _GNU_SOURCE             // required for unshare
#include <sched.h>
#undef _GNU_SOURCE
#include <sys/mount.h>
#include <stdio.h>
#include <string.h>

static char TYPE_TMPFS[] = "tmpfs";
static char TYPE_CGROUP[] = "cgroup";

static int mount_type(char *, char *, unsigned long, void *);
static int mount_bind(char *, char *, bool);
static int mount_user(char *, size_t, char *);
static int mount_cgroup(char *, size_t, char *);

int prepare_mounts(mount_t * mounts, pid_t * pid)
{
    if (unshare(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWCGROUP)) {
        fprintf(stderr, "Unable to unshare mount and pid.\n");
        return -1;
    }
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
        fprintf(stderr, "Unable to make parent private.\n");
        return -1;
    }

    *pid = pidfork();
    if (*pid == -1) {
        return -1;
    }
    if (*pid) {
        return 0;
    }

    // TODO: check if it will be required multiple overlays
    // skip /dev /proc /sys /run /tmp /var/tmp /boot
    if (mount_type(mounts->overlay_type, mounts->root, 0, mounts->overlay_opts)) {
        return -1;
    }

    if (mount_bind(mounts->dev, mounts->root_dev, false)) {
        return -1;
    }

    if (mount_bind(mounts->dev_pts, mounts->root_dev_pts, false)) {
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

    int ret = io_islink(mounts->root_resolv);
    if (ret == -1) {
        return -1;
    }
    if (ret) {
        ret = io_exists(mounts->root_resolv);
        if (ret == -1) {
            return -1;
        }
        ret = !ret;
    } else if (io_unlink(mounts->root_resolv)) {
        return -1;
    }
    if (!ret && io_touch(mounts->root_resolv)) {
        return -1;
    }

    if (mount_bind(mounts->resolv, mounts->root_resolv, false)) {
        return -1;
    }

    if (io_mktmpdir(mounts->root_run_lock, true)) {
        return -1;
    }

    if (io_mktmpdir(mounts->root_run_user, false)) {
        return -1;
    }

    if (mounts->ln_shm) {
        if (io_createlink(mounts->ln_shm, mounts->root_run_shm)) {
            return -1;
        }
        if (mount_type(TYPE_TMPFS, mounts->root_dev_shm, MS_NOSUID | MS_NODEV, NULL)) {
            return -1;
        }
    } else if (io_mktmpdir(mounts->root_run_shm, true)) {
        return -1;
    }

    size_t size = strlen(mounts->root); // TODO: imrprove it
    if (mounts->root_cgroup) {
        if (mount_type(TYPE_TMPFS, mounts->root_cgroup, MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=0755")) {
            return -1;
        }
        for (size_t i = 0; i < mounts->cgroups_count; i++) {
            if (mount_cgroup(mounts->root, size, mounts->cgroups[i])) {
                return -1;
            }
        }
        // /sys/fs/cgroup/unified cgroup2 nsdelegate
    }

    // user should use volumes to bind /sys/firmware/efi/efivars

    if (mounts->volumes_count) {
        for (size_t i = 0; i < mounts->volumes_count; i++) {
            if (mount_user(mounts->root, size, mounts->volumes[i])) {
                return -1;
            }
        }
    }
    return 0;
}

int mount_user_volume(char *root, size_t root_size, char *host, size_t host_size, char *target, size_t target_size)
{
    size_t guest_size = 0;
    char *guest = mem_path(root, root_size, target, target_size, &guest_size);
    if (!guest) {
        return -1;
    }
    int ret = io_exists(host);
    if (ret == -1) {
        return -1;
    }
    bool dir = true;
    if (ret) {
        if (io_mkdir(host, host_size)) {
            return -1;
        }
    } else {
        ret = io_isdir(host);
        if (ret == -1) {
            return -1;
        }
        dir = !ret;
    }
    // TODO: create guest copying ownership and mode of host (like a mirror)?
    size_t dir_size = guest_size;
    if (!dir) {
        int i = dir_size - 1;
        while (dir_size && guest[i] == '/') {
            i--;
        }
        while (dir_size && guest[i] != '/') {
            i--;
        }
        if (!i) {
            fprintf(stderr, "Unable to find parent dir of %s.\n", guest);
            return -1;
        }
        dir_size = i;
        guest[dir_size] = 0;
    }
    if (io_mkdir(guest, dir_size)) {
        return -1;
    }
    if (!dir) {
        guest[dir_size] = '/';
        if (io_touch(guest)) {
            return -1;
        }
    }
    if (mount_bind(host, guest, true)) {
        return -1;
    }
    free(guest);
    return 0;
}

static int mount_type(char *type, char *target, unsigned long flags, void *data)
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

static int mount_bind(char *host, char *target, bool rec)
{
    int flags = MS_BIND;
    if (rec) {
        flags |= MS_REC;
    }
    if (mount(host, target, NULL, flags, NULL)) {
        fprintf(stderr, "Unable to mount bind %s.\n", host);
        return -1;
    }

    flags = MS_PRIVATE;
    if (rec) {
        flags |= MS_REC;
    }
    if (mount(NULL, target, NULL, flags, NULL)) {
        fprintf(stderr, "Unable to make bind %s private.\n", host);
        return -1;
    }

    return 0;
}

static int mount_user(char *root, size_t root_size, char *volume)
{
    size_t size = strlen(volume);
    char *sep = memchr(volume, ':', size);
    if (sep) {
        *sep = 0;
        return mount_user_volume(root, root_size, volume, sep - volume, sep + 1, size - (sep + 1 - volume));
    }
    return mount_user_volume(root, root_size, volume, size, volume, size);
}

static int mount_cgroup(char *root, size_t root_size, char *volume)
{
    char *name = volume;
    if (!memcmp(name, "name=", 5)) {
        name += 5;
    }
    size_t size = strlen(name);
    char *target = mem_append(root, root_size, "/sys/fs/cgroup/", 15, name, size);
    if (!target) {
        return -1;
    }
    if (io_mkdir(target, root_size + size + 15)) {
        return -1;
    }
    if (mount_type(TYPE_CGROUP, target, MS_NOSUID | MS_NODEV | MS_NOEXEC, volume)) {
        return -1;
    }
    return 0;
}

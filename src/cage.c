#include "cage.h"
#include "proc.h"
#include "io.h"
#include "overlay.h"
#include "mount.h"
#include "root.h"
#include "launch.h"
#include <unistd.h>
#include <string.h>

static int spawn_existing(config_t *);

int spawn_cage(config_t * config)
{
    size_t name_size = 0;
    bool compute_name_size = true;

    if (!config->pidfile) {
        if (config->name) {
            name_size = strlen(config->name);
            compute_name_size = false;
        }
        config->pidfile = compute_pidfile(config->name, name_size);
        if (!config->pidfile) {
            return -1;
        }
    }

    if (!access(config->pidfile, F_OK)) {
        if (spawn_existing(config)) {
            return -1;
        }
        return 0;
    }

    int ret = io_isoverlay2supported();
    if (ret == -1) {
        return -1;
    }
    bool overlay2 = ret == 0;
    if (compute_name_size && (!config->upperdir || (overlay2 && !config->workdir))) {
        if (config->name) {
            name_size = strlen(config->name);
        }
    }

    char *opt = compute_overlay(config, name_size, overlay2);
    if (!opt) {
        return -1;
    }
    // TODO: warn if workdir and upperdir are in different filesystem but keep going...
    // TODO: if lowerdir is parent of upperdir truncate 10G, mke2fs, losetup and loop mount "${upperdir}/../.." if appdir is empty

    int fd = create_pidfile(config->pidfile);
    if (fd == -1) {
        return -1;
    }
    // compute_cage

    // compute user, shell, home...
    // reuse term or vt100
    // lang=C if not found
    // path=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

    mount_t mounts;

    pid_t pid = 0;
    if (prepare_mounts(&mounts, &pid)) {
        return -1;
    }

    if (pid) {
        if (writepid(fd, pid)) {
            return -1;
        }
        return 0;
    }
    // compute gui mounts

    // TODO: create currentdir if do not exists before launch after mount

    // check uid gid groups...

    if (changeroot("/tmp")) {
        return -1;
    }
    //execve "/bin/sh", {"-sh",NULL}, env...

    launch_t instance;
    if (launch(&instance)) {
        return -1;
    }
    return 0;
}

static int spawn_existing(config_t * config)
{
    int fd;
    pid_t pid = readpid(config->pidfile, &fd);
    if (pid == -1) {
        return -1;
    }
    // compute envs... term, lang, home, shell, user, path...
    // could consider env from previous instance (/proc/:id/environ)
    // to avoid problems when called by different user, but it could
    // change if exec env is called

    if (changeroot_pid(pid)) {
        return -1;
    }

    if (close_pid(fd)) {
        return -1;
    }
    // launch();
    return 0;
}

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
    size_t name_size = 0, pidfile_size;
    bool compute_name_size = true;

    if (config->pidfile) {
        pidfile_size = strlen(config->pidfile);
    } else {
        if (config->name) {
            name_size = strlen(config->name);
            compute_name_size = false;
        }
        config->pidfile = compute_pidfile(config->name, name_size, &pidfile_size);
        if (!config->pidfile) {
            return -1;
        }
    }

    int ret = io_exists(config->pidfile);
    if (ret == -1) {
        return -1;
    }
    if (!ret) {
        ret = spawn_existing(config);
        if (ret == -1) {
            return -1;
        }
        if (!ret) {
            return 0;
        }
    }
    // TODO: validate user defined envs, should contain '=', make key unique, consider last

    ret = io_isoverlay2supported();
    if (ret == -1) {
        return -1;
    }
    bool overlay2 = ret == 0;
    if (compute_name_size && (!config->upperdir || (overlay2 && !config->workdir))) {
        if (config->name) {
            name_size = strlen(config->name);
        }
    }

    size_t upper_size, work_size;
    char *opts = compute_overlay(config, name_size, overlay2, &upper_size, &work_size);
    if (!opts) {
        return -1;
    }
    // TODO: abort if lowerdir doesn't exist

    // TODO: warn if workdir and upperdir are in different filesystem but keep going...

    // TODO: if lowerdir is parent of upperdir truncate 10G, mke2fs, losetup and loop mount "${upperdir}/../.." if appdir is empty

    if (io_mkdir(config->upperdir, upper_size)) {
        return -1;
    }
    if (overlay2 && io_mkdir(config->workdir, work_size)) {
        return -1;
    }

    int fd = create_pidfile(config->pidfile, pidfile_size);
    if (fd == -1) {
        return -1;
    }
    // if gui compute host's XDG_RUNTIME_DIR

    // check if user defined envs replaces term, lang, path, home, shell and user
    // reuse host term or vt100
    // reuser host lang or lang=C if not set
    // path=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin unless user defined env

    mount_t mounts = {
        .overlay_type = overlay2 ? "overlay" : "overlayfs",
        .dev = "/dev",
        .dev_pts = "/dev/pts",
        .resolv = "/etc/resolv.conf",
        .root = "/tmp",
        .overlay_opts = opts,
        .root_dev = "/tmp/dev",
        .root_dev_pts = "/tmp/dev/pts",
        .root_proc = "/tmp/proc",
        .root_sys = "/tmp/sys",
        .root_tmp = "/tmp/tmp",
        .root_var_tmp = "/tmp/var/tmp",
        .root_run = "/tmp/run",
        .root_resolv = "/tmp/etc/resolv.conf"
            // TODO: shm
            // TODO: user defined mounts
    };

    pid_t pid = 0;
    if (prepare_mounts(&mounts, &pid)) {
        return -1;
    }

    if (pid) {
        if (writepid(fd, pid)) {
            return -1;
        }
        return pidwait(pid);
    }

    if (changeroot(mounts.root)) {
        return -1;
    }
    // TODO: check uid gid groups...
    // TODO: compute user, shell, home, unless user defined before
    // TODO: if no home found create /tmp/.home

    // TODO: compute gui mounts? - after mounts because guest XDG_RUNTIME_DIR should be based on guest user id

    // TODO: create currentdir if do not exists before launch after mount

    //execve "/bin/sh", {"-sh",NULL}, env...

    launch_t instance = {
        .path = "/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin",
        .init = NULL,
        .init_args = NULL,
        .init_envs = NULL,
        .uid = 0,
        .gid = 0,
        .groups_count = 0,
        .groups = NULL,
        .dir = NULL,
        .command = "/bin/sh",
        .args = (char *[]) {"-sh", NULL},
        .envs = (char *[]) {
                            "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin",
                            "TERM=vt100",
                            "LANG=C",
                            "HOME=/root",
                            "USER=root",
                            NULL}
    };
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
    int ret = pidexists(pid);
    if (ret == -1) {
        return -1;
    }
    if (ret) {
        if (io_unlink(config->pidfile)) {
            return -1;
        }
        if (close_pid(fd)) {
            return -1;
        }
        return 1;
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

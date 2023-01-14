#include "cage.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static bool try_reuse(config_t *);

static bool fill_defaults(config_t *);

void spawn_cage(config_t * config)
{
    if (try_reuse(config) || !fill_defaults(config)) {
        return;
    }
    // validate config?
    // if same device try truncate 10G mke2fs and losetup and loop mount

    //unshare(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWCGROUP) //mounts cgroup

    //should spawns new process for pid work
    //use vfork or clone with CLONE_VM | CLONE_VFORK and exit old process
    //setsid()/setpgrp() setpgid(0, 0)

    //mount_overlay ...recursively?
    //mount_bind /dev /dev/pts
    //mount_type /proc /sys /tmp /var/tmp /run
    //mount_bind (conditionally) /sys/fs/cgroup /sys/firmware/efi/efivars
    //mkdir /run/lock /run/user
    //mount_bind /etc/resolv.conf (touch if not exists)
    //mount_type /run/shm or /dev/shm/
    //maybe mount /run/user/$id
    //mount user volumes...

    // check initscript

    // compute user, shell, home...
    // reuse term
    // lang=C if not found
    // path=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

    // check uid gid groups...

    //chdir(root_dir)
    //mount --move . /
    //chroot(".");
    //chdir config.currentdir
    //execve "/bin/sh", {"-sh",NULL}, env...
}

static bool try_reuse(config_t * config)
{
    if (access(config->pidfile, F_OK)) {
        return false;
    }
    // TODO: export code in proc.c to read pid and lock fd.
    // should we fail and exit if file exists but fail to read
    // or should we consider a new instance?
    // if pid is read but process doesn't exists consider new instance!
    // overwrite or remove the file.

    // compute envs... term, lang, home, shell, user, path...
    // could consider env from previous instance (/proc/:id/environ)
    // to avoid problems when called by different user, but it could
    // change if exec env is called

    // nsenter(setns) target(pid) mount pid/proc cgroup root uid gid groups wd
    return false;
}

static bool fill_defaults(config_t * config)
{
    if (!config->upperdir || !config->workdir) {
        size_t appdir_size;
        if (!config->appdir) {
            if (config->name) {
                size_t size = strlen(config->name);
                appdir_size = 28 + size;
                config->appdir = malloc(appdir_size + 1);
                if (!config->appdir) {
                    // TODO: remove duplicated message
                    fprintf(stderr, "Unable to allocate memory\n");
                    return false;
                }
                memcpy(config->appdir, "/var/lib/microcontainer/app-", 28);
                memcpy(config->appdir + 28, config->name, size + 1);
            } else {
                config->appdir = "/var/lib/microcontainer/default";
                appdir_size = 31;
            }
        } else {
            appdir_size = strlen(config->appdir);
        }
        if (!config->upperdir) {
            config->upperdir = malloc(appdir_size + 7);
            if (!config->upperdir) {
                fprintf(stderr, "Unable to allocate memory\n");
                return false;
            }
            memcpy(config->upperdir, config->appdir, appdir_size);
            memcpy(config->upperdir + appdir_size, "/upper", 7);
        }
        if (!config->workdir) {
            config->workdir = malloc(appdir_size + 6);
            if (!config->workdir) {
                fprintf(stderr, "Unable to allocate memory\n");
                return false;
            }
            memcpy(config->workdir, config->appdir, appdir_size);
            memcpy(config->workdir + appdir_size, "/work", 6);
        }
    }
    if (!config->lowerdir) {
        config->lowerdir = "/";
    }
    // config->user and config->group leave empty
    if (!config->currentdir) {
        config->currentdir = "/";
    }
    // warn workdir != upperdir filesystem
    printf("upperdir: %s\nworkdir: %s\nlowerdir: %s\n", config->upperdir, config->workdir, config->lowerdir);
    return true;
}

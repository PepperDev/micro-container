#include "cage.h"
#include "proc.h"
#include "mem.h"
#include "mount.h"
#include "root.h"
#include "launch.h"
#include <unistd.h>
#include <string.h>

static int spawn_existing(config_t *);

int spawn_cage(config_t * config)
{
    size_t name_size = 0;
    //bool compute_name_size = true;

    if (!config->pidfile) {
        if (config->name) {
            name_size = strlen(config->name);
            //compute_name_size = false;
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
    // TODO: create currentdir if do not exists before launch after mount

    /*
       if (compute_cage(config, &cage)) {
       return -1;
       }
     */
    // TODO: if lowerdir is parent of upperdir truncate 10G, mke2fs, losetup and loop mount "${upperdir}/../.." if appdir is empty

    // TODO: open pid file and lock it, otherwise fail

    // compute user, shell, home...
    // reuse term
    // lang=C if not found
    // path=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

    // prepare_mounts();

    // check uid gid groups...

    if (changeroot("/tmp")) {
        return -1;
    }
    //execve "/bin/sh", {"-sh",NULL}, env...
    //launch();
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

/*
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
    // user, group and currentdir leave empty
    // TODO: warn if workdir and upperdir are in different filesystem but keep going...
    return true;
}
*/

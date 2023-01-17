#include "cage.h"
#include "proc.h"
#include "mem.h"
#include "io.h"
#include "mount.h"
#include "root.h"
#include "launch.h"
#include <unistd.h>
#include <string.h>

static int spawn_existing(config_t *);
static char *compute_overlay(config_t *, size_t, bool);
static void buffer_append_opt(buffer_t, char *, size_t);

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

    printf("opt = %s\n", opt);
    return 0;
    // TODO: warn if workdir and upperdir are in different filesystem but keep going...
    // TODO: if lowerdir is parent of upperdir truncate 10G, mke2fs, losetup and loop mount "${upperdir}/../.." if appdir is empty

    // compute_cage
    // TODO: open pid file and lock it, otherwise fail

    // compute user, shell, home...
    // reuse term or vt100
    // lang=C if not found
    // path=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

    // prepare_mounts();

    // TODO: create currentdir if do not exists before launch after mount

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

static char *compute_overlay(config_t * config, size_t name_size, bool overlay2)
{
    size_t lower_size = 0;
    if (config->lowerdir && *config->lowerdir) {
        lower_size = strlen(config->lowerdir);
    } else {
        config->lowerdir = "/";
        lower_size = 1;
    }
    size_t upper_size = 0, work_size = 0;
    if (!config->upperdir || (overlay2 && !config->workdir)) {
        size_t appdir_size;
        if (config->appdir) {
            appdir_size = strlen(config->appdir);
        } else {
            if (config->name) {
                appdir_size = name_size + 28;
                config->appdir = mem_append("/var/lib/microcontainer/app-", 28, config->name, name_size + 1, NULL, 0);
                if (!config->appdir) {
                    return NULL;
                }
            } else {
                config->appdir = "/var/lib/microcontainer/default";
                appdir_size = 31;
            }
        }
        if (config->upperdir) {
            upper_size = strlen(config->upperdir);
        } else {
            config->upperdir = mem_path(config->appdir, appdir_size, "upper", 5, &upper_size);
            if (!config->upperdir) {
                return NULL;
            }
        }
        if (overlay2) {
            if (config->workdir) {
                work_size = strlen(config->workdir);
            } else {
                config->workdir = mem_path(config->appdir, appdir_size, "work", 4, &work_size);
                if (!config->workdir) {
                    return NULL;
                }
            }
        }
    }

    buffer_t buf = buffer_new(lower_size + upper_size + work_size + 64);
    if (!buf) {
        return NULL;
    }
    buffer_write_data(buf, 9, "lowerdir=");
    buffer_append_opt(buf, config->lowerdir, lower_size);
    buffer_write_data(buf, 10, ",upperdir=");
    buffer_append_opt(buf, config->upperdir, upper_size);
    if (overlay2) {
        buffer_write_data(buf, 9, ",workdir=");
        buffer_append_opt(buf, config->workdir, work_size);
    }
    buffer_write_byte(buf, 0);
    return buffer_use(buf);
}

static void buffer_append_opt(buffer_t buf, char *data, size_t size)
{
    char *last = data;
    char *end = data + size;

    while (data < end) {
        if (*data == '\\' || *data == ',') {
            if (last < data) {
                buffer_write_data(buf, data - last, last);
            }
            buffer_write_byte(buf, '\\');
            buffer_write_byte(buf, *data);
            last = data + 1;
        }
        data++;
    }
    if (last < end) {
        buffer_write_data(buf, end - last, last);
    }
}

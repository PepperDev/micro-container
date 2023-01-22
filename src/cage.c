#include "cage.h"
#include "mem.h"
#include "proc.h"
#include "env.h"
#include "user.h"
#include "io.h"
#include "overlay.h"
#include "mount.h"
#include "root.h"
#include "launch.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define CAGE_ROOT "/tmp/.cageroot"
#define CAGE_ROOT_SIZE 14
#define FILE_PASSWD "/etc/passwd"
#define FILE_GROUP "/etc/group"

static int spawn_existing(config_t *, env_t *);
static int launch_cage(env_t *, user_t *);

// compute_cage
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

    env_t envs;
    if (parse_envs(&envs, config->envs, config->envs_count, config->gui)) {
        return -1;
    }

    int ret = io_exists(config->pidfile);
    if (ret == -1) {
        return -1;
    }
    if (!ret) {
        ret = spawn_existing(config, &envs);
        if (ret == -1) {
            return -1;
        }
        if (!ret) {
            return 0;
        }
    }

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

    size_t upper_size, work_size, lower_size;
    char *opts = compute_overlay(config, name_size, overlay2, &upper_size, &work_size, &lower_size);
    if (!opts) {
        return -1;
    }

    ret = io_exists(config->lowerdir);
    if (ret == -1) {
        return -1;
    }
    if (ret) {
        fprintf(stderr, "Lowerdir %s does not exists\n", config->lowerdir);
        return -1;
    }

    if (overlay_filesystem(config->upperdir, upper_size, config->lowerdir, lower_size)) {
        return -1;
    }

    if (io_mkdir(config->upperdir, upper_size)) {
        return -1;
    }

    if (overlay2) {
        if (io_mkdir(config->workdir, work_size)) {
            return -1;
        }
        ret = io_samefs(config->upperdir, config->workdir);
        if (ret == -1) {
            return -1;
        }
        if (ret) {
            fprintf(stderr, "Warning: workdir and upperdir ar in different filesystems.\n");
        }
    }

    char root[] = CAGE_ROOT;
    if (io_mkdir(root, CAGE_ROOT_SIZE)) {
        return -1;
    }

    mount_t mounts = {
        .overlay_type = overlay2 ? "overlay" : "overlayfs",
        .dev = "/dev",
        .dev_pts = "/dev/pts",
        .resolv = "/etc/resolv.conf",
        .root = root,
        .overlay_opts = opts,
        .root_dev = CAGE_ROOT "/dev",
        .root_dev_pts = CAGE_ROOT "/dev/pts",
        .root_proc = CAGE_ROOT "/proc",
        .root_sys = CAGE_ROOT "/sys",
        .root_tmp = CAGE_ROOT "/tmp",
        .root_var_tmp = CAGE_ROOT "/var/tmp",
        .root_run = CAGE_ROOT "/run",
        .root_resolv = CAGE_ROOT "/etc/resolv.conf"
            // TODO: shm
            // TODO: user defined mounts
    };

    int fd = create_pidfile(config->pidfile, pidfile_size);
    if (fd == -1) {
        return -1;
    }

    pid_t pid = 0;
    if (prepare_mounts(&mounts, &pid)) {
        return -1;
    }

    if (pid) {
        if (writepid(fd, pid)) {
            return -1;
        }
        int status = 0;
        if (pidwait(pid, &status)) {
            return -1;
        }
        if (io_unlink(config->pidfile)) {
            return -1;
        }
        return status;
    } else if (close_pid(fd)) {
        return -1;
    }

    user_t user;
    if (parse_user
        (&user, CAGE_ROOT FILE_PASSWD, CAGE_ROOT FILE_GROUP, config->user, config->group, envs.user, envs.home,
         envs.shell, config->initscript)) {
        return -1;
    }
    // TODO: compute gui mounts? - after mounts because guest XDG_RUNTIME_DIR should be based on guest user id, but before chroot to be possible to bind

    if (changeroot(mounts.root)) {
        return -1;
    }

    return launch_cage(&envs, &user);
}

static int spawn_existing(config_t * config, env_t * envs)
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

    ret = changeroot_pid(pid);
    if (ret == -1) {
        return -1;
    }
    if (ret) {
        return 0;
    }

    if (close_pid(fd)) {
        return -1;
    }

    config->initscript = NULL;

    user_t user;
    if (parse_user
        (&user, FILE_PASSWD, FILE_GROUP, config->user, config->group, envs->user, envs->home, envs->shell, false)) {
        return -1;
    }

    return launch_cage(envs, &user);
}

static int launch_cage(env_t * envs, user_t * users)
{
    char *home = envs->home;
    char *user = envs->user;
    char *shell = envs->shell;

    if (!home) {
        if (users->home) {
            home = mem_append("HOME=", 5, users->home, users->home_size + 1, NULL, 0);
            if (!home) {
                return -1;
            }
        } else {
            home = "HOME=/tmp/.home";
            // TODO: create /tmp/.home with uid as owner and gid as group
        }
    }
    if (!shell) {
        if (users->shell) {
            shell = mem_append("SHELL=", 6, users->shell, users->shell_size + 1, NULL, 0);
            if (!shell) {
                return -1;
            }
        } else {
            shell = "SHELL=/bin/sh";
        }
    }
    if (!user) {
        if (users->name) {
            user = mem_append("USER=", 5, users->name, users->name_size + 1, NULL, 0);
            if (!user) {
                return -1;
            }
        }
    }
    // TODO: create currentdir if do not exists before launch after mount, user as owner!!!!

    size_t i = 0;
    char *user_envs[envs->envs_count + 7];
    user_envs[i++] = envs->path;
    user_envs[i++] = envs->term;
    user_envs[i++] = envs->lang;
    user_envs[i++] = home;
    user_envs[i++] = shell;
    if (user) {
        user_envs[i++] = user;
    }
    if (envs->envs_count) {
        memcpy(user_envs + i, envs->envs, envs->envs_count * sizeof(char *));
        i += envs->envs_count;
    }
    user_envs[i] = NULL;

    launch_t instance = {
        .path = envs->path + 5,
        .init = NULL,
        .init_args = NULL,
        .init_envs = NULL,
        .uid = users->uid,
        .gid = users->gid,
        .groups_count = users->groups_count,
        .groups = users->groups,
        .dir = NULL,
        .command = "/bin/sh",
        .args = (char *[]) {"-sh", NULL},
        .envs = user_envs,
    };
    if (launch(&instance)) {
        return -1;
    }
    return 0;
}

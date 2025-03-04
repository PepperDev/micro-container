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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CAGE_ROOT "/tmp/.cageroot"
#define CAGE_ROOT_SIZE 14
#define FILE_PASSWD "/etc/passwd"
#define FILE_GROUP "/etc/group"
#define DIR_CGROUP "/sys/fs/cgroup"

static int spawn_existing(config_t *, env_t *);
static int launch_cage(env_t *, user_t *, char *, char *, char **, size_t);

// compute_cage
int spawn_cage(config_t *config)
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
        .root_resolv = CAGE_ROOT "/etc/resolv.conf",
        .root_run_lock = CAGE_ROOT "/run/lock",
        .root_run_user = CAGE_ROOT "/run/user",
        .root_run_shm = CAGE_ROOT "/run/shm",
        .root_dev_shm = CAGE_ROOT "/dev/shm",
        .ln_shm = NULL,
        .root_cgroup = NULL,
        .cgroups = NULL,
        .cgroups_count = 0,
        .volumes = config->volumes,
        .volumes_count = config->volumes_count,
    };

    char dev_shm[] = "/dev/shm";
    ret = io_islink(dev_shm);
    if (ret == -1) {
        return -1;
    }
    if (ret) {
        mounts.ln_shm = dev_shm;
    }

    ret = io_exists("/proc/cgroups");
    if (ret == -1) {
        return -1;
    }
    if (!ret) {
        ret = io_exists(DIR_CGROUP);
        if (ret == -1) {
            return -1;
        }
        if (!ret) {
            mounts.root_cgroup = CAGE_ROOT DIR_CGROUP;
            size_t size = 0;
            char *data = io_readfile("/proc/self/cgroup", &size);
            if (!data) {
                return -1;
            }
            char *pos = data;
            size_t left = size;
            while (left) {
                char *next = pos;
                while (*next != ':' && *next != '\n' && next - pos < left) {
                    next++;
                }
                if (*next == ':') {
                    next++;
                    char *end = next;
                    while (*end != ':' && *end != '\n' && end - pos < left) {
                        end++;
                    }
                    if (end - next) {
                        mounts.cgroups = mem_reallocate(mounts.cgroups, sizeof(char *) * (mounts.cgroups_count + 1));
                        if (!mounts.cgroups) {
                            return -1;
                        }
                        char zero = 0;
                        mounts.cgroups[mounts.cgroups_count] = mem_append(next, end - next, &zero, 1, NULL, 0);
                        if (!mounts.cgroups[mounts.cgroups_count]) {
                            return -1;
                        }
                        mounts.cgroups_count++;
                        next = end;
                    }
                }
                while (*next != '\n' && next - pos < left) {
                    next++;
                }
                while (*next == '\n' && next - pos < left) {
                    next++;
                }
                left -= next - pos;
                pos = next;
            }
            free(data);
        }
    }

    if (config->gui) {
        ret = io_exists("/tmp/.X11-unix");
        if (ret == -1) {
            return -1;
        }
        if (!ret) {
            mounts.volumes = mem_reallocate(mounts.volumes, sizeof(char *) * (mounts.volumes_count + 1));
            if (!mounts.volumes) {
                return -1;
            }
            char *copy = mem_append("/tmp/.X11-unix", 15, NULL, 0, NULL, 0);
            if (!copy) {
                return -1;
            }
            mounts.volumes[mounts.volumes_count++] = copy;
            envs.envs = mem_reallocate(envs.envs, sizeof(char *) * (envs.envs_count + 1));
            if (!envs.envs) {
                return -1;
            }
            envs.envs[envs.envs_count++] = "DISPLAY=:0.0";
            // TODO: do not replace if already exists
        }
    }

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
        (&user, CAGE_ROOT FILE_PASSWD, CAGE_ROOT FILE_GROUP, config->user, config->group, !envs.user, !envs.home,
         !envs.shell, config->initscript)) {
        return -1;
    }

    if (config->gui) {
        size_t host_size = strlen(envs.host_xdg_runtime_dir);   // TODO: improve it
        size_t pulse_size;
        char *pulse = mem_path(envs.host_xdg_runtime_dir, host_size, "pulse", 5, &pulse_size);
        if (!pulse) {
            return -1;
        }
        size_t wayland_size;
        char *wayland = mem_path(envs.host_xdg_runtime_dir, host_size, "wayland-0", 9, &wayland_size);  // TODO: do not repeat wayland-0
        if (!wayland) {
            return -1;
        }
        ret = io_exists(pulse);
        if (ret == -1) {
            return -1;
        }
        bool have_pulse = !ret;
        ret = io_exists(wayland);
        if (ret == -1) {
            return -1;
        }
        bool have_wayland = !ret;
        size_t size;
        char *user_xdg;
        size_t root_size = strlen(mounts.root); // TODO: improve it
        if (have_pulse || have_wayland) {
            char buf[64];
            if (snprintf(buf, 64, "%d", user.uid) == -1) {
                fprintf(stderr, "Unable to parse user id.\n");
                return -1;
            }
            user_xdg = mem_path("XDG_RUNTIME_DIR=/run/user", 25, buf, strlen(buf), &size);
            if (!user_xdg) {
                return -1;
            }
            envs.envs = mem_reallocate(envs.envs, sizeof(char *) * (envs.envs_count + 1));
            if (!envs.envs) {
                return -1;
            }
            envs.envs[envs.envs_count++] = user_xdg;
            size_t copy_size;
            char *copy = mem_path(mounts.root, root_size, user_xdg + 16, size - 16, &copy_size);
            if (io_mkdir(copy, copy_size)) {    // TODO: create it with mode 0700 instead of 755
                return -1;
            }
            if (io_chown(copy, user.uid, user.gid)) {
                return -1;
            }
            free(copy);
        }

        if (have_wayland) {
            // TODO: preserve XDG_CURRENT_DESKTOP unless overwritten
            envs.envs = mem_reallocate(envs.envs, sizeof(char *) * (envs.envs_count + 8));
            if (!envs.envs) {
                return -1;
            }
            envs.envs[envs.envs_count++] = "WAYLAND_DISPLAY=wayland-0";
            envs.envs[envs.envs_count++] = "XDG_SESSION_TYPE=wayland";
            envs.envs[envs.envs_count++] = "GDK_BACKEND=wayland";
            envs.envs[envs.envs_count++] = "QT_QPA_PLATFORM=wayland-egl";
            envs.envs[envs.envs_count++] = "SDL_VIDEODRIVER=wayland";
            envs.envs[envs.envs_count++] = "CLUTTER_BACKEND=wayland";
            envs.envs[envs.envs_count++] = "MOZ_ENABLE_WAYLAND=1";
            envs.envs[envs.envs_count++] = "_JAVA_AWT_WM_NONREPARENTING=1";
            // TODO: keep previous value if already set
            size_t copy_size;
            char *copy = mem_path(user_xdg + 16, size - 16, "wayland-0", 9, &copy_size);
            if (!copy) {
                return -1;
            }
            if (mount_user_volume(mounts.root, root_size, wayland, wayland_size, copy, copy_size)) {
                return -1;
            }
            free(copy);
        }
        if (have_pulse) {
            size_t copy_size;
            char *copy = mem_path(user_xdg + 16, size - 16, "pulse", 5, &copy_size);
            if (!copy) {
                return -1;
            }
            if (mount_user_volume(mounts.root, root_size, pulse, pulse_size, copy, copy_size)) {
                return -1;
            }
            free(copy);
            copy = mem_path(envs.host_home, strlen(envs.host_home), ".config/pulse/cookie", 20, &copy_size);
            ret = io_exists(copy);
            if (ret == -1) {
                return -1;
            }
            if (!ret) {
                size_t target_size;
                char *target = mem_path(mounts.root, root_size, user.home, strlen(user.home), &target_size);
                if (!target) {
                    return -1;
                }
                char *prev = target;
                target = mem_path(target, target_size, ".config/pulse/cookie", 20, &target_size);
                if (!target) {
                    return -1;
                }
                free(prev);
                target[target_size - 7] = 0;
                ret = io_exists(target);
                if (ret == -1) {
                    return -1;
                }
                if (ret) {
                    if (io_mkdir(target, target_size - 7)) {
                        return -1;
                    }
                    if (io_chown(target, user.uid, user.gid)) {
                        return -1;
                    }
                    target[target_size - 13] = 0;
                    if (io_chown(target, user.uid, user.gid)) {
                        return -1;
                    }
                    target[target_size - 21] = 0;
                    if (io_chown(target, user.uid, user.gid)) {
                        return -1;
                    }
                    target[target_size - 21] = '/';
                    target[target_size - 13] = '/';
                }
                target[target_size - 7] = '/';
                if (mount_user_volume
                    (mounts.root, root_size, copy, copy_size, target + root_size, target_size - root_size)) {
                    return -1;
                }
                free(target);
            }
            free(copy);
        }
        size_t copy_size;
        char *copy = mem_path(envs.host_home, strlen(envs.host_home), ".Xauthority", 11, &copy_size);
        ret = io_exists(copy);
        if (ret == -1) {
            return -1;
        }
        if (!ret) {
            size_t target_size;
            char *target = mem_path(user.home, strlen(user.home), ".Xauthority", 11, &target_size);
            if (mount_user_volume(mounts.root, root_size, copy, copy_size, target, target_size)) {
                return -1;
            }
            free(target);
        }
        free(copy);
    }

    if (changeroot(mounts.root)) {
        return -1;
    }

    return launch_cage(&envs, &user, config->currentdir, config->initscript, config->command, config->command_count);
}

static int spawn_existing(config_t *config, env_t *envs)
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

    user_t user;
    if (parse_user
        (&user, FILE_PASSWD, FILE_GROUP, config->user, config->group, !envs->user, !envs->home, !envs->shell, false)) {
        return -1;
    }

    return launch_cage(envs, &user, config->currentdir, NULL, config->command, config->command_count);
}

static int launch_cage(env_t *envs, user_t *users, char *dir, char *init, char **args, size_t args_count)
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
            char homedir[] = "/tmp/.home";
            int ret = io_exists(homedir);
            if (ret == -1) {
                return -1;
            }
            if (ret && (io_mkdir(homedir, sizeof(homedir)) || io_chown(homedir, users->uid, users->gid))) {
                return -1;
            }
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

    if (dir) {
        int ret = io_exists(dir);
        if (ret == -1) {
            return -1;
        }
        if (ret && (io_mkdir(dir, strlen(dir)) || io_chown(dir, users->uid, users->gid))) {
            return -1;
        }
    }

    size_t i = 0;
    char *user_envs[envs->envs_count + 7];
    user_envs[i++] = envs->path;
    user_envs[i++] = envs->term;
    user_envs[i++] = envs->lang;
    user_envs[i++] = home;
    if (user) {
        user_envs[i++] = user;
    }
    user_envs[i++] = shell;
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
        .dir = dir,
        .envs = user_envs,
    };

    if (init) {
        instance.init_envs = mem_allocate(sizeof(char *) * (envs->envs_count + 7));
        if (!instance.init_envs) {
            return -1;
        }
        char *root_home = envs->home;
        char *root_user = envs->user;
        char *root_shell = envs->shell;
        if (!root_home) {
            if (users->root_home) {
                root_home = mem_append("HOME=", 5, users->root_home, users->root_home_size + 1, NULL, 0);
                if (!root_home) {
                    return -1;
                }
            } else {
                root_home = "HOME=/root";
            }
        }
        if (!root_shell) {
            if (users->root_shell) {
                root_shell = mem_append("SHELL=", 6, users->root_shell, users->root_shell_size + 1, NULL, 0);
                if (!root_shell) {
                    return -1;
                }
            } else {
                root_shell = "SHELL=/bin/sh";
            }
        }
        if (!root_user) {
            if (users->root_name) {
                root_user = mem_append("USER=", 5, users->root_name, users->root_name_size + 1, NULL, 0);
                if (!root_user) {
                    return -1;
                }
            } else {
                root_user = "USER=root";
            }
        }
        i = 0;
        instance.init_envs[i++] = envs->path;
        instance.init_envs[i++] = envs->term;
        instance.init_envs[i++] = envs->lang;
        instance.init_envs[i++] = root_home;
        instance.init_envs[i++] = root_user;
        instance.init_envs[i++] = root_shell;
        if (envs->envs_count) {
            memcpy(instance.init_envs + i, envs->envs, envs->envs_count * sizeof(char *));
            i += envs->envs_count;
        }
        instance.init_envs[i] = NULL;

        instance.init_args = mem_allocate(sizeof(char *) * 3);
        if (!instance.init_args) {
            return -1;
        }
        instance.init_args[0] = root_shell + 6;
        instance.init_args[1] = init;
        instance.init_args[2] = NULL;
        instance.init = instance.init_args[0];
        // TODO: maybe re-read passwd and groups after running initscript, since it can setup users...
    }

    if (args && args_count) {
        instance.args = mem_allocate(sizeof(char *) * (args_count + 1));
        if (!instance.args) {
            return -1;
        }
        memcpy(instance.args, args, args_count * sizeof(char *));
        instance.args[args_count] = NULL;
        instance.command = args[0];
    } else {
        instance.args = mem_allocate(sizeof(char *) * 2);
        if (!instance.args) {
            return -1;
        }
        instance.command = shell + 6;
        size_t shell_size = strlen(instance.command);
        size_t i = shell_size;
        while (i && instance.command[i - 1] != '/') {
            i--;
        }
        char *cmd = mem_allocate(shell_size - i + 2);
        if (!cmd) {
            return -1;
        }
        cmd[0] = '-';
        memcpy(cmd + 1, instance.command + i, shell_size - i + 1);
        instance.args[0] = cmd;
        instance.args[1] = NULL;
    }

    if (launch(&instance)) {
        return -1;
    }
    return 0;
}

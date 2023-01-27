#ifndef MOUNT_H
#define MOUNT_H

#include <sys/types.h>

typedef struct {
    char *overlay_type;
    char *dev;
    char *dev_pts;
    char *resolv;
    char *root;
    char *overlay_opts;
    char *root_dev;
    char *root_dev_pts;
    char *root_proc;
    char *root_sys;
    char *root_tmp;
    char *root_var_tmp;
    char *root_run;
    char *root_resolv;
    char *root_run_lock;
    char *root_run_user;
    char *root_run_shm;
    char *root_dev_shm;
    char *ln_shm;
    char *root_cgroup;
    char **cgroups;
    size_t cgroups_count;
    char **volumes;
    size_t volumes_count;
} mount_t;

int prepare_mounts(mount_t *, pid_t *);

int mount_user_volume(char *, size_t, char *, size_t, char *, size_t);

#endif

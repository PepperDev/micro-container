#ifndef MOUNT_H
#define MOUNT_H

#include <sys/types.h>

typedef struct {
    char *overlay_type;
    char *dev;
    char *dev_pts;
    char *root;
    char *overlay_opts;
    char *root_dev;
    char *root_dev_pts;
    char *root_proc;
    char *root_sys;
    char *root_tmp;
    char *root_var_tmp;
    char *root_run;
    // resolv
    // shm
    // user_volumes...
    pid_t pid;
} mount_t;

int prepare_mounts(mount_t *);

#endif

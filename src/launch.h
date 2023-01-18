#ifndef LAUNCH_H
#define LAUNCH_H

#include <sys/types.h>

typedef struct {
    char *path;
    char *init;
    char **init_args;
    char **init_envs;
    uid_t uid;
    gid_t gid;
    size_t groups_count;
    gid_t *groups;
    char *dir;
    char *command;
    char **args;
    char **envs;
} launch_t;

int launch(launch_t *);

#endif

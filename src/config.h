#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <sys/types.h>

typedef struct {
    char *name;
    char *appdir;
    char *upperdir;
    char *lowerdir;
    char *workdir;
    char *pidfile;
    char *user;
    char *group;
    char *currentdir;
    char *initscript;
    char **envs;
    size_t envs_count;
    char **volumes;
    size_t volumes_count;
    char **command;
    size_t command_count;
    bool stop;
    bool gui;
} config_t;

int config_parse(config_t *, int, char *[]);

#endif

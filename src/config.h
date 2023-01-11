#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

#define VERSION "0.3.0-rc"

typedef struct {
    char *name;
    char *appdir;
    char *upperdir;
    char *lowerdir;
    char *workdir;
    char *pidfile;
    char *user;
    char *uid;
    char *gid;
    char *groups;
    char *startscript;
    char **envs;
    size_t envs_count;
    char **volumes;
    size_t volumes_count;
    char **command;
    size_t command_count;
    bool stop;
    bool gui;
} config_t;

bool config_parse(config_t *, int, char *[]);

#endif

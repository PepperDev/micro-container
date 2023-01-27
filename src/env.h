#ifndef ENV_H
#define ENV_H

#include <stdbool.h>
#include <sys/types.h>

typedef struct {
    char *path;
    char *term;
    char *lang;
    char *home;
    char *shell;
    char *user;
    char *host_xdg_runtime_dir;
    char *host_home;
    char **envs;
    size_t envs_count;
} env_t;

int parse_envs(env_t *, char **, size_t, bool);

#endif

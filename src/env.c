#include "env.h"
#include "mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ENV_TERM "TERM"
#define ENV_TERM_SIZE 4
#define ENV_LANG "LANG"
#define ENV_LANG_SIZE 4

static char *env_default(char *, size_t, char *);

int parse_envs(env_t * env, char **envs, size_t envs_count, bool gui)
{
    memset(env, 0, sizeof(env_t));
    env->envs = envs;
    // TODO: validate user defined envs, should contain '=', make key unique, consider last
    // TODO: store sorted keys to make it unique, use bsearch
    for (size_t i = 0; i < envs_count; i++) {
        char *pos = strchr(envs[i], '=');
        if (!pos) {
            fprintf(stderr, "Invalid input environment %s\n", envs[i]);
            return -1;
        }
        // if (memcmp())
        // if not match copy and increment env->envs_count
        // replace existing if not unique...
    }
    // check if user defined envs replaces term, lang, path, home, shell and user
    if (!env->path) {
        env->path = "/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin";
    }
    if (!env->term) {
        env->term = env_default(ENV_TERM, ENV_TERM_SIZE + 1, ENV_TERM "=vt100");
    }
    if (!env->lang) {
        env->lang = env_default(ENV_LANG, ENV_LANG_SIZE + 1, ENV_LANG "=C");
    }
    if (gui && !env->xdg_runtime_dir) {
        char *val = getenv("XDG_RUNTIME_DIR");
        if (val) {
            env->xdg_runtime_dir = val - 16;
        } else {
            // TODO: obtain caller from real uid or SUDO_USER or parent process
        }
    }
    return 0;
}

static char *env_default(char *key, size_t size, char *def)
{
    char *val = getenv(key);
    if (val) {
        return val - size;
    }
    return def;
}

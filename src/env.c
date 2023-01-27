#include "env.h"
#include "mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ENV_PATH "PATH"
#define ENV_PATH_SIZE 4
#define ENV_TERM "TERM"
#define ENV_TERM_SIZE 4
#define ENV_LANG "LANG"
#define ENV_LANG_SIZE 4
#define ENV_HOME "HOME"
#define ENV_HOME_SIZE 4
#define ENV_SHELL "SHELL"
#define ENV_SHELL_SIZE 5
#define ENV_USER "USER"
#define ENV_USER_SIZE 4
#define ENV_XDG "XDG_RUNTIME_DIR"
#define ENV_XDG_SIZE 15

static bool read_env(char *, char *, size_t, char **);
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
        if (read_env(envs[i], ENV_PATH, ENV_PATH_SIZE, &env->path)) {
            continue;
        }
        if (read_env(envs[i], ENV_TERM, ENV_TERM_SIZE, &env->term)) {
            continue;
        }
        if (read_env(envs[i], ENV_LANG, ENV_LANG_SIZE, &env->lang)) {
            continue;
        }
        if (read_env(envs[i], ENV_HOME, ENV_HOME_SIZE, &env->home)) {
            continue;
        }
        if (read_env(envs[i], ENV_SHELL, ENV_SHELL_SIZE, &env->shell)) {
            continue;
        }
        if (read_env(envs[i], ENV_USER, ENV_USER_SIZE, &env->user)) {
            continue;
        }
        // TODO: replace existing if not unique...
        env->envs[env->envs_count++] = envs[i];
    }
    if (!env->path) {
        env->path = "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin";
    }
    if (!env->term) {
        env->term = env_default(ENV_TERM, ENV_TERM_SIZE + 1, ENV_TERM "=vt100");
    }
    if (!env->lang) {
        env->lang = env_default(ENV_LANG, ENV_LANG_SIZE + 1, ENV_LANG "=C");
    }
    if (gui) {
        char *val = getenv(ENV_XDG);
        if (val) {
            env->host_xdg_runtime_dir = val;
        } else {
            // TODO: obtain caller from real uid or SUDO_UID or parent process
        }
        val = getenv("HOME");   // TODO: do not trust this value, do some mumbo jumbo
        if (val) {
            env->host_home = val;
        } else {
            // TODO: ...
        }
    }
    return 0;
}

static bool read_env(char *env, char *key, size_t size, char **value)
{
    if (memcmp(env, key, size) || env[size] != '=') {
        return false;
    }
    *value = env;
    return true;
}

static char *env_default(char *key, size_t size, char *def)
{
    char *val = getenv(key);
    if (val) {
        return val - size;
    }
    return def;
}

#include "config.h"
#include <string.h>             // memset strlen memcpy
#include <stdlib.h>             // malloc realloc

#define VERSION "0.4.0-rc"

static const char MEM_ERROR[] = "Unable to allocate memory.\n";

static const char *error_message(const char *, size_t, const char *, size_t, const char *);

static bool parse_arg_value(const char **, char *, char, char **, char *, int *);

static bool parse_arg_list(const char **, char *, char, char ***, size_t *, char *, int *);

static bool parse_arg_flag(char *, char, bool *);

const char *config_parse(config_t *config, int argc, char *argv[])
{
    memset(config, 0, sizeof(config_t));
    for (int i = 1; i < argc; i++) {
        char *carg = argv[i];
        if (carg[0] != '-') {
            config->command = argv + i;
            config->command_count = argc - i;
            break;
        }

        if (carg[1] == 'V' && carg[2] == 0) {
            return "cage version " VERSION ".\n";
        }
        if (carg[1] == 'h' && carg[2] == 0) {
            static const char prefix[] = "Usage: ";
            static const char suffix[] = " [options...] [--] [command...]\n";
            return error_message(prefix, sizeof(prefix) - 1, suffix, sizeof(suffix), argv[0]);
        }

        char *next = argv[i + 1];
        const char *error = NULL;
        bool ret;
        if ((ret = parse_arg_value(&error, carg, 'n', &config->name, next, &i))
            || (ret = parse_arg_value(&error, carg, 'a', &config->appdir, next, &i))
            || (ret = parse_arg_value(&error, carg, 'l', &config->lowerdir, next, &i))
            || (ret = parse_arg_value(&error, carg, 'w', &config->workdir, next, &i))
            || (ret = parse_arg_value(&error, carg, 'U', &config->upperdir, next, &i))
            || (ret = parse_arg_value(&error, carg, 'p', &config->pidfile, next, &i))
            || (ret = parse_arg_value(&error, carg, 'u', &config->user, next, &i))
            || (ret = parse_arg_value(&error, carg, 'g', &config->group, next, &i))
            || (ret = parse_arg_value(&error, carg, 'c', &config->currentdir, next, &i))
            || (ret = parse_arg_value(&error, carg, 'i', &config->initscript, next, &i))
            || (ret = parse_arg_list(&error, carg, 'e', &config->envs, &config->envs_count, next, &i))
            || (ret = parse_arg_list(&error, carg, 'v', &config->volumes, &config->volumes_count, next, &i))
            || (ret = parse_arg_flag(carg, 'k', &config->stop))
            || (ret = parse_arg_flag(carg, 'G', &config->gui))
            || (ret = parse_arg_flag(carg, 'C', &config->cgroup))
            || (ret = parse_arg_flag(carg, 'N', &config->network))
            ) {
            if (error) {
                return error;
            }
            continue;
        }
        if (carg[1] == '-' && carg[2] == 0) {
            i++;
            if (i < argc) {
                config->command = argv + i;
                config->command_count = argc - i;
            }
            break;
        }
        static const char prefix[] = "Unkown argument ";
        static const char suffix[] = ".\n";
        return error_message(prefix, sizeof(prefix) - 1, suffix, sizeof(suffix), carg);
    }

    return NULL;
}

static const char *error_message(const char *prefix, size_t prefixlen, const char *suffix, size_t suffixlen,
                                 const char *value)
{
    size_t valuelen = strlen(value);
    char *error = malloc(prefixlen + suffixlen + valuelen);
    if (!error) {
        return MEM_ERROR;
    }
    memcpy(error, prefix, prefixlen);
    char *pos = error + prefixlen;
    memcpy(pos, value, valuelen);
    pos += valuelen;
    memcpy(pos, suffix, suffixlen);
    return error;
}

static bool parse_arg_value(const char **error, char *arg, char value, char **target, char *next, int *i)
{
    if (arg[1] != value) {
        return false;
    }
    if (arg[2] == 0) {
        if (next == NULL) {
            static char template[] = "Required argument not provided for option -x.\n";
            template[sizeof(template) - 4] = value;
            *error = template;
            return true;
        }
        *target = next;
        (*i)++;
    } else if (arg[2] == '=') {
        *target = arg + 3;
    } else {
        *target = arg + 2;
    }
    return true;
}

static bool parse_arg_list(const char **error, char *arg, char value, char ***target, size_t *count, char *next, int *i)
{
    char *change = NULL;
    if (!parse_arg_value(error, arg, value, &change, next, i)) {
        return false;
    } else if (error) {
        return true;
    }
    *target = realloc(*target, (*count + 1) * sizeof(char *));
    if (*target == NULL) {
        *error = MEM_ERROR;
        return true;
    }
    (*target)[*count] = change;
    (*count)++;
    return true;
}

static bool parse_arg_flag(char *arg, char value, bool *target)
{
    if (arg[1] == value && arg[2] == 0) {
        *target = true;
        return true;
    }
    return false;
}

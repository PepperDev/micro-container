#include "config.h"
#include "mem.h"
#include <stdio.h>
#include <string.h>

#define VERSION "0.3.0-rc"

static int parse_arg_print(char *, char, void (*)(char *), char *);

static int parse_arg_value(char *, char, char **, char *, int *);

static int parse_arg_list(char *, char, char ***, size_t *, char *, int *);

static int parse_arg_flag(char *, char, bool *);

static void print_version(char *);

static void print_help(char *);

int config_parse(config_t * config, int argc, char *argv[])
{
    memset(config, 0, sizeof(config_t));
    for (int i = 1; i < argc; i++) {
        char *carg = argv[i];
        if (carg[0] != '-') {
            config->command = argv + i;
            config->command_count = argc - i;
            break;
        }

        if (parse_arg_print(carg, 'V', print_version, NULL) || parse_arg_print(carg, 'h', print_help, argv[0])) {
            return -1;
        }

        char *next = argv[i + 1];
        int ret;
        if ((ret = parse_arg_value(carg, 'n', &config->name, next, &i))
            || (ret = parse_arg_value(carg, 'a', &config->appdir, next, &i))
            || (ret = parse_arg_value(carg, 'l', &config->lowerdir, next, &i))
            || (ret = parse_arg_value(carg, 'w', &config->workdir, next, &i))
            || (ret = parse_arg_value(carg, 'U', &config->upperdir, next, &i))
            || (ret = parse_arg_value(carg, 'p', &config->pidfile, next, &i))
            || (ret = parse_arg_value(carg, 'u', &config->user, next, &i))
            || (ret = parse_arg_value(carg, 'G', &config->group, next, &i))
            || (ret = parse_arg_value(carg, 'c', &config->currentdir, next, &i))
            || (ret = parse_arg_value(carg, 'i', &config->initscript, next, &i))
            || (ret = parse_arg_list(carg, 'e', &config->envs, &config->envs_count, next, &i))
            || (ret = parse_arg_list(carg, 'v', &config->volumes, &config->volumes_count, next, &i))
            || (ret = parse_arg_flag(carg, 'k', &config->stop))
            || (ret = parse_arg_flag(carg, 'g', &config->gui))
            ) {
            if (ret == -1) {
                return -1;
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
        fprintf(stderr, "Unkown argument %s.\n", carg);
        return -1;
    }

    return 0;
}

static int parse_arg_print(char *arg, char value, void (*print)(char *), char *param)
{
    if (arg[1] == value && arg[2] == 0) {
        print(param);
        return -1;
    }
    return 0;
}

static int parse_arg_value(char *arg, char value, char **target, char *next, int *i)
{
    if (arg[1] != value) {
        return 0;
    }
    if (arg[2] == 0) {
        if (next == NULL) {
            fprintf(stderr, "Required argument not provided for option -%c.\n", value);
            return -1;
        }
        *target = next;
        (*i)++;
    } else if (arg[2] == '=') {
        *target = arg + 3;
    } else {
        *target = arg + 2;
    }
    return 1;
}

static int parse_arg_list(char *arg, char value, char ***target, size_t *count, char *next, int *i)
{
    char *change = NULL;
    int ret = parse_arg_value(arg, value, &change, next, i);
    if (ret <= 0) {
        return ret;
    }
    *target = mem_reallocate(*target, (*count + 1) * sizeof(char *));
    if (*target == NULL) {
        return -1;
    }
    (*target)[*count] = change;
    (*count)++;
    return 1;
}

static int parse_arg_flag(char *arg, char value, bool *target)
{
    if (arg[1] == value && arg[2] == 0) {
        *target = true;
        return 1;
    }
    return 0;
}

static void print_version(char *unused)
{
    puts("cage version " VERSION);
}

static void print_help(char *self)
{
    fprintf(stderr, "Usage: %s [options...] [--] [command...]\n", self);
}

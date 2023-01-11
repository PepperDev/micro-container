#include "config.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static bool parse_arg_print(char *, char, void (*)(char *), char *);

static bool parse_arg_value(char *, char, char **, char *, int *);

static bool parse_arg_list(char *, char, char ***, size_t *, char *, int *);

static bool parse_arg_flag(char *, char, bool *);

static void print_version(char *);

static void print_help(char *);

bool config_parse(config_t * config, int argc, char *argv[])
{
    int i;
    char *carg, *next;

    memset(config, 0, sizeof(config_t));
    for (i = 1; i < argc; i++) {
        carg = argv[i];
        if (carg[0] != '-') {
            config->command = &argv[i];
            config->command_count = argc - i;
            break;
        }

        if (parse_arg_print(carg, 'V', print_version, NULL)
            || parse_arg_print(carg, 'h', print_help, argv[0])
            ) {
            return false;
        }
        next = argv[i + 1];
        if (parse_arg_value(carg, 'n', &config->name, next, &i)
            || parse_arg_value(carg, 'a', &config->appdir, next, &i)
            || parse_arg_value(carg, 'l', &config->lowerdir, next, &i)
            || parse_arg_value(carg, 'w', &config->workdir, next, &i)
            || parse_arg_value(carg, 'p', &config->pidfile, next, &i)
            || parse_arg_value(carg, 'u', &config->user, next, &i)
            || parse_arg_value(carg, 'g', &config->group, next, &i)
            || parse_arg_value(carg, 'c', &config->currentdir, next, &i)
            || parse_arg_value(carg, 'i', &config->initscript, next, &i)
            || parse_arg_list(carg, 'e', &config->envs, &config->envs_count, next, &i)
            || parse_arg_list(carg, 'v', &config->volumes, &config->volumes_count, next, &i)
            || parse_arg_flag(carg, 'k', &config->stop)
            || parse_arg_flag(carg, 'g', &config->gui)
            ) {
            continue;
        }
        if (carg[1] == '-' && carg[2] == 0) {
            i++;
            if (i < argc) {
                config->command = &argv[i];
                config->command_count = argc - i;
            }
            break;
        }
        fprintf(stderr, "Unkown argument %s\n", carg);
        return false;
    }
    return true;
}

static bool parse_arg_print(char *arg, char value, void (*print)(char *), char *param)
{
    if(arg[1] == value && arg[2] == 0) {
        print(param);
        return true;
    }
    return false;
}

static bool parse_arg_value(char *arg, char value, char **target, char *next, int *i)
{
    if (arg[1] == value) {
        if (arg[2] == 0) {
            *target = next;
            (*i)++;
        } else if (arg[2] == '=') {
            *target = &arg[3];
        } else {
            *target = &arg[2];
        }
        return true;
    }
    return false;
}

static bool parse_arg_list(char *arg, char value, char ***target, size_t *count, char *next, int *i)
{
    char *change = NULL;
    if (parse_arg_value(arg, value, &change, next, i)) {
        *target = realloc(*target, (*count + 1) * sizeof(char *));
        assert(*target != NULL);
        (*target)[*count] = change;
        (*count)++;
        return true;
    }
    return false;
}

static bool parse_arg_flag(char *arg, char value, bool *target)
{
    if (arg[1] == value && arg[2] == 0) {
        *target = true;
        return true;
    }
    return false;
}

static void print_version(char *unused)
{
    puts("cage version " VERSION);
}

static void print_help(char *self)
{
    fprintf(stderr, "Usage: %s [options...] [--] [command...]\n", self);
}

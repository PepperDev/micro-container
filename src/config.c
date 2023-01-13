#include "config.h"
#include <stdio.h>
#include <string.h>

static bool parse_arg_print(char *, char, void (*)(char *), char *);

static bool parse_arg_value(char *, char, char **, char *, int *, bool *);

static bool parse_arg_list(char *, char, char ***, size_t *, char *, int *, bool *);

static bool parse_arg_flag(char *, char, bool *);

static void print_version(char *);

static void print_help(char *);

bool config_parse(config_t * config, int argc, char *argv[])
{
    int i;
    char *carg, *next;
    bool fatal = false;

    memset(config, 0, sizeof(config_t));
    for (i = 1; i < argc; i++) {
        carg = argv[i];
        if (carg[0] != '-') {
            config->command = argv + i;
            config->command_count = argc - i;
            break;
        }

        if (parse_arg_print(carg, 'V', print_version, NULL)
            || parse_arg_print(carg, 'h', print_help, argv[0])
            ) {
            return false;
        }
        next = argv[i + 1];
        if (parse_arg_value(carg, 'n', &config->name, next, &i, &fatal)
            || parse_arg_value(carg, 'a', &config->appdir, next, &i, &fatal)
            || parse_arg_value(carg, 'l', &config->lowerdir, next, &i, &fatal)
            || parse_arg_value(carg, 'w', &config->workdir, next, &i, &fatal)
            || parse_arg_value(carg, 'p', &config->pidfile, next, &i, &fatal)
            || parse_arg_value(carg, 'u', &config->user, next, &i, &fatal)
            || parse_arg_value(carg, 'g', &config->group, next, &i, &fatal)
            || parse_arg_value(carg, 'c', &config->currentdir, next, &i, &fatal)
            || parse_arg_value(carg, 'i', &config->initscript, next, &i, &fatal)
            || parse_arg_list(carg, 'e', &config->envs, &config->envs_count, next, &i, &fatal)
            || parse_arg_list(carg, 'v', &config->volumes, &config->volumes_count, next, &i, &fatal)
            || parse_arg_flag(carg, 'k', &config->stop)
            || parse_arg_flag(carg, 'g', &config->gui)
            ) {
            if (fatal) {
                return false;
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
        fprintf(stderr, "Unkown argument %s\n", carg);
        return false;
    }

    if (!config->pidfile) {
        // may use paths.h _PATH_VARRUN instead
        if (config->name) {
            size_t size = strlen(config->name);
            config->pidfile = malloc(size + 25);
            if (!config->pidfile) {
                // TODO: remove duplicated message
                fprintf(stderr, "Unable to allocate memory\n");
                return false;
            }
            memcpy(config->pidfile, "/run/microcontainer/", 20);
            memcpy(config->pidfile + 20, config->name, size);
            memcpy(config->pidfile + 20 + size, ".pid", 5);
        } else {
            config->pidfile = "/run/microcontainer/pid";
        }
    }

    return true;
}

static bool parse_arg_print(char *arg, char value, void (*print)(char *), char *param)
{
    if (arg[1] == value && arg[2] == 0) {
        print(param);
        return true;
    }
    return false;
}

static bool parse_arg_value(char *arg, char value, char **target, char *next, int *i, bool *fatal)
{
    if (arg[1] == value) {
        if (arg[2] == 0) {
            if (next == NULL) {
                fprintf(stderr, "Required argument not provided for option -%c\n", value);
                *fatal = true;
            } else {
                *target = next;
                (*i)++;
            }
        } else if (arg[2] == '=') {
            *target = arg + 3;
        } else {
            *target = arg + 2;
        }
        return true;
    }
    return false;
}

static bool parse_arg_list(char *arg, char value, char ***target, size_t *count, char *next, int *i, bool *fatal)
{
    char *change = NULL;
    if (parse_arg_value(arg, value, &change, next, i, fatal)) {
        if (fatal) {
            return true;
        }
        *target = realloc(*target, (*count + 1) * sizeof(char *));
        if (*target == NULL) {
            fprintf(stderr, "Unable to allocate memory\n");
            *fatal = true;
        } else {
            (*target)[*count] = change;
            (*count)++;
        }
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

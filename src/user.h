#ifndef USER_H
#define USER_H

#include <stdbool.h>
#include <sys/types.h>

typedef struct {
    uid_t uid;
    gid_t gid;
    gid_t *groups;
    size_t groups_count;
    char *name;
    size_t name_size;
    char *home;
    size_t home_size;
    char *shell;
    size_t shell_size;
    char *root_name;
    size_t root_name_size;
    char *root_home;
    size_t root_home_size;
    char *root_shell;
    size_t root_shell_size;
} user_t;

int parse_user(user_t *, char *, char *, char *, char *, bool, bool, bool, bool);

#endif

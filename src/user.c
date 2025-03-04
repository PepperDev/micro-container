#include "user.h"
#include "mem.h"
#include "io.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#define _GNU_SOURCE             // required for strchrnul, fgetpwent, fgetgrent
#include <string.h>
#include <pwd.h>
#include <grp.h>
#undef _GNU_SOURCE

typedef struct {
    gid_t gid;
    char *name;
    size_t size;
} group_t;

static int fill_groups(char *, group_t *, size_t, size_t);
static int fill_user(struct passwd *, char **, size_t *, char **, size_t *, char **, size_t *);

int parse_user(user_t *data, char *file_passwd, char *file_group, char *user, char *group, bool name, bool home,
               bool shell, bool root)
{
    memset(data, 0, sizeof(user_t));
    char separator = ':';

    bool found_uid = false;
    bool found_gid = false;
    bool free_group = false;

    size_t group_size = group ? strlen(group) : 0;
    if (user) {
        char *pos = strchrnul(user, separator);
        size_t size = pos - user;
        if (*pos) {
            *pos = 0;
            pos++;
            size_t suffix = strlen(pos);
            if (suffix) {
                if (group_size) {
                    group = mem_append(pos, suffix, &separator, 1, group, group_size);
                    if (!group) {
                        return -1;
                    }
                    group_size += suffix + 1;
                    free_group = true;
                } else {
                    group = pos;
                    group_size = suffix;
                }
            }
        }
        data->uid = strtoul(user, &pos, 10);
        if (*pos) {
            data->name = user;
            data->name_size = size;
        } else {
            found_uid = true;
        }
    } else {
        data->uid = 0;
        found_uid = true;
    }
    if (group_size) {
        size_t parse = 0;
        buffer_t buf = buffer_new(sizeof(group_t) * 5);
        if (!buf) {
            return -1;
        }
        size_t groups_count = 0;
        for (;;) {
            char *pos = memchr(group, separator, group_size);
            size_t size;
            if (pos) {
                *pos = 0;
                size = pos - group;
            } else {
                size = group_size;
            }
            char *aux;
            group_t item;
            item.gid = strtoul(group, &aux, 10);
            if (*aux) {
                item.name = group;
                item.size = size;
                parse++;
            } else {
                item.name = NULL;
            }
            buffer_write_data(buf, sizeof(group_t), &item);
            groups_count++;
            if (!pos) {
                break;
            }
            group = pos + 1;
            group_size -= size + 1;
        }
        group_t *groups = buffer_use(buf);
        if (!groups) {
            return -1;
        }
        if (parse && fill_groups(file_group, groups, groups_count, parse)) {
            return -1;
        }
        data->gid = groups[0].gid;
        data->groups = mem_allocate(sizeof(gid_t) * groups_count);
        if (!data->groups) {
            return -1;
        }
        // TODO: remove duplicates?
        for (int i = 0; i < groups_count; i++) {
            data->groups[i] = groups[i].gid;
        }
        data->groups_count = groups_count;
        free(groups);
        found_gid = true;
    }
    if (free_group) {
        free(group);
    }

    if (!found_uid || !found_gid || name || home || shell) {
        bool scan_user = true;
        bool scan_root = root;
        FILE *fd = fopen(file_passwd, "r");
        if (!fd) {
            fprintf(stderr, "Unable to open passwd %s.\n", file_passwd);
            return -1;
        }
        struct passwd *record;
        while ((scan_user || scan_root) && (record = fgetpwent(fd))) {
            if (scan_root && record->pw_uid == 0) {
                if (fill_user
                    (record, name ? &data->root_name : NULL, &data->root_name_size, home ? &data->root_home : NULL,
                     &data->root_home_size, shell ? &data->root_shell : NULL, &data->root_shell_size)) {
                    return -1;
                }
                scan_root = false;
            }
            if (scan_user && ((found_uid && record->pw_uid == data->uid)
                              || (!found_uid && !memcmp(data->name, record->pw_name, data->name_size + 1)))) {
                if (!found_uid) {
                    data->uid = record->pw_uid;
                }
                if (fill_user
                    (record, (found_uid && name) ? &data->name : NULL, &data->name_size, home ? &data->home : NULL,
                     &data->home_size, shell ? &data->shell : NULL, &data->shell_size)) {
                    return -1;
                }
                if (!found_gid) {
                    data->gid = record->pw_gid;
                    found_gid = true;
                }
                scan_user = false;
            }
        }
        if (fclose(fd)) {
            fprintf(stderr, "Unable to close passwds %s.\n", file_passwd);
            return -1;
        }
        if (scan_user && !found_uid) {
            fprintf(stderr, "Unable to parse user.\n");
            return -1;
        }
    }

    if (!found_gid) {
        data->gid = data->uid;
    }

    if (!data->groups) {
        // TODO: maybe read all groups for a given user if user name is given instead uid
        data->groups = mem_allocate(sizeof(gid_t));
        if (!data->groups) {
            return -1;
        }
        data->groups[0] = data->gid;
        data->groups_count = 1;
    }

    return 0;
}

static int fill_groups(char *file, group_t *list, size_t count, size_t left)
{
    FILE *fd = fopen(file, "r");
    if (!fd) {
        fprintf(stderr, "Unable to open groups %s.\n", file);
        return -1;
    }
    struct group *record;
    while (left && (record = fgetgrent(fd))) {
        for (int i = 0; i < count; i++) {
            if (list[i].name && !memcmp(list[i].name, record->gr_name, list[i].size + 1)) {
                list[i].gid = record->gr_gid;
                list[i].name = NULL;
                left--;
                // could break everycase if the list was unique, as we are not sure...
                if (!left) {
                    break;
                }
            }
        }
    }
    if (fclose(fd)) {
        fprintf(stderr, "Unable to close groups %s.\n", file);
        return -1;
    }
    if (left) {
        fprintf(stderr, "Unable to parse groups: ");
        bool first = true;
        for (int i = 0; i < count; i++) {
            if (list[i].name) {
                if (first) {
                    first = false;
                } else {
                    fprintf(stderr, ", ");
                }
                fprintf(stderr, "%s", list[i].name);
            }
        }
        fprintf(stderr, ".\n");
        return -1;
    }
    return 0;
}

static int fill_user(struct passwd *record, char **name, size_t *name_size, char **home, size_t *home_size,
                     char **shell, size_t *shell_size)
{
    if (name) {
        *name_size = strlen(record->pw_name);
        *name = mem_allocate(*name_size + 1);
        if (!*name) {
            return -1;
        }
        memcpy(*name, record->pw_name, *name_size + 1);
    }
    if (home) {
        *home_size = strlen(record->pw_dir);
        *home = mem_allocate(*home_size + 1);
        if (!*home) {
            return -1;
        }
        memcpy(*home, record->pw_dir, *home_size + 1);
    }
    if (shell) {
        *shell_size = strlen(record->pw_shell);
        *shell = mem_allocate(*shell_size + 1);
        if (!*shell) {
            return -1;
        }
        memcpy(*shell, record->pw_shell, *shell_size + 1);
    }
    return 0;
}

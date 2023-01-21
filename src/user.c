#include "user.h"
#include "mem.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#define _GNU_SOURCE             // required for strchrnul, fgetpwent, fgetgrent
#include <string.h>
#include <pwd.h>
#include <grp.h>
#undef _GNU_SOURCE

int check_superuser(int argc, char *argv[])
{
    if (geteuid() == 0) {
        if (getegid()) {
            if (setegid(0)) {
                fprintf(stderr, "Unable to change effective group.\n");
                return -1;
            }
        }
        struct stat fst;
        char *path = "/proc/self/exe";
        if (stat(path, &fst)) {
            fprintf(stderr, "Unable to access file %s.\n", path);
            return -1;
        }
        if (fst.st_uid != 0 || fst.st_gid != 0) {
            if (chown(path, 0, 0)) {
                fprintf(stderr, "Unable to change owner of file %s.\n", path);
                return -1;
            }
        }
        if ((fst.st_mode & S_ISUID) != S_ISUID) {
            if (chmod(path, fst.st_mode | S_ISUID)) {
                fprintf(stderr, "Unable to change mode of file %s.\n", path);
                return -1;
            }
        }
        return 0;
    }

    {
        char *newargs[argc + 4];
        newargs[0] = "sudo";
        newargs[1] = "--preserve-env";
        newargs[2] = "--";
        memcpy(newargs + 3, argv, (argc + 1) * sizeof(char *));
        execvp(newargs[0], newargs);
    }

    fprintf(stderr, "Unable to escalate privileges.\n");
    return -1;
}

int parse_user(user_t * data, char *file_passwd, char *file_group, char *user, char *group, bool name, bool home,
               bool shell, bool root)
{
    memset(data, 0, sizeof(user_t));
    char separator = ':';

    bool found_uid = false;
    bool found_gid = false;

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
                    group_size += suffix + 1;
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
            // look for uid and envs...
            // look for default group
        } else {
            found_uid = true;
            // look for envs only...
            // look for default group
        }
    }
    if (group) {
        // compute group
    }

    // user may be id or name
    // group may be id or name

    return 0;
}

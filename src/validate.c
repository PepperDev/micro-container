#include "validate.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

bool validate_superuser(int argc, char *argv[])
{
    uid_t uid = geteuid();
    if (uid == 0) {
        struct stat fst;
        char *path = "/proc/self/exe";
        if (stat(path, &fst)) {
            fprintf(stderr, "Unable to access file %s.\n", path);
            return false;
        }
        if (fst.st_uid != 0 || fst.st_gid != 0) {
            if (chown(path, 0, 0)) {
                fprintf(stderr, "Unable to change owner of file %s.\n", path);
                return false;
            }
        }
        if ((fst.st_mode & S_ISUID) != S_ISUID) {
            if (chmod(path, fst.st_mode | S_ISUID)) {
                fprintf(stderr, "Unable to change mode of file %s.\n", path);
                return false;
            }
        }
        return true;
    }

    {
        char *newargs[argc + 3];
        newargs[0] = "sudo";
        newargs[1] = "--";
        memcpy(newargs + 2, argv, (argc + 1) * sizeof(char *));
        execvp(newargs[0], newargs);
    }

    fprintf(stderr, "Unable to escalate privileges.\n");
    return false;

}

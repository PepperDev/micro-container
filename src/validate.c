#include "validate.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

bool validate_superuser(int argc, char *argv[])
{
    uid_t uid = geteuid();
    printf("uid: %d %d\n", uid, getuid());
    if (uid == 0) {
        struct stat fst;
        if (stat(argv[0], &fst)) {
            return false;
        }
        if (fst.st_uid != 0 || fst.st_gid != 0) {
            chown(argv[0], 0, 0);
        }
        if ((fst.st_mode & S_ISUID) != S_ISUID) {
            chmod(argv[0], fst.st_mode | S_ISUID);
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

    fprintf(stderr, "Unable to escalate privileges!\n");
    return false;

}

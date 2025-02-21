#include "super.h"
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

const char *super_do()
{
    struct stat fst;
    const char *path = "/proc/self/exe";
    if (stat(path, &fst)) {
        return "Unable to access self binary.\n";
    }
    if (fst.st_uid != 0 || fst.st_gid != 0) {
        if (chown(path, 0, 0)) {
            return "Unable to change owner of self binary.\n";
        }
    }
    if ((fst.st_mode & S_ISUID) != S_ISUID) {
        if (chmod(path, fst.st_mode | S_ISUID)) {
            return "Unable to change mode of self binary.\n";
        }
    }
    return NULL;
}

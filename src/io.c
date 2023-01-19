#include "io.h"
#include <sys/utsname.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int io_isoverlay2supported()
{
    struct utsname suname;
    if (uname(&suname)) {
        fprintf(stderr, "Unable to read kernel version.\n");
        return -1;
    }
    int major, minor;
    if (sscanf(suname.release, "%d.%d.%*s", &major, &minor) != 2) {
        fprintf(stderr, "Unable to parse kernel version.\n");
        return -1;
    }
    if (major > 3 || (major == 3 && minor >= 18)) {
        return 0;
    }
    return 1;
}

int io_exists(char *file)
{
    int ret = access(file, F_OK);
    if (ret) {
        if (errno == ENOENT) {
            return 1;
        }
        fprintf(stderr, "Unable check file %s existence.\n", file);
        return -1;
    }
    return 0;
}

int io_unlink(char *file)
{
    if (unlink(file)) {
        fprintf(stderr, "Unable to remove file %s\n", file);
        return -1;
    }
    return 0;
}

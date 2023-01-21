#include "io.h"
#include <sys/utsname.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

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

int io_mkdir(char *dir, size_t size)
{
    int ret = io_exists(dir);
    if (ret == -1) {
        return -1;
    }
    if (!ret) {
        struct stat fst;
        if (stat(dir, &fst)) {
            fprintf(stderr, "Unable to get stat of file %s\n", dir);
            return -1;
        }
        if (!S_ISDIR(fst.st_mode)) {
            fprintf(stderr, "File %s is supposed to be a directory\n", dir);
            return -1;
        }
        return 0;
    }
    size_t i = size - 1;
    while (i && dir[i] != '/') {
        i--;
    }
    while (i && dir[i] == '/') {
        i--;
    }
    if (i) {
        dir[i + 1] = 0;
        if (io_mkdir(dir, i)) {
            return -1;
        }
        dir[i + 1] = '/';
    }
    if (mkdir(dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) {
        fprintf(stderr, "Unable to create directory %s\n", dir);
        return -1;
    }
    return 0;
}

int io_touch(char *file)
{
    int fd = open(file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        fprintf(stderr, "Unable to open file %s\n", file);
        return -1;
    }
    if (close(fd)) {
        fprintf(stderr, "Unable to close file %s\n", file);
        return -1;
    }
    return 0;
}

int io_truncate(char *file, off_t size)
{
    int fd = open(file, O_WRONLY | O_NONBLOCK | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        fprintf(stderr, "Unable to create file %s\n", file);
        return -1;
    }
    if (ftruncate(fd, size)) {
        fprintf(stderr, "Unable to truncate file %s\n", file);
        return -1;
    }
    if (close(fd)) {
        fprintf(stderr, "Unable to close file %s\n", file);
        return -1;
    }
    return 0;
}

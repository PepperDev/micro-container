#include "io.h"
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/loop.h>

#define SECTOR_HEADER 4096

static int io_open(char *, int, int);
static int io_close(int);
static int io_stat(char *, struct stat *);

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

int io_chown(char *file, uid_t uid, gid_t gid)
{
    if (chown(file, uid, gid)) {
        fprintf(stderr, "Unable to change owner of %s\n", file);
        return -1;
    }
    return 0;
}

int io_touch(char *file)
{
    int fd = io_open(file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        return -1;
    }
    if (io_close(fd)) {
        return -1;
    }
    return 0;
}

int io_truncate(char *file, off_t size)
{
    int fd = io_open(file, O_WRONLY | O_NONBLOCK | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        return -1;
    }
    if (ftruncate(fd, size)) {
        fprintf(stderr, "Unable to truncate file %s\n", file);
        return -1;
    }
    if (io_close(fd)) {
        return -1;
    }
    return 0;
}

int io_blankfirststsector(char *file)
{
    int fd = io_open(file, O_RDONLY, 0);
    if (fd == -1) {
        return -1;
    }
    char buf[SECTOR_HEADER];
    if (read(fd, buf, SECTOR_HEADER) != SECTOR_HEADER) {
        fprintf(stderr, "Unable to read file %s\n", file);
        return -1;
    }
    if (io_close(fd)) {
        return -1;
    }
    for (int i = SECTOR_HEADER; i--;) {
        if (buf[i]) {
            return 1;
        }
    }
    return 0;
}

int io_samefs(char *source, char *target)
{
    struct stat fst;
    if (io_stat(source, &fst)) {
        return -1;
    }
    dev_t dev = fst.st_dev;
    if (io_stat(target, &fst)) {
        return -1;
    }
    if (dev == fst.st_dev) {
        return 0;
    }
    return 1;
}

int io_loop(char *dev, char *file)
{
    int fd = io_open("/dev/loop-control", O_RDWR | O_CLOEXEC, 0);
    if (fd == -1) {
        return -1;
    }
    int id = ioctl(fd, LOOP_CTL_GET_FREE);
    if (id == -1) {
        fprintf(stderr, "Unable to quey free loop.\n");
        return -1;
    }
    if (io_close(fd)) {
        return -1;
    }
    if (sprintf(dev, "/dev/loop%u", id) == -1) {
        fprintf(stderr, "Unable to parse loop.\n");
        return -1;
    }
    fd = io_open(file, O_RDWR, 0);
    if (fd == -1) {
        return -1;
    }
    int lfd = io_open(dev, O_RDWR, 0);
    if (lfd == -1) {
        return -1;
    }
    if (ioctl(lfd, LOOP_SET_FD, fd)) {
        fprintf(stderr, "Unable to set loop fd.\n");
        return -1;
    }
    struct loop_info info;
    if (ioctl(lfd, LOOP_GET_STATUS, &info)) {
        fprintf(stderr, "Unable query loop status.\n");
        return -1;
    }
    info.lo_flags |= LO_FLAGS_AUTOCLEAR;
    if (ioctl(lfd, LOOP_SET_STATUS, &info)) {
        fprintf(stderr, "Unable update loop status.\n");
        return -1;
    }
    if (io_close(lfd)) {
        return -1;
    }
    if (io_close(fd)) {
        return -1;
    }
    return 0;
}

static int io_open(char *file, int flags, int mode)
{
    int fd = open(file, flags, mode);
    if (fd == -1) {
        fprintf(stderr, "Unable to open file %s.\n", file);
    }
    return fd;
}

static int io_close(int fd)
{
    if (close(fd)) {
        fprintf(stderr, "Unable to close file.\n");
        return -1;
    }
    return 0;
}

static int io_stat(char *file, struct stat *fst)
{
    if (stat(file, fst)) {
        fprintf(stderr, "Unable to stat file %s.\n", file);
        return -1;
    }
    return 0;
}

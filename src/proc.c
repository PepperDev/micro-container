#include "proc.h"
#include "mem.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#define _POSIX_SOURCE           // required for kill, nanosleep
#include <signal.h>
#include <time.h>
#undef _POSIX_SOURCE
#include <sys/file.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

int killpid(char *name, char *pidfile)
{
    if (!pidfile) {
        pidfile = compute_pidfile(name, name ? strlen(name) : 0);
        if (!pidfile) {
            return -1;
        }
    }

    if (access(pidfile, F_OK)) {
        return 0;
    }

    int fd;
    pid_t pid = readpid(pidfile, &fd);
    if (pid == -1) {
        return -1;
    }

    bool dowait = true;
    if (kill(pid, SIGTERM)) {
        if (errno != ESRCH) {
            fprintf(stderr, "Unable to send term signal to %d.\n", pid);
            return -1;
        }
        dowait = false;
    }

    if (dowait) {
        int count = 50;
        struct timespec tv = {
            .tv_sec = 0,
            .tv_nsec = 100000000
        };
        // TODO: if kernel >= 5.3 use pidfd_open
        for (; count > 0; count--) {
            if (kill(pid, 0) && errno == ESRCH) {
                dowait = false;
                break;
            }
            nanosleep(&tv, &tv);
        }
        if (dowait) {
            if (kill(pid, SIGKILL)) {
                if (errno != ESRCH) {
                    fprintf(stderr, "Unable to send term signal to %d.\n", pid);
                    return -1;
                }
                dowait = false;
            }
            if (dowait && waitpid(pid, NULL, 0) == -1 && errno != ECHILD) {
                fprintf(stderr, "Unable to wait process %d.\n", pid);
                return -1;
            }
        }
    }

    if (unlink(pidfile)) {
        fprintf(stderr, "Unable to remove pidfile %s\n", pidfile);
        return -1;
    }

    if (close_pid(fd)) {
        return -1;
    }
    return 0;
}

char *compute_pidfile(char *name, size_t size)
{
    // may use paths.h _PATH_VARRUN instead
    if (!name) {
        return "/run/microcontainer/pid";
    }
    return mem_append("/run/microcontainer/", 20, name, size, ".pid", 5);
}

pid_t readpid(char *pidfile, int *fd)
{
    *fd = open(pidfile, O_RDONLY);
    if (*fd == -1) {
        fprintf(stderr, "Unable to open pidfile %s.\n", pidfile);
        return -1;
    }

    if (flock(*fd, LOCK_EX)) {
        fprintf(stderr, "Unable to lock pidfile %s.\n", pidfile);
        return -1;
    }

    char buf[65];
    ssize_t size = read(*fd, buf, 64);
    if (size == -1) {
        fprintf(stderr, "Unable to read pidfile %s.\n", pidfile);
        return -1;
    }
    buf[size] = 0;

    errno = 0;
    long value = atol(buf);     // strtol(buf, NULL, 10);
    if (value == 0 && errno) {
        fprintf(stderr, "Unable to parse pidfile %s.\n", pidfile);
        return -1;
    }

    return value;
}

int close_pid(int fd)
{
    if (flock(fd, LOCK_UN)) {
        fprintf(stderr, "Unable to unlock pidfile.\n");
        return -1;
    }
    if (close(fd)) {
        fprintf(stderr, "Unable to close pidfile.\n");
        return -1;
    }
    return 0;
}

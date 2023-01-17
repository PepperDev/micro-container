#include "proc.h"
#include "mem.h"
#include <unistd.h>
#define _POSIX_SOURCE           // required for fileno, kill, nanosleep
#include <stdio.h>
#include <signal.h>
#include <time.h>
#undef _POSIX_SOURCE
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

int killpid(char *name, char *pidfile)
{
    FILE *file;
    pid_t pid;
    bool dowait = true;

    if (!pidfile) {
        pidfile = compute_pidfile(name, name ? strlen(name) : 0);
        if (!pidfile) {
            return -1;
        }
    }

    if (access(pidfile, F_OK)) {
        return -1;
    }

    file = fopen(pidfile, "r");
    if (!file) {
        fprintf(stderr, "Unable to open pidfile %s.\n", pidfile);
        return -1;
    }

    if (flock(fileno(file), LOCK_EX)) {
        fprintf(stderr, "Unable to lock pidfile %s.\n", pidfile);
        return -1;
    }

    if (fscanf(file, "%d", &pid) != 1) {
        fprintf(stderr, "Unable to read pidfile %s.\n", pidfile);
        return -1;
    }

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

    fclose(file);
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

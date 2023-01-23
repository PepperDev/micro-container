#include "proc.h"
#include "mem.h"
#include "io.h"
#define _GNU_SOURCE             // required for vfork
#include <unistd.h>
#undef _GNU_SOURCE
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

static int lockfd(int);

pid_t pidfork()
{
    // use vfork or clone with CLONE_VM | CLONE_VFORK
    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "Unable to fork process.\n");
    }
    return pid;
}

int killpid(char *name, char *pidfile)
{
    if (!pidfile) {
        pidfile = compute_pidfile(name, name ? strlen(name) : 0, NULL);
        if (!pidfile) {
            return -1;
        }
    }

    int ret = io_exists(pidfile);
    if (ret == -1) {
        return -1;
    }
    if (ret) {
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
            int ret = pidexists(pid);
            if (ret == -1) {
                return -1;
            }
            if (ret) {
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
            if (dowait && pidwait(pid, NULL)) {
                return -1;
            }
        }
    }

    ret = io_exists(pidfile);
    if (ret == -1) {
        return -1;
    }
    if (!ret && io_unlink(pidfile)) {
        return -1;
    }

    if (close_pid(fd)) {
        return -1;
    }
    return 0;
}

char *compute_pidfile(char *name, size_t size, size_t *len)
{
    // may use paths.h _PATH_VARRUN instead
    if (!name) {
        if (len) {
            *len = 23;
        }
        return mem_append("/run/microcontainer/pid", 23, NULL, 0, NULL, 0);
    }
    if (len) {
        *len = 24 + size;
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

    if (lockfd(*fd)) {
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

int create_pidfile(char *pidfile, size_t size)
{
    size_t i = size - 1;
    while (i && pidfile[i] != '/') {
        i--;
    }
    while (i && pidfile[i] == '/') {
        i--;
    }
    if (i) {
        pidfile[i + 1] = 0;
        if (io_mkdir(pidfile, i)) {
            return -1;
        }
        pidfile[i + 1] = '/';
    }
    int fd = open(pidfile, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        fprintf(stderr, "Unable to create pidfile %s.\n", pidfile);
        return -1;
    }

    if (lockfd(fd)) {
        return -1;
    }

    return fd;
}

static int lockfd(int fd)
{
    if (flock(fd, LOCK_EX)) {
        fprintf(stderr, "Unable to lock pidfile.\n");
        return -1;
    }
    return 0;
}

int writepid(int fd, pid_t pid)
{
    char buf[64];
    int size = snprintf(buf, 64, "%d", pid);
    if (size == -1) {
        fprintf(stderr, "Unable to parse pid %d.\n", pid);
        return -1;
    }
    if (write(fd, buf, size) != size) {
        fprintf(stderr, "Unable to write pid %d into pidfile.\n", pid);
        return -1;
    }
    if (close_pid(fd)) {
        return -1;
    }
    return 0;
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

int pidwait(pid_t pid, int *status)
{
    if (waitpid(pid, status, 0) == -1 && errno != ECHILD) {
        fprintf(stderr, "Unable to wait process %d.\n", pid);
        return -1;
    }
    // if status try to parse it?
    // WTERMSIG(status) || WEXITSTATUS(status)
    // WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status)
    return 0;
}

int pidexists(pid_t pid)
{
    if (kill(pid, 0)) {
        if (errno == ESRCH) {
            return 1;
        }
        fprintf(stderr, "Unable to check process %d existence.\n", pid);
        return -1;
    }
    return 0;
}

int fork_and_exec(char **args)
{
    pid_t pid = vfork();
    if (pid == -1) {
        fprintf(stderr, "Unable to fork process.\n");
    }
    if (!pid) {
        execvp(args[0], args);
        return -1;
    }
    int status = -1;
    if (pidwait(pid, &status)) {
        return -1;
    }
    if (status) {
        fprintf(stderr, "Process exited with failure %s %d.\n", args[0], status);
        return -1;
    }
    return 0;
}

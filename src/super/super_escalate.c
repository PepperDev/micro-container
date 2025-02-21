#include "super.h"
#include <stddef.h>
#define _GNU_SOURCE
#include <unistd.h>
#undef _GNU_SOURCE
#include <fcntl.h>
#include <sys/wait.h>

const char *super_escalate(int argc, char *argv[])
{
    if (geteuid() == 0) {
        if (getegid()) {
            if (setegid(0)) {
                return "Unable to change effective group.\n";
            }
        }
        return NULL;
    }

    pid_t pid = vfork();
    if (pid < 0) {
        return "Unable to spawn child proccess.\n";
    }

    if (pid == 0) {
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd < 0) {
            return "Unable to open null file descriptor.\n";
        }
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDIN_FILENO);
        close(null_fd);
        char *args[] = {
            "sudo",
            "--",
            argv[0],
            "--fix",
            NULL
        };
        execvp(args[0], args);
        return "Unable to execute sudo.\n";
    }

    int status;
    if (waitpid(pid, &status, 0) <= 0) {
        return "Unable to wait for child proccess.\n";
    }
    if (!WIFEXITED(status)) {
        return "Child terminated.\n";
    }
    if (WEXITSTATUS(status)) {
        return "Unable to escalate privileges.\n";
    }

    execvp(argv[0], argv);
    return "Unable to retry provilege escalation.\n";
}

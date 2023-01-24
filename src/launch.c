#include "launch.h"
#include "proc.h"
#define _GNU_SOURCE             // required for vfork, setreuid, setregid, setgroups, execvpe
#include <unistd.h>
#undef _GNU_SOURCE
#define _POSIX_SOURCE           // required for setenv
#include <stdlib.h>
#undef _POSIX_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef struct {
    uid_t uid;
    gid_t gid;
    size_t groups_count;
    gid_t *groups;
    char *dir;
    char *command;
    char **args;
    char **envs;
} exec_t;

static int launch_exec(exec_t *);

int launch(launch_t * launch)
{
    if (setenv("PATH", launch->path, 1)) {
        fprintf(stderr, "Unable to set PATH environment.\n");
        return -1;
    }
    if (launch->init) {
        pid_t pid = vfork();
        if (pid == -1) {
            fprintf(stderr, "Unable to fork new process.\n");
            return -1;
        }
        if (pid == 0) {
            exec_t value = {
                .uid = 0,
                .gid = 0,
                .groups_count = 0,
                .groups = NULL,
                .dir = launch->dir,
                .command = launch->init,
                .args = launch->init_args,
                .envs = launch->init_envs
            };
            return launch_exec(&value);
        }
        int status = -1;
        if (pidwait(pid, &status)) {
            return -1;
        }
        if (status) {
            fprintf(stderr, "Init process failed.\n");
            return -1;
        }
    }
    exec_t value = {
        .uid = launch->uid,
        .gid = launch->gid,
        .groups_count = launch->groups_count,
        .groups = launch->groups,
        .dir = launch->dir,
        .command = launch->command,
        .args = launch->args,
        .envs = launch->envs
    };
    return launch_exec(&value);
}

static int launch_exec(exec_t * launch)
{
    if (launch->dir && chdir(launch->dir)) {
        fprintf(stderr, "Unable to change directory to %s.\n", launch->dir);
        return -1;
    }
    if (setregid(launch->gid, launch->gid)) {
        fprintf(stderr, "Unable to change group.\n");
        return -1;
    }
    if (setgroups(launch->groups_count, launch->groups)) {
        fprintf(stderr, "Unable to change group list.\n");
        return -1;
    }
    if (setreuid(launch->uid, launch->uid)) {
        fprintf(stderr, "Unable to change user.\n");
        return -1;
    }
    // TODO: umask 002?
    execvpe(launch->command, launch->args, launch->envs);
    fprintf(stderr, "Unable to launch command.\n");
    return -1;
}

#ifndef CAGE_CGROUP_H
#define CAGE_CGROUP_H

// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>
#include "util/mem.h"

int cgroup_enter(const char *root, int is_cgroup2);
int cgroup_get_type(void);
int cgroup_read_filesystems(buf_t * b);
int cgroup_detect_mount(int *root_private);
int cgroup_pre_unshare_at(const char *root, int fs_type, pid_t pid);
int cgroup_pre_unshare(int fs_type, pid_t pid);
void cgroup_cleanup(pid_t pid, int fs_type);
void cgroup_cleanup_at(const char *root, pid_t pid, int fs_type);
int cgroup_rmdir_recursive(const char *path);
int mount_cgroups(const char *root, int has_cgroup);
int read_cgroups_file(buf_t * b);
int read_proc_cgroups(char (*names)[64]);
int write_pid(const char *path, pid_t pid);
char *advance_line(char *p);
int cgroup_v1_enter_one(const char *cg, const char *ctrl, const char *name, pid_t pid);
int cgroup_v1_enter(const char *root, const char *name, pid_t pid);
int cgroup_v2_enter(const char *root, const char *name, pid_t pid);
int build_subtree_value(const char *in, size_t len, char *out, size_t cap, size_t *out_len);
int write_subtree(const char *cg, const char *val, size_t vlen);
int enable_cgroup_controllers(const char *cg);
int mount_cgroup_tmpfs(const char *cg);
int mount_cgroup_v1(const char *root);
int mount_cgroup_v2(const char *root);

#endif

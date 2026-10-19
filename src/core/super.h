#ifndef CAGE_SUPER_H
#define CAGE_SUPER_H

// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

int super_fix(void);
int super_escalate(char **argv, uid_t euid, uid_t ruid, gid_t egid, gid_t rgid);

int is_suid_binary(void);

#endif

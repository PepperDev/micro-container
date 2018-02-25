#include <sys/mount.h>
#include <unistd.h>
#include <sys/types.h>

#include "var.h"
#include "uid.h"
#include "config.h"
#include "user.h"

// TODO: volumes, x11, pulse
// /proc/self/mountinfo \040 -> space

void container(const char *root) {
	uid_t uid;
	gid_t gid;
	char *home;
	calc_user(root, &home, &uid, &gid);
	//clone2
	//chroot
	//execve
	//setns(int fd, int nstype);
	//sigaction
}

#include <sys/mount.h>
#include <unistd.h>
#include <sys/types.h>

#include "var.h"
#include "uid.h"
#include "config.h"

void container(const char *root) {
	//clone2
	//chroot
	//execve
	//setns(int fd, int nstype);
	//sigaction
}

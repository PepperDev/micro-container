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
	// TODO: check if home exists, if not copy from /etc/skel
	// TODO: check and copy if not exists: .inputrc .vimrc .gitconfig .screenrc
	// TODO: mount /dev /dev/pts /sys /proc? /run
	// TODO: if ssd mount tmpfs on /tmp /var/tmp
	// TODO: clear /tmp /var/tmp
	// TODO: create dir /run/lock /run/user /run/resolvconf
	// TODO: if /dev/shm is link resolve it till the end, if the result does not exists create dir, if is not link create it to /run/shm, on the end mount tmpfs
	// TODO: mount tmp for /run/lock
	// TODO: if systemd create dir /.. and mount tmpfs
	// TODO: create dir 755 root:utmp /run/screen
	// TODO: copy from host:/etc/resolv.conf to container:/run/resolvconf/resolv.conf and create link if it does not exists
	// TODO: write pid and lock files
	// TODO: perform chroot, unbind and launch application, if it is the first instance clone2 and use execv, if is not use just execv
	// TODO: to unbind use setns
	// TODO: to launch use setuid and setgid
	// TODO: for the first instance use signals (sigaction) and the lock file to control other instances
}

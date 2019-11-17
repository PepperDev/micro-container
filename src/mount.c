#define _GNU_SOURCE
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <sys/mount.h>
#include <stdlib.h>

#include "mount.h"
#include "config.h"
#include "buffer.h"

static void buffer_append_opt(buffer, char*, size_t);

void prepare_mounts()
{
	if (unshare(CLONE_NEWNS | CLONE_NEWPID))
	{
		fprintf(
			stderr,
			"Warning: unable to unshare mount and pid!\n"
		);
	}
	pid_t pid = vfork();
	if (pid != 0) {
		int status;
		waitpid(pid, &status, 0);
		exit(0);
	}
	// clone() -> CLONE_VM | CLONE_VFORK | SIGCHLD
	char *type = "overlayfs";
	buffer buf = buffer_new(1024);
	buffer_write_data(buf, 9, "lowerdir=");
	buffer_append_opt(buf, config_lowerdir, config_lowerdir_size);
	buffer_write_data(buf, 10, ",upperdir=");
	buffer_append_opt(buf, computed_upperdir, computed_upperdir_size);
	if (computed_workdir != NULL)
	{
		type = "overlay";
		buffer_write_data(buf, 9, ",workdir=");
		buffer_append_opt(buf, computed_workdir, computed_workdir_size);
	}
	buffer_write_byte(buf, 0);
	char *opt = buffer_reuse(buf);
	if (mount(type, computed_rootdir, type, 0, opt))
	{
		fprintf(
			stderr,
			"Fatal: unable to mount overlay \"%s\"!\n",
			computed_rootdir
		);
		exit(1);
	}
	free(opt);
	if (mount(NULL, computed_rootdir, NULL, MS_PRIVATE, NULL))
	{
		fprintf(
			stderr,
			"Fatal: unable to make overlay private \"%s\"!\n",
			computed_rootdir
		);
		exit(1);
	}
	// TODO: mount other lowers

	buf = buffer_new_from(computed_rootdir_size, computed_rootdir);
	buffer_write_data(buf, 5, "/dev");
	opt = buffer_reuse(buf);
	if (mount("/dev", opt, NULL, MS_BIND, NULL))
	{
		fprintf(
			stderr,
			"Fatal: unable to mount bind \"%s\"!\n",
			opt
		);
		exit(1);
	}
	if (mount(NULL, opt, NULL, MS_UNBINDABLE, NULL))
	{
		fprintf(
			stderr,
			"Fatal: unable to make bind unbindable \"%s\"!\n",
			opt
		);
		exit(1);
	}
	free(opt);

	buf = buffer_new_from(computed_rootdir_size, computed_rootdir);
	buffer_write_data(buf, 9, "/dev/pts");
	opt = buffer_reuse(buf);
	if (mount("/dev/pts", opt, NULL, MS_BIND, NULL))
	{
		fprintf(
			stderr,
			"Fatal: unable to mount bind \"%s\"!\n",
			opt
		);
		exit(1);
	}
	if (mount(NULL, opt, NULL, MS_UNBINDABLE, NULL))
	{
		fprintf(
			stderr,
			"Fatal: unable to make bind unbindable \"%s\"!\n",
			opt
		);
		exit(1);
	}
	free(opt);

	buf = buffer_new_from(computed_rootdir_size, computed_rootdir);
	buffer_write_data(buf, 6, "/proc");
	opt = buffer_reuse(buf);
	if (mount("proc", opt, "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL))
	{
		fprintf(
			stderr,
			"Fatal: unable to mount proc \"%s\"!\n",
			opt
		);
		exit(1);
	}
	free(opt);

	buf = buffer_new_from(computed_rootdir_size, computed_rootdir);
	buffer_write_data(buf, 5, "/sys");
	opt = buffer_reuse(buf);
	if (mount("sysfs", opt, "sysfs", 0, NULL))
	{
		fprintf(
			stderr,
			"Fatal: unable to mount sys \"%s\"!\n",
			opt
		);
		exit(1);
	}
	free(opt);
	// rotational? tmpfs /tmp and /var/tmp
	// mount -t tmpfs -o rw,noexec,nosuid,mode=0755 tmpfs "$root/run"
	// ... /etc/resolv.conf
	// ... /dev/shm
	// ... /run/lock
}

static void buffer_append_opt(buffer buf, char *data, size_t size)
{
	char *last = data;
	char *pos = data;
	char *end = data + size;

	while (pos < end)
	{
		if (*pos == '\\' || *pos == ',')
		{
			if (last < pos)
			{
				buffer_write_data(buf, pos - last, last);
			}
			buffer_write_byte(buf, '\\');
			buffer_write_byte(buf, *pos);
			last = pos + 1;
		}
		pos++;
	}

	if (last < end)
	{
		buffer_write_data(buf, end - last, last);
	}
}

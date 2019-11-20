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
#include "io.h"

static char* compose_path(char*, size_t, char*, size_t, size_t*);

static void buffer_append_opt(buffer, char*, size_t);

static void mount_overlay(char*, char*, size_t, char*, size_t, char*, size_t);

static void mount_overlay_extra(
	char*, size_t, char*, size_t,
	char*, size_t, char*, size_t,
	char*, size_t, size_t
);

static void mount_bind(char*, size_t, char*, size_t);

static void mount_type(
	char*, size_t, char*, size_t, char*, unsigned long, void*
);

static void root_mkdir(char*, size_t, char*, size_t);

void prepare_mounts()
{
	if (unshare(CLONE_NEWNS | CLONE_NEWPID))
	{
		fprintf(
			stderr,
			"Warning: unable to unshare mount and pid!\n"
		);
	}
	// if vfork() becomes a problem use clone() with CLONE_VM | CLONE_VFORK
	pid_t pid = vfork();
	if (pid != 0) {
		waitpid(pid, NULL, 0);
		// TODO: unload?
		exit(EXIT_SUCCESS);
	}

	mount_overlay(
		computed_rootdir, config_lowerdir, config_lowerdir_size,
		computed_upperdir, computed_upperdir_size,
		computed_workdir, computed_workdir_size
	);
	for (size_t i = 0; i < computed_lowerdirs_count; i++)
	{
		mount_overlay_extra(
			computed_rootdir, computed_rootdir_size,
			config_lowerdir, config_lowerdir_size,
			computed_upperdir, computed_upperdir_size,
			computed_workdir, computed_workdir_size,
			computed_lowerdirs[i], computed_lowerdirs_sizes[i], i
		);
	}

	mount_bind(
		computed_rootdir, computed_rootdir_size,
		"/dev", 4
	);

	mount_bind(
		computed_rootdir, computed_rootdir_size,
		"/dev/pts", 8
	);

	mount_type(
		computed_rootdir, computed_rootdir_size,
		"/proc", 5, "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL
	);

	mount_type(
		computed_rootdir, computed_rootdir_size,
		"/sys", 4, "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL
	);

	if (!io_isrotational(computed_upperdir))
	{
		mount_type(
			computed_rootdir, computed_rootdir_size,
			"/tmp", 4, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOATIME, NULL
		);
		mount_type(
			computed_rootdir, computed_rootdir_size,
			"/var/tmp", 8, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOATIME, NULL
		);
	}

	mount_type(
		computed_rootdir, computed_rootdir_size,
		"/run", 4, "tmpfs", MS_NOSUID | MS_NOEXEC, "mode=0755"
	);

	root_mkdir(
		computed_rootdir, computed_rootdir_size,
		"/run/lock", 9
	);
	root_mkdir(
		computed_rootdir, computed_rootdir_size,
		"/run/user", 9
	);

	char *resolv = compose_path(
		computed_rootdir, computed_rootdir_size,
		"/etc/resolv.conf", 16, NULL
	);
	if (io_islink(resolv))
	{
		// TODO: ...
	}

	// TODO: /dev/shm

	mount_type(
		computed_rootdir, computed_rootdir_size,
		"/run/lock", 9, "tmpfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL
	);

	// TODO: /run/user/[uid]
	// TODO: /sys/fs/cgroup

	// TODO: /run/screen

	// TODO: cp /etc/resolv.conf
	free(resolv);

	// TODO: /run/user/[uid]/pulse

	// TODO: mount bind volumes
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

static char* compose_path(
	char *root, size_t root_size, char *dir, size_t dir_size, size_t *size
)
{
	buffer buf = buffer_new_from(root_size, root);
	if (root[root_size - 1] != PATH_SEPARATOR)
	{
		buffer_write_byte(buf, PATH_SEPARATOR);
	}
	if (dir[0] == PATH_SEPARATOR)
	{
		dir++;
		dir_size--;
	}
	buffer_write_data(buf, dir_size, dir);
	if (size != NULL)
	{
		*size = buffer_length(buf);
	}
	buffer_write_byte(buf, 0);
	return buffer_reuse(buf);
}

static void mount_overlay(
	char *root, char *lower, size_t lower_size,
	char *upper, size_t upper_size, char *work, size_t work_size
)
{
	char *type = "overlayfs";
	buffer buf = buffer_new(1024);
	buffer_write_data(buf, 9, "lowerdir=");
	buffer_append_opt(buf, lower, lower_size);
	buffer_write_data(buf, 10, ",upperdir=");
	buffer_append_opt(buf, upper, upper_size);
	if (work != NULL)
	{
		type = "overlay";
		buffer_write_data(buf, 9, ",workdir=");
		buffer_append_opt(buf, work, work_size);
	}
	buffer_write_byte(buf, 0);
	char *opt = buffer_reuse(buf);
	if (mount(type, root, type, 0, opt))
	{
		fprintf(
			stderr,
			"Fatal: unable to mount overlay \"%s\"!\n",
			root
		);
		exit(1);
	}
	free(opt);
	if (mount(NULL, root, NULL, MS_PRIVATE, NULL))
	{
		fprintf(
			stderr,
			"Fatal: unable to make overlay private \"%s\"!\n",
			root
		);
		exit(1);
	}
}

static void mount_overlay_extra(
	char *root, size_t root_size, char *lower, size_t lower_size,
	char *upper, size_t upper_size, char *work, size_t work_size,
	char *extra, size_t extra_size, size_t id
)
{
	// TODO: concatenate upper and work directories
	// TODO: if work or upper does not exists, create recursively, for upper make sure to keep same permissions, ownership and timestamps
	// TODO: make sure null terminated root
}

static void mount_bind(
	char *root, size_t root_size, char *bind, size_t bind_size
)
{
	char *point = compose_path(root, root_size, bind, bind_size, NULL);
	if (mount(bind, point, NULL, MS_BIND, NULL))
	{
		fprintf(
			stderr,
			"Fatal: unable to mount bind \"%s\"!\n",
			point
		);
		exit(1);
	}
	if (mount(NULL, point, NULL, MS_UNBINDABLE, NULL))
	{
		fprintf(
			stderr,
			"Fatal: unable to make bind unbindable \"%s\"!\n",
			point
		);
		exit(1);
	}
	free(point);
}

static void mount_type(
	char *root, size_t root_size, char *point, size_t point_size, char *type,
	unsigned long flags, void *data
)
{
	char *full = compose_path(root, root_size, point, point_size, NULL);
	if (mount(type, full, type, flags, data))
	{
		fprintf(
			stderr,
			"Fatal: unable to mount %s \"%s\"!\n",
			type,
			full
		);
		exit(1);
	}
	free(full);
}

static void root_mkdir(char *root, size_t root_size, char *dir, size_t dir_size)
{
	char *full = compose_path(root, root_size, dir, dir_size, NULL);
	if (!io_mkdir(full, 0, 0, 0))
	{
		fprintf(
			stderr,
			"Warning: unable to create directory \"%s\"!\n",
			full
		);
	}
	free(full);
}

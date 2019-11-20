#define _GNU_SOURCE
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <string.h>

#include "mount.h"
#include "config.h"
#include "buffer.h"
#include "io.h"

static const char PATH_DEV_SHM[] = "/dev/shm";
static const char TYPE_TMPFS[]   = "tmpfs";

static char* compose_path(const char*, size_t, const char*, size_t, size_t*);

static void buffer_append_opt(buffer, const char*, size_t);

static void mount_overlay(
	const char*, const char*, size_t,
	const char*, size_t, const char*, size_t
);

static void mount_overlay_extra(
	const char*, size_t, const char*, size_t,
	const char*, size_t, const char*, size_t,
	const char*, size_t, size_t
);

static void mount_bind(const char*, size_t, const char*, size_t);

static void mount_type(
	const char*, size_t, const char*, size_t,
	const char*, unsigned long, const void*
);

static void root_mkdir(const char*, size_t, const char*, size_t);

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
			"/tmp", 4, TYPE_TMPFS, MS_NOSUID | MS_NODEV | MS_NOATIME, NULL
		);
		mount_type(
			computed_rootdir, computed_rootdir_size,
			"/var/tmp", 8, TYPE_TMPFS, MS_NOSUID | MS_NODEV | MS_NOATIME, NULL
		);
	}

	mount_type(
		computed_rootdir, computed_rootdir_size,
		"/run", 4, TYPE_TMPFS, MS_NOSUID | MS_NOEXEC, "mode=0755"
	);

	char *lock = compose_path(
		computed_rootdir, computed_rootdir_size,
		"/run/lock", 9, NULL
	);
	root_mkdir(
		NULL, 0,
		lock, 0
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
		// TODO: resolv path...
	}

	char *shm = NULL;
	if (io_islink(PATH_DEV_SHM))
	{
		char *path = io_realpath(PATH_DEV_SHM);
		if (path == NULL)
		{
			fprintf(
				stderr,
				"Fatal: unable to compute shared memory path!\n"
			);
			exit(1);
		}
		shm = compose_path(
			computed_rootdir, computed_rootdir_size,
			path, strlen(path), NULL
		);
		free(path);
		if (!io_isdir(shm) && !io_mkdir(shm, 0, 0, 0))
		{
			fprintf(
				stderr,
				"Fatal: unable to create directory \"%s\"!\n",
				shm
			);
			exit(1);
		}
	}
	else
	{
		shm = compose_path(
			computed_rootdir, computed_rootdir_size,
			PATH_DEV_SHM, sizeof(PATH_DEV_SHM) - 1, NULL
		);
		char *path = compose_path(
			computed_rootdir, computed_rootdir_size,
			"/run/shm", 8, NULL
		);
		if (!io_symlink(PATH_DEV_SHM, path))
		{
			fprintf(
				stderr,
				"Fatal: unable to create symbolic link \"%s\"!\n",
				path
			);
			exit(1);
		}
		free(path);
	}
	mount_type(
		NULL, 0, shm, 0,
		TYPE_TMPFS, MS_NOSUID | MS_NODEV, NULL
	);
	free(shm);

	mount_type(
		NULL, 0, lock, 0,
		TYPE_TMPFS, MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL
	);
	free(lock);

	// TODO: /run/user/[uid]
	// TODO: /sys/fs/cgroup

	// TODO: /run/screen

	// TODO: cp /etc/resolv.conf
	free(resolv);

	// TODO: force user discover?
	// TODO: /run/user/[uid]/pulse

	// TODO: mount bind volumes
}

static void buffer_append_opt(buffer buf, const char *data, size_t size)
{
	char *pos = (char*) data;
	char *last = pos;
	char *end = pos + size;

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
	const char *root, size_t root_size,
	const char *dir, size_t dir_size, size_t *size
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
	const char *root, const char *lower, size_t lower_size,
	const char *upper, size_t upper_size, const char *work, size_t work_size
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
	const char *root, size_t root_size, const char *lower, size_t lower_size,
	const char *upper, size_t upper_size, const char *work, size_t work_size,
	const char *extra, size_t extra_size, size_t id
)
{
	// TODO: concatenate upper and work directories
	// TODO: if work or upper does not exists, create recursively, for upper make sure to keep same permissions, ownership and timestamps
	// TODO: make sure null terminated root
}

static void mount_bind(
	const char *root, size_t root_size, const char *bind, size_t bind_size
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
	const char *root, size_t root_size,
	const char *point, size_t point_size,
	const char *type, unsigned long flags, const void *data
)
{
	char *full;
	if (root == NULL)
	{
		full = (char*) point;
	}
	else
	{
		full = compose_path(root, root_size, point, point_size, NULL);
	}
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
	if (root != NULL)
	{
		free(full);
	}
}

static void root_mkdir(
	const char *root, size_t root_size,
	const char *dir, size_t dir_size
)
{
	char *full;
	if (root == NULL)
	{
		full = (char*) dir;
	}
	else
	{
		full = compose_path(root, root_size, dir, dir_size, NULL);
	}
	if (!io_mkdir(full, 0, 0, 0))
	{
		fprintf(
			stderr,
			"Warning: unable to create directory \"%s\"!\n",
			full
		);
	}
	if (root != NULL)
	{
		free(full);
	}
}

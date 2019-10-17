#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <assert.h>

#include "validate.h"
#include "config.h"
#include "io.h"
#include "user.h"
#include "buffer.h"

static void validate_special(const char*);
static void validate_base();
static void validate_library();
static void validate_lower();
static void validate_user();
/*
static void validate_volumes();
static void validate_scripts();
static void compute_rootdir();
static void compute_appdir();
static void compute_upperdir(); //lower extras...
static void compute_workdir();
*/

static void add_computed_lower(char *path, size_t len);

static void resolve_path(char **path, size_t *len);

void validate(const char *program)
{
	validate_special(program);
	user_collect();
	validate_base();
	validate_library();
	validate_lower();
	validate_user();
}

static void validate_special(const char *program)
{
	if (!io_addmod(program, S_ISUID, 0, 0))
	{
		fprintf(
			stderr,
			"Warning: unable to set special mode to \"%s\"!\n",
			program
		);
	}
}

static void validate_base()
{
	if (config_basedir == NULL)
	{
		user_require_home();
		buffer buf = buffer_new_from(user_home_size, user_home);
		buffer_write_byte(buf, PATH_SEPARATOR);
		buffer_write_data(buf, sizeof(DEFAULT_BASEAPPDIR), DEFAULT_BASEAPPDIR);
		config_basedir = buffer_reuse(buf);
	}

	if (!io_isdir(config_basedir))
	{
		user_require_caller();
		if (!io_mkdir(config_basedir, 1, user_caller_uid, user_caller_gid))
		{
			fprintf(
				stderr,
				"Fatal: unable to create library directory \"%s\"!\n",
				config_basedir
			);
			exit(1);
		}
	}

	resolve_path(&config_basedir, &config_basedir_size);
}

static void validate_library()
{
	if (config_librarydir == NULL)
	{
		config_librarydir = DEFAULT_LIBRARYDIR;
	}

	if (!io_isdir(config_librarydir))
	{
		if (!io_mkdir(config_librarydir, 0, 0, 0))
		{
			fprintf(
				stderr,
				"Fatal: unable to create library directory \"%s\"!\n",
				config_librarydir
			);
			exit(1);
		}

		chmod(config_librarydir, S_IRWXU);
	}

	resolve_path(&config_librarydir, &config_librarydir_size);
}

static void validate_lower()
{
	if (config_lowerdir == NULL)
	{
		config_lowerdir = DEFAULT_LOWERDIR;
	}

	char *pos = strchr(config_lowerdir, LIST_SEPARATOR);
	if (pos != NULL)
	{
		*pos = 0;
	}

	if (!io_isdir(config_lowerdir))
	{
		fprintf(
			stderr,
			"Fatal: lower directory \"%s\" does not exists!\n",
			config_lowerdir
		);
		exit(1);
	}

	if (pos == NULL)
	{
		config_lowerdir_size = strlen(config_lowerdir);
	}
	else
	{
		config_lowerdir_size = pos - config_lowerdir;
		for (;;)
		{
			pos++;
			if (*pos == 0)
			{
				break;
			}
			char *aux = pos;
			pos = strchr(pos, LIST_SEPARATOR);
			if (pos == NULL)
			{
				add_computed_lower(aux, strlen(aux));
				break;
			}
			else
			{
				*pos = 0;
				add_computed_lower(aux, pos - aux);
			}
		}
	}
}

static void validate_user()
{
	if (config_user == NULL)
	{
		user_require_caller();
		computed_uid = user_caller_uid;
		computed_gid = user_caller_gid;
		computed_user_read = 1;
		return;
	}
	computed_user_read = 0;
	config_user_size = strlen(config_user);
	char *pos = memchr(config_user, LIST_SEPARATOR, config_user_size);
	if (pos == NULL)
	{
		return;
	}
	char *end;
	computed_uid = strtoul(config_user, &end, 10);
	if (end != pos)
	{
		return;
	}
	computed_gid = strtoul(pos + 1, &end, 10);
	if (*end)
	{
		return;
	}
	computed_user_read = 1;
}

static void add_computed_lower(char *path, size_t len)
{
	if (
		config_lowerdir_size > 0 &&
		memcmp(config_lowerdir, path, config_lowerdir_size) != 0
	)
	{
		fprintf(
			stderr,
			"Fatal: extra lower directory \"%s\" does not belongs to "
			"base lower directory \"%s\"!\n",
			path,
			config_lowerdir
		);
		exit(1);
	}

	if (!io_isdir(path))
	{
		fprintf(
			stderr,
			"Fatal: extra lower directory \"%s\" does not exists!\n",
			path
		);
		exit(1);
	}

	computed_lowerdirs = realloc(
		computed_lowerdirs,
		(computed_lowerdirs_count + 1) * sizeof(char*)
	);
	assert(computed_lowerdirs != NULL);
	computed_lowerdirs_sizes = realloc(
		computed_lowerdirs_sizes,
		(computed_lowerdirs_count + 1) * sizeof(size_t)
	);
	assert(computed_lowerdirs_sizes != NULL);
	computed_lowerdirs[computed_lowerdirs_count] = path;
	computed_lowerdirs_sizes[computed_lowerdirs_count] = len;
	computed_lowerdirs_count++;
}

static void resolve_path(char **path, size_t *len)
{
	char *resolved = io_realpath(*path);
	if (resolved == NULL)
	{
		fprintf(
			stderr,
			"Warning: unable to resolve absolute path of \"%s\"!\n",
			*path
		);
	}
	else
	{
		*path = resolved;
	}
	*len = strlen(*path);
}

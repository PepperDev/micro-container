#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "validate.h"
#include "config.h"
#include "io.h"
#include "user.h"
#include "buffer.h"

static void validate_special(const char*);
static void validate_base();
static void validate_library();
/*static void validate_lower();
static void validate_name();
static void validate_work();
static void validate_user();
static void validate_volume();
static void validate_scripts();*/

static void resolve_path(char **path, size_t *len);

void validate(const char *program)
{
	validate_special(program);
	user_collect();
	validate_base();
	validate_library();
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

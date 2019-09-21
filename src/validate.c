#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "validate.h"
#include "config.h"
#include "io.h"

static void validate_base();
static void validate_library();
static void validate_lower();
static void validate_name();
static void validate_work();
static void validate_user();
static void validate_volume();
static void validate_scripts();

static void resolve_path(char **path, size_t *len);

void validate()
{
	validate_base();
	validate_library();
}

static void validate_base()
{
	// TODO:...
}

static void validate_library()
{
	if (config_librarydir == NULL)
	{
		config_librarydir = DEFAULT_LIBRARYDIR;
	}

	if (!io_isdir(config_librarydir))
	{
		if (!io_mkdir(config_librarydir))
		{
			fprintf(
				stderr,
				"Fatal: unable to create library directory \"%s\"!\n",
				config_librarydir
			);
			abort();
		}

		chmod(config_librarydir, S_IRWXU);
	}

	resolve_path(&config_librarydir, &config_librarydir_size);
}

static void resolve_path(char **path, size_t *len)
{
	char *resolved = io_realpath(*path);
	if (resolved != NULL) {
		*path = resolved;
	}
	*len = strlen(*path);
}

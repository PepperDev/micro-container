#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "config.h"

char config_unload = 0;

char *config_librarydir = NULL,
	*config_basedir = NULL,
	*config_lowerdir = NULL,
	*config_name = NULL,
	*config_workdir = NULL,
	*config_user = NULL,
	*config_initscript = NULL,
	*config_shutdownscript = NULL,
	**config_volumes = NULL,
	**config_command = NULL;

size_t config_librarydir_size = 0,
	config_basedir_size = 0,
	config_lowerdir_size = 0,
	config_name_size = 0,
	config_workdir_size = 0,
	config_user_size = 0,
	config_initscript_size = 0,
	config_shutdownscript_size = 0,
	config_volumes_count = 0,
	config_command_count = 0,
	*config_volumes_sizes = 0,
	*config_command_sizes = 0;


static void print_version();
static void print_help(char*);
static char parse_arg_value(char*, char, char**, char*, int*);
static char parse_arg_list(char*, char, size_t*, char***, char*, int*);
static void add_list(size_t*, char***, char*);

char config_parse(int argc, char *argv[])
{
	char moreargs = 1, name = 0;
	char *carg, *narg;
	for (int i = 1; i < argc; i++)
	{
		if (moreargs)
		{
			carg = argv[i];
			if (carg != NULL && *carg == '-')
			{
				carg++;
				if (*carg == 0 && !config_unload)
				{
					config_unload = 1;
					continue;
				}
				if (*carg == '-')
				{
					carg++;
					if (*carg == 0)
					{
						moreargs = 0;
						continue;
					}
					if (memcmp("version", carg, 8) == 0)
					{
						print_version();
						return 0;
					}
					if (memcmp("help", carg, 5) == 0)
					{
						print_help(argv[0]);
						return 0;
					}
				} else {
					narg = NULL;
					if (i + 1 < argc)
					{
						narg = argv[i + 1];
						if (narg != NULL && *narg == 0)
						{
							narg = NULL;
						}
					}
					if (parse_arg_value(carg, 'n', &config_name,
							narg, &i))
					{
						name = 1;
						continue;
					}
					if (
						parse_arg_value(carg, 'd', &config_librarydir,
							narg, &i) ||
						parse_arg_value(carg, 'b', &config_basedir,
							narg, &i) ||
						parse_arg_value(carg, 'l', &config_lowerdir,
							narg, &i) ||
						parse_arg_value(carg, 'w', &config_workdir,
							narg, &i) ||
						parse_arg_value(carg, 'u', &config_user,
							narg, &i) ||
						parse_arg_value(carg, 'i', &config_initscript,
							narg, &i) ||
						parse_arg_value(carg, 's', &config_shutdownscript,
							narg, &i) ||
						parse_arg_list(carg, 'v', &config_volumes_count,
							&config_volumes, narg, &i)
					)
					{
						continue;
					}
				}
			}
			if (name)
			{
				moreargs = 0;
			}
			else
			{
				carg = argv[i];
				if (carg != NULL && *carg != 0)
				{
					config_name = carg;
				}
				name = 1;
				continue;
			}
		}
		add_list(&config_command_count, &config_command, argv[i]);
	}
	return 1;
}


static void print_version()
{
	puts("micro-container version " VERSION "\n");
}

static void print_help(char* self)
{
	printf("Usage: %s [-] [-d librarydir] [-b basedir] "
		"[-l lowerdir] [-n name] [-w workdir] [-u user:group] "
		"[[-v hostvolume:guestvolume]...] [-i initscript] "
		"[-s shutdownscript] [[--] command args...]\n", self);
}

static char parse_arg_value(char *arg, char value, char **target, char *next,
	int *i)
{
	if (*arg != value)
	{
		return 0;
	}
	arg++;
	if (*arg == 0)
	{
		if (next == NULL)
		{
			return 0;
		}
		*target = next;
		(*i)++;
		return 1;
	}
	if (*arg == '=')
	{
		arg++;
	}
	if (*arg == 0)
	{
		return 0;
	}
	*target = arg;
	return 1;

}

static char parse_arg_list(char *arg, char value, size_t *length,
	char ***target, char *next, int *i)
{
	char *change = NULL;
	if (parse_arg_value(arg, value, &change, next, i))
	{
		add_list(length, target, change);
		return 1;
	}
	return 0;
}

static void add_list(size_t *length, char ***target, char *value)
{
	*target = realloc(*target, (*length + 1) * sizeof(char**));
	assert(*target != NULL);
	(*target)[*length] = value;
	(*length)++;
}

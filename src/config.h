#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define VERSION "0.2.0-alpha"

extern char config_unload;

extern char *config_librarydir,
	*config_basedir,
	*config_lowerdir,
	*config_name,
	*config_workdir,
	*config_user,
	*config_initscript,
	*config_shutdownscript,
	**config_volumes,
	**config_command;

extern size_t config_librarydir_size,
	config_basedir_size,
	config_lowerdir_size,
	config_name_size,
	config_workdir_size,
	config_user_size,
	config_initscript_size,
	config_shutdownscript_size,
	config_volumes_count,
	config_command_count,
	*config_volumes_sizes,
	*config_command_sizes;

char config_parse(int, char*[]);

#endif

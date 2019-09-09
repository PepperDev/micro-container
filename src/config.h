#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

extern char config_unload;

extern char *config_rootdir,
	*config_basedir,
	*config_lowerdir,
	*config_name,
	*config_workdir,
	*config_user,
	*config_initscript,
	*config_shutdownscript
	**config_volumes,
	**config_command;

extern size_t config_rootdir_size,
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

// TODO: list for volume and command


#endif

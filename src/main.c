#include "config.h"
#include "validate.h"
#include "mount.h"
#include "launch.h"
#include "unload.h"

int main(int argc, char *argv[])
{
	if (!config_parse(argc, argv))
	{
		return 0;
	}

	validate(argv[0]);

	if (!config_unload)
	{
		prepare_mounts();

		launch();
	}

	unload();

	return 0;
}

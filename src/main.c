#include "config.h"
#include "validate.h"
#include "mount.h"

int main(int argc, char *argv[])
{
	if (!config_parse(argc, argv))
	{
		return 0;
	}

	validate(argv[0]);

	prepare_mounts();

	//launch
	return 0;
}

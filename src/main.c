#include "config.h"
#include "validate.h"

int main(int argc, char *argv[])
{
    config_t config;

    if (!(validate_superuser(argc, argv)
          && config_parse(&config, argc, argv))) {
        return EXIT_FAILURE;
    }

    if (config.stop) {
        // read pidfile if process exists close it
    }
    // read pidfile if exists try spawning another instance

    return EXIT_SUCCESS;
}

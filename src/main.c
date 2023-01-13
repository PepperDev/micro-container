#include "config.h"
#include "validate.h"
#include "proc.h"

int main(int argc, char *argv[])
{
    config_t config;

    if (!(validate_superuser(argc, argv)
          && config_parse(&config, argc, argv))) {
        return EXIT_FAILURE;
    }

    if (config.stop) {
        if (killpid(config.pidfile)) {
            return EXIT_SUCCESS;
        }
        return EXIT_FAILURE;
    }
    // read pidfile if exists try spawning another instance

    return EXIT_SUCCESS;
}

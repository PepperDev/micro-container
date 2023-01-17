#include "config.h"
#include "user.h"
#include "proc.h"
#include "cage.h"
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (check_superuser(argc, argv)) {
        return EXIT_FAILURE;
    }

    config_t config;
    if (config_parse(&config, argc, argv)) {
        return EXIT_FAILURE;
    }

    if (config.stop) {
        if (killpid(config.name, config.pidfile)) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (spawn_cage(&config)) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

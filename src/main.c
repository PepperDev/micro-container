#include "super/super.h"
#include "error/error.h"
#include "config/config.h"
#include "user.h"
#include "proc.h"
#include "cage.h"
#include <stdlib.h>             // EXIT_FAILURE EXIT_SUCCESS

int main(int argc, char *argv[])
{
    if (super_parse(argc, argv)) {
        return error_log(super_fix());
    }

    const char *error = super_escalate(argc, argv);
    if (error) {
        return error_log(error);
    }

    config_t config;
    error = config_parse(&config, argc, argv);
    if (error) {
        return error_log(error);
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

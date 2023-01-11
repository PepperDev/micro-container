#include "config.h"

int main(int argc, char *argv[])
{
    config_t config;
    if (!config_parse(&config, argc, argv)) {
        return EXIT_FAILURE;
    }
    // ...

    return EXIT_SUCCESS;
}

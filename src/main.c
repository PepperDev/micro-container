#include "config.h"

int main(int argc, char *argv[])
{
    config_t config;
    config_parse(&config, argc, argv);

    // ...

    return EXIT_SUCCESS;
}

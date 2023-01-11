#include "config.h"

#include <unistd.h>

int main(int argc, char *argv[])
{
    config_t config;
    config_parse(&config, argc, argv);
    argv[1][0] = 0;
    argv[1] = NULL;
    sleep(5);

    return EXIT_SUCCESS;
}

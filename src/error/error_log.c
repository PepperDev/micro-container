#include "error.h"
#include <string.h>             // strlen
#include <unistd.h>             // write
#include <stdlib.h>             // EXIT_SUCCESS EXIT_FAILURE

int error_log(const char *error)
{
    if (error) {
        write(STDERR_FILENO, error, strlen(error));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

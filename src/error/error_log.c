#include "error.h"
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int error_log(const char *error)
{
    if (error) {
        write(STDERR_FILENO, error, strlen(error));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

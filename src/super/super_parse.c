#include "super.h"
#include <string.h>

bool super_parse(int argc, char *argv[])
{
    return argc == 2 && memcmp(argv[1], "--fix", 6) == 0;
}

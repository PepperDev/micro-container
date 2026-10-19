#include <unistd.h>
#include <stdlib.h>

void __gcov_flush(void) __attribute__((weak));

void _exit(int status)
{
  if (__gcov_flush)
    __gcov_flush();
  _Exit(status);
}

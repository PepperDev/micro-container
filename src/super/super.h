#ifndef SUPER_H
#define SUPER_H

#include <stdbool.h>

bool super_parse(int, char *[]);
const char *super_do();
const char *super_escalate(int, char *[]);

#endif

#ifndef SUPER_H
#define SUPER_H

#include <stdbool.h>            // bool

bool super_parse(int, char *[]);
const char *super_fix();
const char *super_escalate(int, char *[]);

#endif

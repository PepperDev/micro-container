#ifndef OVERLAY_H
#define OVERLAY_H

#include "config/config.h"

char *compute_overlay(config_t *, size_t, bool, size_t *, size_t *, size_t *);

int overlay_filesystem(char *, size_t, char *, size_t);

#endif

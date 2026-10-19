#ifndef CAGE_OVERLAY_H
#define CAGE_OVERLAY_H

#include "sandbox/appdir.h"

char *build_overlay_opts(const char *lower, const char *upper, const char *work);
int prepare_overlay(const char *root, const appdir_t * dirs);

#endif

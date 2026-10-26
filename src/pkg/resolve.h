#ifndef CAGE_RESOLVE_H
#define CAGE_RESOLVE_H

int resolve_module(const char *name, char **path_out, char ***dep_names, int *ndep);

#endif

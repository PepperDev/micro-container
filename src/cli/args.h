#ifndef CAGE_ARGS_H
#define CAGE_ARGS_H

// cppcheck-suppress missingIncludeSystem
#include <stddef.h>

#define CAGE_MAX_ENVVARS 256
#define CAGE_MAX_VOLUMES 256
#define CAGE_MAX_CMD_ARGS 256

typedef struct {
  char *appdir;
  int unshare_cgroup;
  char *currentdir;
  char *envvars[CAGE_MAX_ENVVARS];
  size_t envvar_count;
  int graphical;
  char *group;
  int show_help;
  char *initscript;
  int kill_mode;
  char *lowerdir;
  int unshare_net;
  char *name;
  char *pidfile;
  char *inst_env;
  char *upperdir;
  char *user;
  int show_version;
  char *volumes[CAGE_MAX_VOLUMES];
  size_t volume_count;
  char *workdir;
  int has_dashdash;
  char *command[CAGE_MAX_CMD_ARGS];
  size_t command_count;
} cage_config_t;

void cage_config_init(cage_config_t * cfg);
int cage_parse_args(cage_config_t * cfg, int argc, char **argv);
void cage_print_usage(void);
void cage_print_version(void);

#endif

#ifndef CAGE_ENV_H
#define CAGE_ENV_H

// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>
// cppcheck-suppress missingIncludeSystem
#include <stddef.h>
// cppcheck-suppress missingIncludeSystem
#include <stdint.h>

typedef struct {
  uint32_t *hashes;
  char **keys;
  char **vals;
  int *used;
  size_t cap;
  size_t len;
} env_map_t;

typedef struct {
  char *path;
  char *term;
  char *lang;
  char *home;
  char *shell;
  char *user;
  int guest_shell_override;
  char *host_xdg_runtime_dir;
  char *host_home;
  env_map_t map;
} env_t;

int parse_envs(env_t * env, char **envs, size_t envs_count);

const char *get_host_xdg_dir(char *buf, size_t size, uid_t ruid);

void env_set_gui_envs(env_t * env, const char *display, int guest_x11num, const char *host_wl, uid_t ruid);

char *env_read_pid_environ(pid_t pid);
void env_inherit_gui(env_map_t * m, const char *blob);

void env_map_free(env_map_t * m);
void env_map_init(env_map_t * m);
char **env_map_build(const env_map_t * m, size_t *out_len);
void env_map_put(env_map_t * m, const char *key, const char *val);
const char *env_map_get(const env_map_t * m, const char *key);
void env_map_ensure(env_map_t * m, const char *key, const char *val);

#endif

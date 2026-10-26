#include "launch.h"
#include "util/io.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>

static char **build_login_argv(const char *shell, char **out_argv0)
{
  const char *base = strrchr(shell, '/');
  base = base ? base + 1 : shell;

  size_t len = strlen(base) + 2;
  *out_argv0 = malloc(len);
  if (!*out_argv0)
    return NULL;
  (*out_argv0)[0] = '-';
  memcpy(*out_argv0 + 1, base, len - 1);

  char **av = malloc(3 * sizeof(char *));
  if (!av) {
    free(*out_argv0);
    *out_argv0 = NULL;
    return NULL;
  }
  av[0] = *out_argv0;
  av[1] = NULL;
  av[2] = NULL;
  return av;
}

static void fill_home_user(env_t *e, const user_t *u)
{
  env_map_ensure(&e->map, "HOME", get_home(u));
  env_map_ensure(&e->map, "USER", get_name(u));
}

static void fill_shell(env_t *e, const user_t *u)
{
  if (e->guest_shell_override)
    env_map_put(&e->map, "SHELL", get_shell(u));
}

static const char *pick_shell(const user_t *user, const env_t *env)
{
  const char *s = env_map_get(&env->map, "SHELL");
  return s ? s : get_shell(user);
}

struct launch_cmd {
  const char *cmd;
  char **argv;
  char **login_argv;
  char *login_argv0;
};

static int is_dash(char **argv)
{
  return argv && argv[0] && strcmp(argv[0], "-") == 0;
}

static int has_command(char **argv)
{
  return argv && argv[0];
}

static struct launch_cmd prepare_command(const user_t *user, const env_t *env, char **argv)
{
  struct launch_cmd c = { NULL, argv, NULL, NULL };
  if (has_command(argv) && !is_dash(argv)) {
    c.cmd = argv[0];
    return c;
  }
  const char *shell = is_dash(argv) ? pick_shell(user, env) : get_shell(user);
  c.login_argv = build_login_argv(shell, &c.login_argv0);
  if (!c.login_argv)
    return c;
  c.argv = c.login_argv;
  c.cmd = shell;
  return c;
}

static int setup_cwd(const char *cwd)
{
  if (!cwd || !*cwd)
    return 0;
  if (chdir(cwd) != 0) {
    if (io_mkdir(cwd) != 0)
      return -1;
    if (chdir(cwd) != 0)
      return -1;
  }
  return 0;
}

static int do_priv_drop(const user_t *user)
{
  gid_t gid = get_gid(user);
  if (setregid(gid, gid) != 0)
    return -1;
  group_list_t gl = get_groups(user);
  if (sys_setgroups(gl.count, gl.groups) != 0)
    return -1;
  uid_t uid = get_uid(user);
  if (setreuid(uid, uid) != 0)
    return -1;
  return 0;
}

static void exec_or_fail(const char *cmd, char **argv, char **envp)
{
  umask(0022);
  execvpe(cmd, argv, envp);
  fprintf(stderr, "cage: exec failed: %s\n", strerror(errno));
}

static int launch_exec(const user_t *user, const char *cwd, const char *cmd, char **argv, char **envp)
{
  if (setup_cwd(cwd) != 0)
    return -1;
  if (do_priv_drop(user) != 0)
    return -1;
  exec_or_fail(cmd, argv, envp);
  return -1;
}

int launch_cage(const user_t *user, env_t *env, const char *cwd, char **argv)
{
  struct launch_cmd c = prepare_command(user, env, argv);
  if (!c.cmd)
    return -1;
  fill_home_user(env, user);
  fill_shell(env, user);
  char **envp = env_map_build(&env->map, NULL);
  if (!envp) {
    free(c.login_argv0);
    free(c.login_argv);
    return -1;
  }
  int r = launch_exec(user, cwd, c.cmd, c.argv, envp);
  free(envp);
  free(c.login_argv0);
  free(c.login_argv);
  return r;
}

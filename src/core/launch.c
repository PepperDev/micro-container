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
  env_map_ensure(&e->map, "HOME", get_home((user_t *) u));
  env_map_ensure(&e->map, "USER", get_name((user_t *) u));
}

static void fill_shell(env_t *e, const user_t *u)
{
  if (e->guest_shell_override)
    env_map_put(&e->map, "SHELL", get_shell((user_t *) u));
}

static char **fill_env(const user_t *u, env_t *e)
{
  fill_home_user(e, u);
  fill_shell(e, u);
  return env_map_build(&e->map, NULL);
}

static char **build_env_array(const user_t *user, env_t *env)
{
  return fill_env(user, env);
}

static const char *shell_from_env_shell(const env_t *env)
{
  return env_map_get(&env->map, "SHELL");
}

static const char *pick_shell(const user_t *user, const env_t *env)
{
  const char *s = shell_from_env_shell(env);
  return s ? s : get_shell((user_t *) user);
}

static const char *try_dash_cmd(char **argv, const user_t *user, const env_t *env,
                                char ***login_argv, char **login_argv0)
{
  if (!argv || !argv[0] || strcmp(argv[0], "-") != 0)
    return NULL;
  const char *shell = pick_shell(user, env);
  *login_argv = build_login_argv(shell, login_argv0);
  if (!*login_argv)
    return NULL;
  return shell;
}

static const char *setup_command(const user_t *user, const env_t *env, char ***argv, char ***login_argv,
                                 char **login_argv0)
{
  const char *cmd = try_dash_cmd(*argv, user, env, login_argv, login_argv0);
  if (cmd) {
    *argv = *login_argv;
    return cmd;
  }
  if (*argv && (*argv)[0])
    return (*argv)[0];
  *login_argv = build_login_argv(get_shell((user_t *) user), login_argv0);
  if (!*login_argv)
    return NULL;
  *argv = *login_argv;
  return get_shell((user_t *) user);
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
  gid_t gid = get_gid((user_t *) user);
  if (setregid(gid, gid) != 0)
    return -1;
  group_list_t gl = get_groups((user_t *) user);
  if (sys_setgroups(gl.count, gl.groups) != 0)
    return -1;
  uid_t uid = get_uid((user_t *) user);
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
  char *la0 = NULL, **la = NULL;
  const char *command = setup_command(user, env, &argv, &la, &la0);
  if (!command)
    return -1;
  char **envp = build_env_array(user, env);
  if (!envp) {
    free(la0);
    free(la);
    return -1;
  }
  int r = launch_exec(user, cwd, command, argv, envp);
  free(envp);
  free(la0);
  free(la);
  return r;
}

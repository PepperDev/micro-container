#include "env.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

#define DEF_PATH "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
#define DEF_TERM "TERM=linux"
#define DEF_LANG "LANG=C"

static void sync_field(env_map_t *m, const char *kv)
{
  if (!kv)
    return;
  const char *eq = strchr(kv, '=');
  if (!eq || (size_t)(eq - kv) >= 128)
    return;
  char key[128];
  size_t klen = (size_t)(eq - kv);
  memcpy(key, kv, klen);
  key[klen] = '\0';
  env_map_put(m, key, eq + 1);
}

static void set_default_env(env_t *e)
{
  if (!e->path)
    e->path = DEF_PATH;
  if (!e->term) {
    const char *t = getenv("TERM");
    e->term = t ? (char *)(t - 5) : DEF_TERM;
  }
  if (!e->lang) {
    const char *l = getenv("LANG");
    e->lang = l ? (char *)(l - 5) : DEF_LANG;
  }
}

static int has_path(const char *d, size_t dl, const char *s)
{
  size_t sl = strlen(s);
  char *p = malloc(dl + sl + 1);
  if (!p)
    return 0;
  memcpy(p, d, dl);
  memcpy(p + dl, s, sl + 1);
  int ok = io_exists(p) == 0;
  free(p);
  return ok;
}

static void add_wl_vars(env_map_t *m, const char *wl)
{
  env_map_ensure(m, "WAYLAND_DISPLAY", wl ? wl : "wayland-0");
  env_map_put(m, "XDG_SESSION_TYPE", "wayland");
  env_map_put(m, "GDK_BACKEND", "wayland");
  env_map_put(m, "QT_QPA_PLATFORM", "wayland-egl");
  env_map_put(m, "SDL_VIDEODRIVER", "wayland");
  env_map_put(m, "CLUTTER_BACKEND", "wayland");
  env_map_put(m, "MOZ_ENABLE_WAYLAND", "1");
  env_map_put(m, "_JAVA_AWT_WM_NONREPARENTING", "1");
  const char *d = getenv("XDG_CURRENT_DESKTOP");
  env_map_put(m, "XDG_CURRENT_DESKTOP", d ? d : "wlroots");
}

static void detect_xdg_gui(const char *xrd, env_map_t *m, const char *host_wl)
{
  size_t xl = strlen(xrd);
  const char *wn = host_wl ? host_wl : "wayland-0";
  char wls[64];
  snprintf(wls, sizeof(wls), "/%s", wn);
  int wl = has_path(xrd, xl, wls), pa = has_path(xrd, xl, "/pulse");
  if (wl || pa)
    env_map_ensure(m, "XDG_RUNTIME_DIR", xrd);
  if (wl)
    add_wl_vars(m, wn);
}

void env_set_gui_envs(env_t *env, const char *display, int guest_x11num, const char *host_wl, uid_t ruid)
{
  if (display && display[0]) {
    env_map_ensure(&env->map, "DISPLAY", display);
  } else if (guest_x11num >= 0) {
    char s[32];
    snprintf(s, sizeof(s), ":%d.0", guest_x11num);
    env_map_ensure(&env->map, "DISPLAY", s);
  }
  if (!env->host_xdg_runtime_dir) {
    char buf[64];
    env->host_xdg_runtime_dir = mem_strdup(get_host_xdg_dir(buf, sizeof(buf), ruid));
  }
  if (env->host_xdg_runtime_dir)
    detect_xdg_gui(env->host_xdg_runtime_dir, &env->map, host_wl);
}

const char *get_host_xdg_dir(char *buf, size_t sz, uid_t ruid)
{
  const char *x = getenv("XDG_RUNTIME_DIR");
  if (x && x[0])
    return x;
  const char *su = getenv("SUDO_UID");
  if (su) {
    snprintf(buf, sz, "/run/user/%s", su);
    return buf;
  }
  snprintf(buf, sz, "/run/user/%u", ruid);
  return buf;
}

char *env_read_pid_environ(pid_t pid)
{
  char path[64];
  buf_t b = BUF_INIT;
  snprintf(path, sizeof(path), "/proc/%d/environ", (int)pid);
  if (io_read_quiet(path, &b) != 0) {
    buf_free(&b);
    return NULL;
  }
  if (buf_grow(&b, 1) != 0) {
    buf_free(&b);
    return NULL;
  }
  b.data[b.len] = '\0';
  return buf_done(&b);
}

static int key_is_gui(const char *key, size_t klen)
{
  static const char *const keys[] = {
    "DISPLAY", "XDG_RUNTIME_DIR", "WAYLAND_DISPLAY", "PULSE_SERVER", "PULSE_COOKIE",
    "XDG_SESSION_TYPE", "XDG_CURRENT_DESKTOP", "GDK_BACKEND", "QT_QPA_PLATFORM",
    "SDL_VIDEODRIVER", "CLUTTER_BACKEND", "MOZ_ENABLE_WAYLAND",
    "_JAVA_AWT_WM_NONREPARENTING",
  };
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    if (strlen(keys[i]) == klen && memcmp(keys[i], key, klen) == 0)
      return 1;
  return 0;
}

void env_inherit_gui(env_map_t *m, const char *blob)
{
  if (!blob)
    return;
  const char *p = blob;
  while (*p) {
    const char *eq = strchr(p, '=');
    if (eq && (size_t)(eq - p) < 128) {
      char key[128];
      size_t klen = (size_t)(eq - p);
      memcpy(key, p, klen);
      key[klen] = '\0';
      if (key_is_gui(key, klen))
        env_map_ensure(m, key, eq + 1);
    }
    p += strlen(p) + 1;
  }
}

static int try_any(char *e, env_t *env)
{
  const char *keys[] = { "PATH", "TERM", "LANG", "HOME", "SHELL", "USER" };
  char **f = &env->path;
  for (int i = 0; i < 6; i++) {
    int l = keys[i][4] ? 5 : 4;
    if (memcmp(e, keys[i], l) || e[l] != '=')
      continue;
    f[i] = e;
    return 1;
  }
  return 0;
}

static int parse_one_env(char *e, env_t *env)
{
  char *eq = strchr(e, '=');
  if (!eq) {
    fprintf(stderr, "cage: invalid input environment %s\n", e);
    return -1;
  }
  try_any(e, env);
  *eq = '\0';
  env_map_put(&env->map, e, eq + 1);
  *eq = '=';
  return 0;
}

static int shell_not_given(const env_t *env)
{
  return !env->shell || env->shell[sizeof("SHELL=") - 1] == '\0';
}

int parse_envs(env_t *env, char **envs, size_t cnt)
{
  memset(env, 0, sizeof(*env));
  env_map_init(&env->map);
  set_default_env(env);
  sync_field(&env->map, env->path);
  sync_field(&env->map, env->term);
  sync_field(&env->map, env->lang);
  for (size_t i = 0; i < cnt; i++) {
    if (parse_one_env(envs[i], env) != 0) {
      env_map_free(&env->map);
      return -1;
    }
  }
  env->guest_shell_override = shell_not_given(env);
  return 0;
}

void env_map_ensure(env_map_t *m, const char *k, const char *v)
{
  if (!v || !v[0] || env_map_get(m, k))
    return;
  env_map_put(m, k, v);
}

#include "prep.h"
#include "proc.h"
#include "sandbox/cgroup.h"
#include "sandbox/mount.h"
#include "util/env.h"
#include "util/io.h"
#include "util/mem.h"
// cppcheck-suppress missingIncludeSystem
#include <limits.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

const char *find_envvar(const cage_config_t *cfg, const char *key)
{
  size_t kl = strlen(key);
  for (size_t i = 0; i < cfg->envvar_count; i++) {
    const char *e = cfg->envvars[i];
    if (strncmp(e, key, kl) == 0 && e[kl] == '=') {
      const char *v = e + kl + 1;
      return v[0] ? v : NULL;
    }
  }
  return NULL;
}

int resolve_script(const char *script, const char *root, char *buf, size_t size)
{
  if (access(script, R_OK) != 0) {
    char *path = mem_path(root, script);
    if (!path)
      return -1;
    int ok = access(path, R_OK) == 0;
    mem_free(path);
    if (!ok)
      return -1;
  }
  int n = snprintf(buf, size, "%s", script);
  if (n < 0 || (size_t)n >= size)
    return -1;
  return 0;
}

static char *resolve_pidfile(const cage_config_t *cfg, char **free_out)
{
  if (cfg->pidfile) {
    *free_out = NULL;
    return cfg->pidfile;
  }
  char *pf = compute_pidfile(cfg->name, NULL);
  *free_out = pf;
  return pf;
}

int do_early_setup(const cage_config_t *cfg, struct pidfile_info *pi)
{
  pi->pidfile = resolve_pidfile(cfg, &pi->free_pidfile);
  return pi->pidfile ? 0 : -1;
}

static char *alloc_root_template(void)
{
  return mem_strdup("/tmp/cage-XXXXXX");
}

static void unprepare(char **root)
{
  if (*root) {
    io_rmtree(*root);
    mem_free(*root);
    *root = NULL;
  }
}

static int early_prep(const cage_config_t *cfg, struct prep_result *pr)
{
  if (early_setup_appdir(cfg, &pr->dirs) != 0)
    return -1;
  if (pr->cg_fs_type != 2)
    return 0;
  return cgroup_pre_unshare(pr->cg_fs_type, pr->cg_pid) != 0 ? -1 : 0;
}

int do_root_prep(const cage_config_t *cfg, struct prep_result *pr, const struct run_ctx *rc, int root_private)
{
  if (early_prep(cfg, pr) != 0)
    return -1;
  if (!(pr->root = alloc_root_template()))
    return -1;
  if (prepare_mounts(pr, cfg, rc, root_private)) {
    unprepare(&pr->root);
    return -1;
  }
  return 0;
}

#include "modprobe.h"
#include "modload.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
#include "resolve.h"
// cppcheck-suppress missingIncludeSystem
#include <ctype.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/utsname.h>

static const char *skip_to_name(const char *start, const char *end)
{
  const char *s = start;
  for (const char *q = start; q < end; q++) {
    if (*q == '/')
      s = q + 1;
  }
  return s;
}

static size_t first_word_len(const char *s, size_t len)
{
  size_t i;
  for (i = 0; i < len; i++)
    if (isspace((unsigned char)s[i]))
      break;
  return i;
}

static int name_matches(const char *s, size_t n, const char *name)
{
  for (size_t i = 0; i < n; i++) {
    char c = name[i];
    if (c == '-')
      c = '_';
    if (s[i] != c)
      return 0;
  }
  return 1;
}

// cppcheck-suppress staticFunction
int line_has_name(const char *line, size_t len, const char *name)
{
  size_t plen = first_word_len(line, len);
  const char *slash = skip_to_name(line, line + plen);
  size_t slen = (line + plen) - slash;
  size_t nlen = strlen(name);
  if (slen < nlen || !name_matches(slash, nlen, name))
    return 0;
  size_t rest = slen - nlen;
  if (rest == 0)
    return 1;
  if (rest == 3)
    return memcmp(slash + nlen, ".ko", 3) == 0;
  return 0;
}

static int scan_buf(const char *data, size_t len, const char *name)
{
  const char *p = data, *end = data + len;
  while (p < end) {
    const char *nl = memchr(p, '\n', end - p);
    size_t l = nl ? (size_t)(nl - p) : (size_t)(end - p);
    if (line_has_name(p, l, name))
      return 1;
    if (!nl)
      break;
    p = nl + 1;
  }
  return 0;
}

// cppcheck-suppress unusedFunction
int scan_modules(const char *path, const char *name)
{
  buf_t buf = BUF_INIT;
  if (io_read(path, &buf) != 0)
    return -1;
  int r = scan_buf(buf.data, buf.len, name);
  buf_free(&buf);
  return r;
}

static buf_t cached_proc = BUF_INIT;
static buf_t cached_builtin = BUF_INIT;

static void invalidate_module_cache(void)
{
  buf_free(&cached_proc);
}

static int is_loaded(const char *name)
{
  if (cached_proc.data == NULL) {
    if (io_read("/proc/modules", &cached_proc) != 0)
      return -1;
  }
  return scan_buf(cached_proc.data, cached_proc.len, name);
}

// cppcheck-suppress staticFunction
const char *__modprobe_builtin_prefix(void)
{
  return "/lib/modules";
}

weak_alias(__modprobe_builtin_prefix, modprobe_builtin_prefix);

static int is_builtin(const char *name)
{
  if (cached_builtin.data == NULL) {
    struct utsname uts;
    if (uname(&uts) != 0)
      return -1;
    const char *base = modprobe_builtin_prefix();
    char *path = mem_cat(base, strlen(base), "/", (size_t)1,
                         uts.release, strlen(uts.release),
                         "/modules.builtin", strlen("/modules.builtin"),
                         MEM_CAT_END);
    if (!path)
      return -1;
    if (io_read(path, &cached_builtin) != 0) {
      mem_free(path);
      return -1;
    }
    mem_free(path);
  }
  return scan_buf(cached_builtin.data, cached_builtin.len, name);
}

// cppcheck-suppress staticFunction
int check_module_status(const char *name)
{
  int loaded = is_loaded(name);
  if (loaded < 0)
    return -1;
  if (loaded == 1)
    return 1;
  int builtin = is_builtin(name);
  if (builtin < 0)
    return -1;
  if (builtin == 1)
    return 1;
  return 0;
}

// cppcheck-suppress staticFunction
int load_dep(const char *name)
{
  char *dep_path = NULL;
  char **sub_deps = NULL;
  int sub_n = 0;
  if (resolve_module(name, &dep_path, &sub_deps, &sub_n) != 0)
    return -1;
  int r = load_module_path(dep_path);
  mem_free(dep_path);
  for (int j = 0; j < sub_n; j++)
    mem_free(sub_deps[j]);
  mem_free(sub_deps);
  return r;
}

static int load_deps(char **deps, int ndeps, char *mod_path)
{
  for (int i = 0; i < ndeps; i++) {
    if (load_dep(deps[i]) != 0) {
      mem_free(deps[i]);
      for (int j = i + 1; j < ndeps; j++)
        mem_free(deps[j]);
      mem_free(deps);
      mem_free(mod_path);
      return -1;
    }
    mem_free(deps[i]);
  }
  mem_free(deps);
  return 0;
}

int modprobe_load(const char *name)
{
  int status = check_module_status(name);
  if (status < 0)
    return -1;
  if (status == 1)
    return 0;
  char *mod_path = NULL;
  char **deps = NULL;
  int ndeps = 0;
  if (resolve_module(name, &mod_path, &deps, &ndeps) != 0) {
    fprintf(stderr, "cage: module '%s' not found\n", name);
    return -1;
  }
  if (load_deps(deps, ndeps, mod_path) != 0)
    return -1;
  int r = load_module_path(mod_path);
  if (r == 0)
    invalidate_module_cache();
  mem_free(mod_path);
  return r;
}

// cppcheck-suppress unusedFunction
void modprobe_cache_clear_builtin(void)
{
  buf_free(&cached_builtin);
}

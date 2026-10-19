#include "resolve.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <ctype.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/utsname.h>

struct dep_line {
  const char *mod_path;
  size_t mod_len;
  const char *deps_str;
  size_t deps_len;
};

static int parse_dep_line(const char *line, size_t len, struct dep_line *d)
{
  const char *colon = memchr(line, ':', len);
  if (!colon)
    return -1;
  d->mod_path = line;
  d->mod_len = (size_t)(colon - line);
  d->deps_str = colon + 1;
  d->deps_len = len - d->mod_len - 1;
  return 0;
}

static const char *skip_to_name(const char *start, const char *end)
{
  const char *s = start;
  for (const char *q = start; q < end; q++) {
    if (*q == '/')
      s = q + 1;
  }
  return s;
}

static int valid_ko_suffix(const char *s, size_t after)
{
  if (after == 0)
    return 1;
  if (after == 3)
    return memcmp(s, ".gz", 3) == 0 || memcmp(s, ".xz", 3) == 0;
  if (after == 4)
    return memcmp(s, ".zst", 4) == 0;
  return 0;
}

static int name_matches(const char *name, const char *s, size_t slen)
{
  size_t nlen = strlen(name);
  if (slen < nlen + 3 || memcmp(s + nlen, ".ko", 3) != 0)
    return 0;
  if (memcmp(s, name, nlen) != 0)
    return 0;
  return valid_ko_suffix(s + nlen + 3, slen - nlen - 3);
}

static int find_in_deps(const char *line, size_t len, const char *name)
{
  struct dep_line d;
  if (parse_dep_line(line, len, &d) != 0)
    return 0;
  if (d.mod_len == 0)
    return 0;
  const char *slash = skip_to_name(d.mod_path, d.mod_path + d.mod_len);
  return name_matches(name, slash, d.mod_path + d.mod_len - slash);
}

static const char *skip_ws(const char *p, const char *end)
{
  while (p < end && isspace((unsigned char)*p))
    p++;
  return p;
}

static const char *skip_nws(const char *p, const char *end)
{
  while (p < end && !isspace((unsigned char)*p))
    p++;
  return p;
}

static void free_deps(char **deps, int nd)
{
  for (int i = 0; i < nd; i++)
    mem_free(deps[i]);
  mem_free(deps);
}

static int add_dep_name(const char *start, size_t len, char ***deps, int *nd)
{
  char *dep = mem_alloc(len + 1);
  if (!dep)
    return -1;
  memcpy(dep, start, len);
  dep[len] = '\0';
  char **t = mem_realloc(*deps, (size_t)(*nd + 1) * sizeof(**deps));
  if (!t) {
    mem_free(dep);
    return -1;
  }
  *deps = t;
  (*deps)[(*nd)++] = dep;
  return 1;
}

static int try_add_dep(const char *start, const char *end, char ***deps, int *nd)
{
  size_t ssz = end - start;
  if (ssz <= 3)
    return 0;
  size_t ko_at = ssz;
  while (ko_at >= 3 && memcmp(start + ko_at - 3, ".ko", 3) != 0)
    ko_at--;
  if (ko_at < 3)
    return 0;
  if (!valid_ko_suffix(start + ko_at, ssz - ko_at))
    return 0;
  const char *sl = skip_to_name(start, start + ko_at - 3);
  size_t slen = (start + ko_at - 3) - sl;
  return add_dep_name(sl, slen, deps, nd);
}

static int extract_deps(const char *str, size_t len, char ***out_deps, int *out_nd)
{
  const char *dp = str, *de = str + len;
  char **deps = NULL;
  int nd = 0;
  while (dp < de) {
    dp = skip_ws(dp, de);
    if (dp >= de)
      break;
    const char *start = dp;
    dp = skip_nws(dp, de);
    if (try_add_dep(start, dp, &deps, &nd) < 0) {
      free_deps(deps, nd);
      *out_deps = NULL;
      *out_nd = 0;
      return -1;
    }
  }
  *out_deps = deps;
  *out_nd = nd;
  return 0;
}

static buf_t cached_deps = BUF_INIT;

// cppcheck-suppress unusedFunction
void resolve_cache_clear(void)
{
  buf_free(&cached_deps);
}

// cppcheck-suppress staticFunction
const char *__resolve_mod_dir(void)
{
  return "/lib/modules";
}

weak_alias(__resolve_mod_dir, resolve_mod_dir);

// cppcheck-suppress staticFunction
const char *__resolve_build_prefix(void)
{
  return "/lib/modules";
}

weak_alias(__resolve_build_prefix, resolve_build_prefix);

static const buf_t *read_deps(struct utsname *uts)
{
  if (uname(uts) != 0)
    return NULL;
  if (cached_deps.data != NULL)
    return &cached_deps;
  const char *base = resolve_mod_dir();
  char *path = mem_cat(base, strlen(base), "/", (size_t)1,
                       uts->release, strlen(uts->release),
                       "/modules.dep", strlen("/modules.dep"), MEM_CAT_END);
  if (!path)
    return NULL;
  if (io_read(path, &cached_deps) != 0) {
    mem_free(path);
    return NULL;
  }
  mem_free(path);
  return &cached_deps;
}

static int build_mod_path(const struct dep_line *d, const char *release, char **out)
{
  const char *base = resolve_build_prefix();
  char *full = mem_cat(base, strlen(base), "/", 1,
                       release, strlen(release), "/", 1, MEM_CAT_END);
  if (!full)
    return -1;
  size_t flen = strlen(full);
  char *tmp = mem_realloc(full, flen + d->mod_len + 1);
  if (!tmp) {
    mem_free(full);
    return -1;
  }
  memcpy(tmp + flen, d->mod_path, d->mod_len);
  tmp[flen + d->mod_len] = '\0';
  *out = tmp;
  return 0;
}

static int process_dep_line(const struct dep_line *d, const char *release, char **path_out, char ***deps, int *nd)
{
  if (build_mod_path(d, release, path_out) != 0)
    return -1;
  if (extract_deps(d->deps_str, d->deps_len, deps, nd) != 0) {
    mem_free(*path_out);
    *path_out = NULL;
    return -1;
  }
  return 0;
}

static int find_in_buf(const buf_t *buf, const char *name, struct dep_line *d)
{
  const char *p = buf->data, *end = p + buf->len;
  while (p < end) {
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
    if (find_in_deps(p, len, name))
      return parse_dep_line(p, len, d);
    if (!nl)
      return -1;
    p = nl + 1;
  }
  return -1;
}

int resolve_module(const char *name, char **path_out, char ***dep_names, int *ndep)
{
  struct utsname uts;
  const buf_t *buf = read_deps(&uts);
  if (!buf)
    return -1;
  struct dep_line d;
  char **deps = NULL;
  int nd = 0;
  *path_out = NULL;
  if (find_in_buf(buf, name, &d) == 0)
    process_dep_line(&d, uts.release, path_out, &deps, &nd);
  if (!*path_out) {
    mem_free(deps);
    return -1;
  }
  *dep_names = deps;
  *ndep = nd;
  return 0;
}

#include "setup.h"

#include "cli/args.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>

#define RESOLV_MAX (64 * 1024)

static int val_is_local_ip4(const char *v, size_t n)
{
  return n >= 4 && strncmp(v, "127.", 4) == 0;
}

static int val_is_local_ip6(const char *v, size_t n)
{
  return n >= 3 && strncmp(v, "::1", 3) == 0;
}

static int val_is_localhost_name(const char *v, size_t n)
{
  return n >= 9 && strncmp(v, "localhost", 9) == 0;
}

static int val_is_empty(const char *v, size_t n)
{
  for (size_t i = 0; i < n; i++)
    if (v[i] != ' ' && v[i] != '\t')
      return 0;
  return 1;
}

static int val_is_localhost(const char *v, size_t n)
{
  return val_is_local_ip4(v, n) || val_is_local_ip6(v, n) || val_is_localhost_name(v, n) || val_is_empty(v, n);
}

static int line_is_localhost_ns(const char *p, size_t len)
{
  if (len < 11 || strncmp(p, "nameserver ", 11) != 0)
    return 0;
  return val_is_localhost(p + 11, len - 11);
}

static int line_has_real_ns(const char *p, size_t len)
{
  return len >= 11 && strncmp(p, "nameserver ", 11) == 0 && !line_is_localhost_ns(p, len);
}

// cppcheck-suppress staticFunction
int has_real_ns_in_content(const char *p)
{
  while (p && *p) {
    const char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    if (line_has_real_ns(p, len))
      return 1;
    if (!nl)
      break;
    p = nl + 1;
  }
  return 0;
}

// cppcheck-suppress staticFunction
int __needs_default_dns(void)
{
  buf_t b = BUF_INIT;
  if (io_read_cap("/etc/resolv.conf", &b, RESOLV_MAX) != 0)
    return 1;
  int r = !has_real_ns_in_content(b.data);
  buf_free(&b);
  return r;
}

weak_alias(__needs_default_dns, needs_default_dns);

static int setup_resolv_write_dns(int fd)
{
  const char *c = "nameserver 8.8.8.8\nnameserver 1.1.1.1\n";
  ssize_t len = (ssize_t) strlen(c);
  return write(fd, c, (size_t)len) == len ? 0 : -1;
}

static int setup_resolv_file_bind(const char *target)
{
  char tmp[] = "/tmp/cage-dns-XXXXXX";
  int fd = mkstemp(tmp);
  if (fd < 0)
    return -1;
  int ok = fchmod(fd, 0644) == 0 && setup_resolv_write_dns(fd) == 0;
  if (close(fd) != 0)
    ok = 0;
  if (!ok || sys_mount(tmp, target, NULL, MS_BIND, NULL) != 0) {
    unlink(tmp);
    return -1;
  }
  unlink(tmp);
  return 0;
}

static int setup_resolv_bind(const char *path)
{
  char src[4096];
  if (!realpath("/etc/resolv.conf", src))
    return -1;
  if (io_touch(path) != 0)
    return -1;
  if (sys_mount(src, path, NULL, MS_BIND, NULL) != 0) {
    fprintf(stderr, "cage: failed to mount resolv.conf\n");
    return -1;
  }
  return 0;
}

static int handle_nonreg_resolv(const char *resolv)
{
  if (io_unlink(resolv) != 0)
    return -1;
  if (io_touch(resolv) != 0)
    return -1;
  return 1;
}

static int resolv_has_content(const char *resolv)
{
  buf_t b = BUF_INIT;
  if (io_read_cap(resolv, &b, RESOLV_MAX) != 0) {
    buf_free(&b);
    return -1;
  }
  int has = has_real_ns_in_content(b.data);
  buf_free(&b);
  return has;
}

static int resolv_existing_regular(const char *resolv, const struct stat *st, const cage_config_t *cfg)
{
  if (st->st_size == 0)
    return 1;
  if (!cfg->unshare_net)
    return 0;
  int has = resolv_has_content(resolv);
  if (has < 0)
    return -1;
  if (has > 0)
    return 0;
  return 1;
}

static int create_resolv_file(const char *resolv)
{
  char *p = strdup(resolv);
  if (!p)
    return -1;
  char *slash = strrchr(p, '/');
  if (slash) {
    *slash = '\0';
    if (io_exists(p) == 1 && io_mkdir(p) != 0) {
      free(p);
      return -1;
    }
  }
  free(p);
  return io_touch(resolv) != 0 ? -1 : 1;
}

static int check_or_create_resolv(const char *resolv, const cage_config_t *cfg)
{
  struct stat st;
  if (lstat(resolv, &st) != 0) {
    if (errno == ENOENT)
      return create_resolv_file(resolv);
    return -1;
  }
  if (!S_ISREG(st.st_mode))
    return handle_nonreg_resolv(resolv);
  return resolv_existing_regular(resolv, &st, cfg);
}

static int do_resolv_replace(const char *resolv, const cage_config_t *cfg)
{
  if (cfg->unshare_net && needs_default_dns())
    return setup_resolv_file_bind(resolv);
  return setup_resolv_bind(resolv);
}

/* cppcheck-suppress staticFunction -- tests call it directly */
int setup_resolv(const char *root, const cage_config_t *cfg)
{
  char *resolv = mem_path(root, "etc/resolv.conf");
  if (!resolv)
    return -1;

  int r = check_or_create_resolv(resolv, cfg);
  if (r < 0) {
    mem_free(resolv);
    return -1;
  }
  if (r == 0) {
    mem_free(resolv);
    return 0;
  }

  r = do_resolv_replace(resolv, cfg);
  mem_free(resolv);
  return r;
}

// cppcheck-suppress staticFunction
int setup_run_dirs(const char *root)
{
  char *lock = mem_path(root, "run/lock");
  if (!lock)
    return -1;
  if (io_mkdtmp(lock, 1) != 0) {
    mem_free(lock);
    return -1;
  }
  mem_free(lock);
  char *user = mem_path(root, "run/user");
  if (!user)
    return -1;
  int r = io_mkdtmp(user, 0);
  mem_free(user);
  return r;
}

static int setup_devshm_symlink(const char *root)
{
  char *sl = mem_path(root, "run/shm");
  if (!sl)
    return -1;
  if (io_symlink("/dev/shm", sl) != 0) {
    mem_free(sl);
    return -1;
  }
  mem_free(sl);
  char *ds = mem_path(root, "dev/shm");
  if (!ds)
    return -1;
  if (io_mkdir(ds) != 0)
    goto fail;
  if (sys_mount("tmpfs", ds, "tmpfs", 0, NULL) != 0) {
    fprintf(stderr, "cage: mount dev/shm failed\n");
    goto fail;
  }
  mem_free(ds);
  return 0;
 fail:
  mem_free(ds);
  return -1;
}

static int setup_devshm_dir(const char *root)
{
  char *sd = mem_path(root, "run/shm");
  if (!sd)
    return -1;
  if (io_mkdtmp(sd, 1) != 0) {
    mem_free(sd);
    return -1;
  }
  mem_free(sd);
  return 0;
}

// cppcheck-suppress staticFunction
int setup_devshm(const char *root)
{
  int r = io_islink("/dev/shm");
  if (r < 0)
    return -1;
  if (r == 0)
    return setup_devshm_dir(root);
  return setup_devshm_symlink(root);
}

int mount_misc(const char *root, const cage_config_t *cfg)
{
  if (setup_resolv(root, cfg) != 0)
    return -1;
  if (setup_run_dirs(root) != 0)
    return -1;
  if (setup_devshm(root) != 0)
    return -1;
  return 0;
}

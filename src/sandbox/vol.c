#include "vol.h"
#include "util/io.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>

static int set_target(vol_t *v, const char *spec, const char *colon)
{
  if (colon) {
    size_t target_len = strlen(colon + 1);
    if (target_len == 0 || target_len >= sizeof(v->target))
      return -1;
    memcpy(v->target, colon + 1, target_len + 1);
  } else {
    size_t host_len = strlen(spec);
    if (host_len >= sizeof(v->target))
      return -1;
    memcpy(v->target, spec, host_len + 1);
  }
  return 0;
}

int parse_vol(vol_t *v, const char *spec)
{
  memset(v, 0, sizeof(*v));
  const char *colon = strchr(spec, ':');
  if (set_target(v, spec, colon) != 0)
    return -1;
  size_t host_len = colon ? (size_t)(colon - spec) : strlen(spec);
  if (host_len == 0 || host_len >= sizeof(v->host))
    return -1;
  memcpy(v->host, spec, host_len);
  v->host[host_len] = '\0';
  return 0;
}

static int ensure_host(const char *path)
{
  struct stat st;
  if (stat(path, &st) == 0)
    return 0;
  return io_mkdir_own(path, 0, 0, 0755);
}

static int create_guest_dir(const char *guest)
{
  return io_mkdir(guest);
}

static int create_guest_file(const char *guest)
{
  char *p = strrchr(guest, '/');
  if (p && p != guest) {
    *p = '\0';
    if (io_mkdir_own(guest, 0, 0, 0755) != 0) {
      *p = '/';
      return -1;
    }
    *p = '/';
  }
  return io_touch(guest);
}

static int mount_bind_private(const char *src, const char *dst)
{
  unsigned long flags = MS_BIND | MS_REC;
  if (sys_mount(src, dst, NULL, flags, NULL) != 0) {
    fprintf(stderr, "cage: bind mount failed\n");
    return -1;
  }
  if (sys_mount(NULL, dst, NULL, MS_PRIVATE, NULL) != 0) {
    fprintf(stderr, "cage: bind mount private failed\n");
    return -1;
  }
  return 0;
}

static int create_guest(const char *guest, int is_dir)
{
  if (is_dir == 0)
    return create_guest_dir(guest);
  if (is_dir == 1)
    return create_guest_file(guest);
  return -1;
}

int mount_user_volume(const vol_t *v, const char *root)
{
  char *guest = mem_path(root, v->target);
  if (!guest)
    return -1;
  if (ensure_host(v->host) != 0)
    goto fail;
  if (create_guest(guest, io_isdir(v->host)) != 0)
    goto fail;
  if (mount_bind_private(v->host, guest) != 0)
    goto fail;
  mem_free(guest);
  return 0;
 fail:
  mem_free(guest);
  return -1;
}

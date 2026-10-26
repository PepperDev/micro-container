#include "loop.h"
#include "pkg/modprobe.h"
#include "util/io.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/mount.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/loop.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

// cppcheck-suppress staticFunction
int __loop_open_control(void)
{
  int e = sys_access("/dev/loop-control", F_OK);
  if (e != 0) {
    if (errno != ENOENT && errno != EACCES)
      return -1;
    if (modprobe_load("loop") != 0)
      return -1;
  }
  return sys_open_loop_control();
}

weak_alias(__loop_open_control, loop_open_control);

// cppcheck-suppress staticFunction
int loop_control_alloc(void)
{
  int fd = loop_open_control();
  if (fd < 0)
    return -1;
  int id = sys_loop_get_free(fd);
  if (id >= 0) {
    close(fd);
    return id;
  }
  if (errno != ENOSPC) {
    id = sys_loop_add(fd);
    if (id >= 0) {
      close(fd);
      return id;
    }
  }
  close(fd);
  return -1;
}

// cppcheck-suppress staticFunction
int try_open_loop_dev(int n)
{
  return sys_open_loop_dev(n);
}

// cppcheck-suppress staticFunction
int scan_loop_devices(void)
{
  for (int n = 0; n < 256; n++) {
    int fd = try_open_loop_dev(n);
    if (fd < 0)
      continue;
    if (sys_loop_get_stat(fd) < 0 && errno == ENXIO) {
      close(fd);
      return n;
    }
    close(fd);
  }
  return -1;
}

/* cppcheck-suppress staticFunction */
int find_free_loop(void)
{
  int id = loop_control_alloc();
  if (id >= 0)
    return id;
  id = scan_loop_devices();
  if (id < 0)
    fprintf(stderr, "cage: unable to allocate loop device (max devices exhausted)\n");
  return id;
}

static int loop_set_autoclear(int fd)
{
  struct loop_info64 li;
  memset(&li, 0, sizeof(li));
  li.lo_flags = LO_FLAGS_AUTOCLEAR;
  int r = sys_loop_set_status64(fd, &li);
  if (r != 0)
    sys_loop_clr_fd(fd);
  return r;
}

static int open_rw(const char *path)
{
  int fd = open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    fprintf(stderr, "cage: unable to open %s\n", path);
  return fd;
}

/*
 * Attach `file` to a free loop device and return the open loop device fd.
 * The caller must keep the fd open until the device is mounted, then close it:
 * LO_FLAGS_AUTOCLEAR detaches the device when the last user (the mount or this
 * fd) releases it, so closing before mount loses the device.
 */
int io_loop(char *dev, size_t devsz, const char *file)
{
  int id = find_free_loop();
  if (id < 0)
    return -1;
  snprintf(dev, devsz, "/dev/loop%d", id);
  int img_fd = open_rw(file);
  if (img_fd < 0)
    return -1;
  int loop_fd = sys_open_loop_dev(id);
  if (loop_fd < 0) {
    close(img_fd);
    return -1;
  }
  int err = sys_loop_set_fd(loop_fd, img_fd);
  close(img_fd);
  if (!err)
    err = loop_set_autoclear(loop_fd);
  if (err) {
    fprintf(stderr, "cage: loop set fd failed\n");
    close(loop_fd);
    return -1;
  }
  return loop_fd;
}

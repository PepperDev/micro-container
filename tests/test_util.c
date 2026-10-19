#include "test_util.h"
#include "util/io.h"
#include "util/mem.h"
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/types.h>

int io_rmdir(const char *path)
{
  if (rmdir(path) != 0) {
    fprintf(stderr, "cage: unable to rmdir %s\n", path);
    return -1;
  }
  return 0;
}

int io_truncate(const char *path, off_t size)
{
  int fd = open(path, O_WRONLY | O_NONBLOCK | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0) {
    fprintf(stderr, "cage: unable to open %s\n", path);
    return -1;
  }
  if (ftruncate(fd, size) != 0) {
    fprintf(stderr, "cage: unable to truncate %s\n", path);
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

int test_rmtree(const char *path)
{
  struct stat sb;
  if (lstat(path, &sb) == 0 && S_ISLNK(sb.st_mode)) {
    if (unlink(path) != 0 && errno != ENOENT) {
      fprintf(stderr, "cage: cannot remove %s\n", path);
      return -1;
    }
    return 0;
  }
  DIR *d = opendir(path);
  if (!d) {
    if (unlink(path) == 0 || errno == ENOENT)
      return 0;
    fprintf(stderr, "cage: cannot remove %s\n", path);
    return -1;
  }
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
      continue;
    char *child = mem_path(path, e->d_name);
    if (!child) {
      closedir(d);
      return -1;
    }
    int r = test_rmtree(child);
    mem_free(child);
    if (r != 0) {
      closedir(d);
      return -1;
    }
  }
  closedir(d);
  if (rmdir(path) != 0 && errno != ENOENT) {
    fprintf(stderr, "cage: cannot rmdir %s\n", path);
    return -1;
  }
  return 0;
}

char *test_temp_root(void)
{
  char *tmpl = mem_path("/tmp", "cage-test.XXXXXX");
  if (!tmpl)
    return NULL;
  char *d = mkdtemp(tmpl);
  if (!d) {
    mem_free(tmpl);
    return NULL;
  }
  return d;
}

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

/* Production has no truncate-to-size helper; keep this thin wrapper local. */
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

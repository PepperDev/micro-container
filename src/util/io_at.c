#include "io.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/stat.h>

struct comp {
  char name[256];
};

struct owner {
  uid_t uid;
  gid_t gid;
};

static int next_comp(const char **p, struct comp *c)
{
  while (**p == '/')
    (*p)++;
  if (**p == '\0')
    return 0;
  const char *start = *p;
  while (**p && **p != '/')
    (*p)++;
  size_t len = (size_t)(*p - start);
  if (len >= sizeof(c->name))
    return -1;
  memcpy(c->name, start, len);
  c->name[len] = '\0';
  return 1;
}

static int open_component_nofollow(int dirfd, const char *name)
{
  return sys_openat(dirfd, name, O_PATH | O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC, 0);
}

static int create_dir_component(int dirfd, const char *name, struct owner o)
{
  if (sys_mkdirat(dirfd, name, 0755) != 0) {
    fprintf(stderr, "cage: unable to mkdir %s\n", name);
    return -1;
  }
  if ((o.uid != 0 || o.gid != 0) && sys_fchownat(dirfd, name, o.uid, o.gid, AT_SYMLINK_NOFOLLOW) != 0) {
    fprintf(stderr, "cage: unable to chown %s\n", name);
    return -1;
  }
  return 0;
}

static int ensure_dir_component(int dirfd, const char *name, struct owner o)
{
  struct stat st;
  if (sys_fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
    if (!S_ISDIR(st.st_mode)) {
      fprintf(stderr, "cage: refusing to use non-directory %s\n", name);
      return -1;
    }
  } else if (create_dir_component(dirfd, name, o) != 0) {
    return -1;
  }
  return open_component_nofollow(dirfd, name);
}

static int no_more_components(const char *p)
{
  const char *save = p;
  struct comp nxt;
  return next_comp(&save, &nxt) == 0;
}

static int descend_components(int rootfd, const char *rel, struct owner o, char *leaf)
{
  const char *p = rel;
  int cur = rootfd;
  struct comp c;
  int rc;
  while ((rc = next_comp(&p, &c)) > 0) {
    if (no_more_components(p)) {
      snprintf(leaf, 256, "%s", c.name);
      return cur;
    }
    int fd = ensure_dir_component(cur, c.name, o);
    if (fd < 0)
      break;
    if (cur != rootfd)
      close(cur);
    cur = fd;
  }
  if (cur != rootfd)
    close(cur);
  return -1;
}

static void release_parent(int parent, int rootfd)
{
  if (parent != rootfd)
    close(parent);
}

static int chown_leaf(int parent, const char *leaf, uid_t uid, gid_t gid)
{
  if (uid == 0 && gid == 0)
    return 0;
  if (sys_fchownat(parent, leaf, uid, gid, AT_SYMLINK_NOFOLLOW) != 0) {
    fprintf(stderr, "cage: unable to chown %s\n", leaf);
    return -1;
  }
  return 0;
}

static int mkdir_leaf(int parent, const char *leaf, struct owner o, mode_t mode)
{
  if (sys_mkdirat(parent, leaf, mode) != 0 && errno != EEXIST) {
    fprintf(stderr, "cage: unable to mkdir %s\n", leaf);
    return -1;
  }
  return chown_leaf(parent, leaf, o.uid, o.gid);
}

int io_mkdir_own_at(int rootfd, const char *rel, uid_t uid, gid_t gid, mode_t mode)
{
  struct owner o = { uid, gid };
  char leaf[256];
  int parent = descend_components(rootfd, rel, o, leaf);
  if (parent < 0)
    return -1;
  int r = mkdir_leaf(parent, leaf, o, mode);
  release_parent(parent, rootfd);
  return r;
}

int io_mkdir_parents_at(int rootfd, const char *rel, uid_t uid, gid_t gid, mode_t mode)
{
  struct owner parents = { 0, 0 };
  struct owner leaf_owner = { uid, gid };
  char leaf[256];
  int parent = descend_components(rootfd, rel, parents, leaf);
  if (parent < 0)
    return -1;
  int r = mkdir_leaf(parent, leaf, leaf_owner, mode);
  release_parent(parent, rootfd);
  return r;
}

int io_touch_at(int rootfd, const char *rel, uid_t uid, gid_t gid)
{
  struct owner o = { uid, gid };
  char leaf[256];
  int parent = descend_components(rootfd, rel, o, leaf);
  if (parent < 0)
    return -1;
  int fd = sys_openat(parent, leaf, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
  if (fd < 0) {
    fprintf(stderr, "cage: unable to create %s\n", leaf);
    release_parent(parent, rootfd);
    return -1;
  }
  close(fd);
  int r = chown_leaf(parent, leaf, uid, gid);
  release_parent(parent, rootfd);
  return r;
}

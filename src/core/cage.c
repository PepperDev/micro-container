#include "cage.h"
#include "prep.h"
#include "cli/args.h"
#include "proc.h"
#include "util/io.h"
#include "util/mem.h"
#include "util/syscall.h"
#include "net/net.h"
#include "net/netlink.h"
#include "pkg/modprobe.h"
#include "sandbox/cgroup.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <signal.h>
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/mman.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/wait.h>
// cppcheck-suppress missingIncludeSystem
#include <time.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

static void nl_send_msg(int fd, void *buf)
{
  sys_netlink_send(fd, buf, ((struct nlmsghdr *)buf)->nlmsg_len);
}

static void parent_nl_addr6(int fd, int net_id, int host_idx)
{
  char mbuf[64];
  struct net6_addr host6;
  net6_addr(net_id, 1, &host6);
  if (netlink_fill_addr6(mbuf, sizeof(mbuf), host_idx, NET6_PREFIXLEN, &host6) == 0)
    nl_send_msg(fd, mbuf);
}

static void parent_nl_cmds(int fd, int net_id, int nsfd)
{
  char mbuf[256];
  char host[32];
  int host_idx;
  snprintf(host, sizeof(host), "cage-veth%d", net_id);
  if (netlink_create_veth(fd, host, "eth0", nsfd, &host_idx) < 0)
    return;
  if (netlink_fill_addr(mbuf, sizeof(mbuf), host_idx, 31, NET_HOST_IP(net_id)) == 0)
    nl_send_msg(fd, mbuf);
  if (netlink_fill_up(mbuf, sizeof(mbuf), host_idx) == 0)
    nl_send_msg(fd, mbuf);
  parent_nl_addr6(fd, net_id, host_idx);
}

static void do_parent_net(pid_t child, int net_id, volatile int *net_futex)
{
  char buf[64];
  if (*net_futex != 1)
    sys_futex_wait(net_futex, 0);
  snprintf(buf, sizeof(buf), "/proc/%d/ns/net", (int)child);
  int fd, nsfd = open(buf, O_RDONLY | O_CLOEXEC);
  if (nsfd >= 0) {
    if (sys_netlink_open(&fd) == 0) {
      parent_nl_cmds(fd, net_id, nsfd);
      sys_netlink_close(fd);
    }
    close(nsfd);
  }
  *net_futex = 2;
  sys_futex_wake(net_futex, 1);
}

static volatile int *create_futex(void)
{
  volatile int *fut = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (fut == MAP_FAILED)
    return NULL;
  *fut = 0;
  return fut;
}

static volatile int *setup_net_links(int *net_id)
{
  int nfd;
  if (sys_netlink_open(&nfd) < 0)
    return NULL;
  int r = netlink_alloc_veth_id(nfd, "cage-veth", net_id);
  sys_netlink_close(nfd);
  if (r < 0)
    return NULL;
  if (net_enable_ip_forward() < 0 || net_setup_nat() < 0)
    return NULL;
  net_enable_ip6_forward();
  net_setup_nat6();
  return create_futex();
}

static void unlink_pidfile(const char *pidfile)
{
  io_unlink_quiet(pidfile);
}

static int setup_net_or_fail(const cage_config_t *cfg, volatile int **net_futex, int *net_id)
{
  if (!cfg->unshare_net)
    return 0;
  if (modprobe_load("veth") != 0)
    return -1;
  *net_futex = setup_net_links(net_id);
  return *net_futex ? 0 : -1;
}

static void free_futex(volatile int *fut)
{
  if (fut)
    munmap((void *)fut, 4096);
}

struct spawn_st {
  env_t env;
  user_t *user;
  const char *pidfile;
  char *free_pidfile, *root;
  pid_t child;
  int pidfd, net_id, cg_fs_type, cgroup_root_private;
  pid_t cg_pid;
  volatile int *net_futex;
  volatile int *root_futex;
};

static void spawn_st_init(struct spawn_st *st)
{
  memset(&st->env, 0, sizeof(st->env));
  st->user = NULL;
  st->net_id = -1;
  st->pidfd = -1;
  st->child = -1;
  st->root = NULL;
  st->net_futex = NULL;
  st->root_futex = NULL;
  st->cg_fs_type = 0;
  st->cg_pid = 0;
  st->cgroup_root_private = 0;
}

static int setup_cgroup(const cage_config_t *cfg, struct spawn_st *st)
{
  if (!cfg->unshare_cgroup)
    return 0;
  st->cg_fs_type = cgroup_get_type();
  if (st->cg_fs_type < 0)
    return -1;
  st->cg_pid = getpid();
  st->cgroup_root_private = 0;
  if (st->cg_fs_type == 2 && cgroup_detect_mount(&st->cgroup_root_private) != 0)
    return -1;
  return 0;
}

static int do_setup_prep(const cage_config_t *cfg, struct spawn_st *st, uid_t ruid)
{
  st->root_futex = create_futex();
  if (!st->root_futex)
    return -1;
  struct run_ctx rc = { st->user, &st->env, st->net_futex, st->root_futex, st->net_id, ruid, st->cg_fs_type };
  struct prep_result pr;
  pr.cg_fs_type = st->cg_fs_type;
  pr.cg_pid = st->cg_pid;
  if (do_root_prep(cfg, &pr, &rc, st->cgroup_root_private) < 0)
    return -1;
  st->root = pr.root;
  st->child = pr.child;
  return 0;
}

static void remove_root(char **root)
{
  if (*root) {
    if (rmdir(*root) != 0)
      io_rmtree(*root);
    mem_free(*root);
    *root = NULL;
  }
}

static int wait_root_ready(volatile int *ready, pid_t child)
{
  for (;;) {
    if (*ready == 1)
      return 0;
    siginfo_t si;
    memset(&si, 0, sizeof(si));
    if (waitid(P_PID, (id_t) child, &si, WEXITED | WNOHANG | WNOWAIT) == 0 && si.si_pid == child)
      return -1;
    struct timespec ts = { 0, 20 * 1000 * 1000 };
    sys_futex_wait_to(ready, 0, &ts);
  }
}

static int finish_child(struct spawn_st *st)
{
  int status;
  pidwait(st->child, &status);
  unlink_pidfile(st->pidfile);
  cgroup_cleanup(st->cg_pid, st->cg_fs_type);
  user_free(st->user);
  remove_root(&st->root);
  return exit_code_from_status(status);
}

static void spawn_cleanup(struct spawn_st *st)
{
  free_futex(st->net_futex);
  free_futex(st->root_futex);
  if (st->child >= 0) {
    kill(st->child, SIGTERM);
    pidwait(st->child, NULL);
  }
  if (st->pidfd >= 0)
    close_pid(st->pidfd);
  unlink_pidfile(st->pidfile);
  cgroup_cleanup(st->cg_pid, st->cg_fs_type);
  user_free(st->user);
  remove_root(&st->root);
  mem_free(st->free_pidfile);
  mem_free(st->env.host_xdg_runtime_dir);
}

static int do_setup(const cage_config_t *cfg, struct spawn_st *st, uid_t ruid)
{
  if (setup_net_or_fail(cfg, &st->net_futex, &st->net_id) < 0)
    return -1;
  if (setup_cgroup(cfg, st) < 0)
    return -1;
  return do_setup_prep(cfg, st, ruid);
}

static int spawn_init(cage_config_t *cfg, struct spawn_st *st, uid_t ruid)
{
  spawn_st_init(st);
  struct pidfile_info pi;
  if (do_early_setup(cfg, &pi))
    return -1;
  st->pidfile = pi.pidfile;
  st->free_pidfile = pi.free_pidfile;
  st->pidfd = create_pidfile(st->pidfile);
  if (st->pidfd < 0)
    return -1;
  if (do_setup(cfg, st, ruid) != 0)
    goto fail;
  return 0;
 fail:
  spawn_cleanup(st);
  return -1;
}

static int spawn_exec(struct spawn_st *st)
{
  dprintf(st->pidfd, "%d", st->child);
  close_pid(st->pidfd);
  st->pidfd = -1;
  if (st->net_id >= 0)
    do_parent_net(st->child, st->net_id, st->net_futex);
  free_futex(st->net_futex);
  if (wait_root_ready(st->root_futex, st->child) == 0)
    remove_root(&st->root);
  free_futex(st->root_futex);
  return finish_child(st);
}

int spawn_cage(cage_config_t *cfg, uid_t ruid)
{
  struct spawn_st st;
  if (spawn_init(cfg, &st, ruid) < 0)
    return -1;
  int r = spawn_exec(&st);
  mem_free(st.free_pidfile);
  mem_free(st.env.host_xdg_runtime_dir);
  return r;
}

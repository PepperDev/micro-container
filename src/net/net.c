#include "net/net.h"
#include "net/netlink.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <fcntl.h>
// cppcheck-suppress missingIncludeSystem
#include <dirent.h>

// ULA prefix for the sandbox link: fdca:006e::/64. Each veth id N gets its own
// /127 block, mirroring the IPv4 /31: the peer (cage) takes 2N, the host end
// 2N+1, so the two ends are on-link.
void net6_addr(int id, int host_end, struct net6_addr *out)
{
  static const unsigned char pfx[15] = { 0xfd, 0xca, 0x00, 0x6e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  memset(out->b, 0, sizeof(out->b));
  memcpy(out->b, pfx, sizeof(pfx));
  out->b[15] = (unsigned char)((unsigned int)id * 2 + (host_end ? 1u : 0u));
}

// cppcheck-suppress staticFunction
int link_dump_find(int fd, const char *name, int *ifindex)
{
  return netlink_dump_find_ifname(fd, name, ifindex);
}

static int nl_transact(int fd, char *buf, size_t cap, int filled)
{
  if (filled < 0)
    return -1;
  if (sys_netlink_send(fd, buf, ((struct nlmsghdr *)buf)->nlmsg_len) < 0)
    return -1;
  size_t rlen;
  return sys_netlink_recv(fd, buf, cap, &rlen);
}

// cppcheck-suppress staticFunction
int addr_add(int fd, int ifindex, uint32_t local)
{
  char buf[128];
  int filled = netlink_fill_addr(buf, sizeof(buf), ifindex, 31, local);
  return nl_transact(fd, buf, sizeof(buf), filled);
}

// cppcheck-suppress staticFunction
int default_route_add(int fd, int ifindex, uint32_t gw)
{
  char buf[128];
  int filled = netlink_fill_default_route(buf, sizeof(buf), ifindex, gw);
  return nl_transact(fd, buf, sizeof(buf), filled);
}

// cppcheck-suppress staticFunction
int link_up(int fd, int ifindex)
{
  char buf[128];
  int filled = netlink_fill_up(buf, sizeof(buf), ifindex);
  return nl_transact(fd, buf, sizeof(buf), filled);
}

static int setup_loopback(int fd)
{
  int ifindex;
  if (link_dump_find(fd, "lo", &ifindex) < 0)
    return -1;
  return link_up(fd, ifindex);
}

static int addr6_add(int fd, int ifindex, int id, int host_end)
{
  char buf[128];
  struct net6_addr a;
  net6_addr(id, host_end, &a);
  int filled = netlink_fill_addr6(buf, sizeof(buf), ifindex, NET6_PREFIXLEN, &a);
  return nl_transact(fd, buf, sizeof(buf), filled);
}

static int default_route6_add(int fd, int ifindex, int id)
{
  char buf[128];
  struct net6_addr gw;
  net6_addr(id, 1, &gw);
  int filled = netlink_fill_default_route6(buf, sizeof(buf), ifindex, &gw);
  return nl_transact(fd, buf, sizeof(buf), filled);
}

static int config_child_v4(int fd, int id, int ifindex)
{
  if (addr_add(fd, ifindex, NET_CAGE_IP(id)) < 0)
    return -1;
  if (link_up(fd, ifindex) < 0)
    return -1;
  return default_route_add(fd, ifindex, NET_HOST_IP(id));
}

// IPv6 is best-effort: veth IPv6 addressing needs no host upstream, but a host
// with IPv6 disabled must not make a whole -N run fail.
static void config_child_v6(int fd, int id, int ifindex)
{
  if (addr6_add(fd, ifindex, id, 0) == 0)
    default_route6_add(fd, ifindex, id);
}

static int config_child_link(int fd, int id)
{
  int ifindex;
  if (link_dump_find(fd, CAGE_VETH_PEER_NAME, &ifindex) < 0)
    return -1;
  if (config_child_v4(fd, id, ifindex) < 0)
    return -1;
  config_child_v6(fd, id, ifindex);
  return 0;
}

int net_setup_child(int id)
{
  int fd;
  if (sys_netlink_open(&fd) < 0)
    return -1;
  int r = 0;
  if (config_child_link(fd, id) < 0 || setup_loopback(fd) < 0)
    r = -1;
  sys_netlink_close(fd);
  return r;
}

static int write_one(const char *path)
{
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  int r = write(fd, "1\n", 2);
  int e = close(fd);
  return r == 2 && e == 0 ? 0 : -1;
}

static int write_str(const char *path, const char *s)
{
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  size_t n = strlen(s);
  int r = (int)write(fd, s, n);
  int e = close(fd);
  return r == (int)n && e == 0 ? 0 : -1;
}

// cppcheck-suppress staticFunction
int __net_enable_ip_forward(void)
{
  return write_one("/proc/sys/net/ipv4/ip_forward");
}

weak_alias(__net_enable_ip_forward, net_enable_ip_forward);

static int raise_accept_ra(const char *name)
{
  char path[64], val[4];
  if (snprintf(path, sizeof(path), "/proc/sys/net/ipv6/conf/%s/accept_ra", name) >= (int)sizeof(path))
    return 1;
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return 1;
  ssize_t n = read(fd, val, sizeof(val) - 1);
  close(fd);
  if (n <= 0)
    return 1;
  val[n] = '\0';
  if (val[0] != '1')
    return 1;
  return write_str(path, "2\n") == 0;
}

static int skip_conf_entry(const char *name)
{
  return name[0] == '.' || !strcmp(name, "all") || !strcmp(name, "default");
}

static int raise_all_accept_ra(DIR *d)
{
  int ok = 1;
  const struct dirent *e;
  while (ok && (e = readdir(d)) != NULL) {
    if (!skip_conf_entry(e->d_name))
      ok = raise_accept_ra(e->d_name);
  }
  return ok;
}

/* Kernels before 6.17 lack force_forwarding: raise accept_ra on every
   interface first, so enabling global forwarding does not purge their RA
   default routes (rt6_purge_dflt_routers skips accept_ra == 2). */
static void legacy_ip6_forward(void)
{
  DIR *d = opendir("/proc/sys/net/ipv6/conf");
  if (!d)
    return;
  int ok = raise_all_accept_ra(d);
  closedir(d);
  if (!ok)
    return;
  write_str("/proc/sys/net/ipv6/conf/default/accept_ra", "2\n");
  write_one("/proc/sys/net/ipv6/conf/all/forwarding");
}

/* all/force_forwarding does not propagate to existing interfaces; write each
   one individually so return traffic entering the host upstream is forwarded. */
static void force_forward_existing(DIR *d)
{
  char path[64];
  const struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (skip_conf_entry(e->d_name))
      continue;
    if (snprintf(path, sizeof(path), "/proc/sys/net/ipv6/conf/%s/force_forwarding", e->d_name) >= (int)sizeof(path))
      continue;
    write_str(path, "1\n");
  }
}

// cppcheck-suppress staticFunction
int __net_enable_ip6_forward(void)
{
  if (write_str("/proc/sys/net/ipv6/conf/all/force_forwarding", "1\n") == 0) {
    write_str("/proc/sys/net/ipv6/conf/default/force_forwarding", "1\n");
    DIR *d = opendir("/proc/sys/net/ipv6/conf");
    if (d) {
      force_forward_existing(d);
      closedir(d);
    }
    return 0;
  }
  legacy_ip6_forward();
  return 0;
}

weak_alias(__net_enable_ip6_forward, net_enable_ip6_forward);

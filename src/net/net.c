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

#define HOST_PFX "cage-veth"
#define PEER_NAME "eth0"
#define MAX_VETH_ID 126

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
  char buf[16384];
  int len = netlink_fill_dump_link(buf, sizeof(buf));
  if (len < 0)
    return -1;
  if (sys_netlink_send(fd, buf, (size_t)len) < 0)
    return -1;
  size_t rlen;
  if (sys_netlink_recv_dump(fd, buf, sizeof(buf), &rlen) < 0)
    return -1;
  return netlink_find_ifname(buf, rlen, name, ifindex) > 0 ? 0 : -1;
}

// cppcheck-suppress staticFunction
int addr_add(int fd, int ifindex, uint32_t local)
{
  char buf[128];
  if (netlink_fill_addr(buf, sizeof(buf), ifindex, 31, local) < 0)
    return -1;
  if (sys_netlink_send(fd, buf, ((struct nlmsghdr *)buf)->nlmsg_len) < 0)
    return -1;
  size_t rlen;
  if (sys_netlink_recv(fd, buf, sizeof(buf), &rlen) < 0)
    return -1;
  return 0;
}

// cppcheck-suppress staticFunction
int default_route_add(int fd, int ifindex, uint32_t gw)
{
  char buf[128];
  if (netlink_fill_default_route(buf, sizeof(buf), ifindex, gw) < 0)
    return -1;
  if (sys_netlink_send(fd, buf, ((struct nlmsghdr *)buf)->nlmsg_len) < 0)
    return -1;
  size_t rlen;
  if (sys_netlink_recv(fd, buf, sizeof(buf), &rlen) < 0)
    return -1;
  return 0;
}

// cppcheck-suppress staticFunction
int link_up(int fd, int ifindex)
{
  char buf[128];
  if (netlink_fill_up(buf, sizeof(buf), ifindex) < 0)
    return -1;
  if (sys_netlink_send(fd, buf, ((struct nlmsghdr *)buf)->nlmsg_len) < 0)
    return -1;
  size_t rlen;
  if (sys_netlink_recv(fd, buf, sizeof(buf), &rlen) < 0)
    return -1;
  return 0;
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
  if (netlink_fill_addr6(buf, sizeof(buf), ifindex, NET6_PREFIXLEN, &a) < 0)
    return -1;
  if (sys_netlink_send(fd, buf, ((struct nlmsghdr *)buf)->nlmsg_len) < 0)
    return -1;
  size_t rlen;
  if (sys_netlink_recv(fd, buf, sizeof(buf), &rlen) < 0)
    return -1;
  return 0;
}

static int default_route6_add(int fd, int ifindex, int id)
{
  char buf[128];
  struct net6_addr gw;
  net6_addr(id, 1, &gw);
  if (netlink_fill_default_route6(buf, sizeof(buf), ifindex, &gw) < 0)
    return -1;
  if (sys_netlink_send(fd, buf, ((struct nlmsghdr *)buf)->nlmsg_len) < 0)
    return -1;
  size_t rlen;
  if (sys_netlink_recv(fd, buf, sizeof(buf), &rlen) < 0)
    return -1;
  return 0;
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
  if (link_dump_find(fd, PEER_NAME, &ifindex) < 0)
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

// cppcheck-suppress staticFunction
int __net_enable_ip_forward(void)
{
  int fd = open("/proc/sys/net/ipv4/ip_forward", O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  int r = write(fd, "1\n", 2);
  int e = close(fd);
  return r == 2 && e == 0 ? 0 : -1;
}

weak_alias(__net_enable_ip_forward, net_enable_ip_forward);

// cppcheck-suppress staticFunction
int __net_enable_ip6_forward(void)
{
  int fd = open("/proc/sys/net/ipv6/conf/all/forwarding", O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  int r = write(fd, "1\n", 2);
  int e = close(fd);
  return r == 2 && e == 0 ? 0 : -1;
}

weak_alias(__net_enable_ip6_forward, net_enable_ip6_forward);

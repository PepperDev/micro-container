#include "net/netlink.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <netinet/in.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/veth.h>

struct nla_blob {
  const void *p;
  uint16_t n;
};

static int put_attr(void *buf, size_t cap, size_t *off, int type, struct nla_blob d)
{
  size_t alen = NLA_ALIGN(sizeof(struct nlattr) + d.n);
  if (*off + alen > cap)
    return -1;
  struct nlattr *a = (struct nlattr *)((char *)buf + *off);
  a->nla_len = (uint16_t) (sizeof(struct nlattr) + d.n);
  a->nla_type = (uint16_t) type;
  if (d.n)
    memcpy(a + 1, d.p, d.n);
  *off += alen;
  return 0;
}

static int put_attr_u32(void *buf, size_t cap, size_t *off, int type, uint32_t val)
{
  return put_attr(buf, cap, off, type, (struct nla_blob) { &val, 4 });
}

static struct ifinfomsg *fill_ifinfomsg(struct nlmsghdr *h, unsigned char family, uint32_t change)
{
  struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(h);
  ifi->ifi_family = family;
  ifi->ifi_change = change;
  return ifi;
}

static int fill_veth_peer(void *buf, size_t cap, size_t *off, const char *peer, int nsfd)
{
  size_t vp = netlink_nest_start(buf, off, VETH_INFO_PEER | NLA_F_NESTED);
  memset((char *)buf + *off, 0, sizeof(struct ifinfomsg));
  *off += sizeof(struct ifinfomsg);
  if (netlink_put_string(buf, cap, off, IFLA_IFNAME, peer) < 0)
    return -1;
  if (put_attr_u32(buf, cap, off, IFLA_NET_NS_FD, (uint32_t) nsfd) < 0)
    return -1;
  netlink_nest_end(buf, vp, off);
  return 0;
}

static int fill_veth_linkinfo(void *buf, size_t cap, size_t *off, const char *peer, int nsfd)
{
  size_t li = netlink_nest_start(buf, off, IFLA_LINKINFO | NLA_F_NESTED);
  if (netlink_put_string(buf, cap, off, IFLA_INFO_KIND, "veth") < 0)
    return -1;
  size_t id = netlink_nest_start(buf, off, IFLA_INFO_DATA | NLA_F_NESTED);
  if (fill_veth_peer(buf, cap, off, peer, nsfd) < 0)
    return -1;
  netlink_nest_end(buf, id, off);
  netlink_nest_end(buf, li, off);
  return 0;
}

// cppcheck-suppress staticFunction
int netlink_fill_create_veth(void *buf, size_t cap, const char *host_name, const char *peer_name, int nsfd)
{
  memset(buf, 0, cap);
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  h->nlmsg_type = RTM_NEWLINK;
  h->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
  h->nlmsg_seq = 1;
  fill_ifinfomsg(h, AF_UNSPEC, 0xFFFFFFFF);
  size_t off = NLMSG_HDRLEN + sizeof(struct ifinfomsg);
  if (netlink_put_string(buf, cap, &off, IFLA_IFNAME, host_name) < 0 ||
      fill_veth_linkinfo(buf, cap, &off, peer_name, nsfd) < 0)
    return -1;
  h->nlmsg_len = (uint32_t) off;
  return 0;
}

int netlink_fill_dump_link(void *buf, size_t cap)
{
  size_t len = netlink_request_len();
  if (len > cap)
    return -1;
  memset(buf, 0, len);
  netlink_request_fill(buf, len, RTM_GETLINK, NLM_F_DUMP, AF_UNSPEC);
  return (int)len;
}

static int link_has_name(struct nlattr *attr, int alen, const char *name)
{
  while (NLA_OK(attr, alen)) {
    if (attr->nla_type == IFLA_IFNAME && strcmp((const char *)NLA_DATA(attr), name) == 0)
      return 1;
    NLA_NEXT(attr, alen);
  }
  return 0;
}

int netlink_find_ifname(const void *buf, size_t len, const char *name, int *ifindex)
{
  struct nlmsghdr *h;
  size_t rem = len;
  for (h = (struct nlmsghdr *)buf; NLMSG_OK(h, rem); h = NLMSG_NEXT(h, rem)) {
    if (h->nlmsg_type != RTM_NEWLINK)
      continue;
    struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(h);
    if (link_has_name((struct nlattr *)((char *)ifi + NLMSG_ALIGN(sizeof(*ifi))),
                      (int)NLMSG_PAYLOAD(h, sizeof(*ifi)), name)) {
      *ifindex = ifi->ifi_index;
      return 1;
    }
  }
  return 0;
}

static int parse_veth_id(const char *name, const char *prefix)
{
  size_t pfxlen = strlen(prefix);
  if (strncmp(name, prefix, pfxlen) != 0)
    return -1;
  char *end;
  long id = strtol(name + pfxlen, &end, 10);
  if (end == name + pfxlen || *end != '\0')
    return -1;
  return (int)id;
}

static void max_veth_scan_attrs(struct nlattr *attr, int alen, const char *prefix, int *max)
{
  while (NLA_OK(attr, alen)) {
    if (attr->nla_type == IFLA_IFNAME) {
      int vid = parse_veth_id((const char *)NLA_DATA(attr), prefix);
      if (vid >= 0 && vid > *max)
        *max = vid;
    }
    NLA_NEXT(attr, alen);
  }
}

// cppcheck-suppress staticFunction
int netlink_max_veth_id(const void *buf, size_t len, const char *prefix, int *id)
{
  int max = -1;
  struct nlmsghdr *h;
  size_t rem = len;
  for (h = (struct nlmsghdr *)buf; NLMSG_OK(h, rem); h = NLMSG_NEXT(h, rem)) {
    if (h->nlmsg_type != RTM_NEWLINK)
      continue;
    struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(h);
    max_veth_scan_attrs((struct nlattr *)((char *)ifi + NLMSG_ALIGN(sizeof(*ifi))),
                        (int)NLMSG_PAYLOAD(h, sizeof(*ifi)), prefix, &max);
  }
  *id = max;
  return max >= 0 ? 1 : 0;
}

static int fill_addr_attrs(void *buf, size_t cap, size_t *off, int prefix, uint32_t local)
{
  uint32_t addr = htonl(local);
  if (put_attr_u32(buf, cap, off, IFA_LOCAL, addr) < 0)
    return -1;
  if (prefix >= 31)
    return 0;
  uint32_t nm = htonl((1u << (32 - prefix)) - 1);
  return put_attr_u32(buf, cap, off, IFA_BROADCAST, addr | nm);
}

static struct ifaddrmsg *fill_addr_hdr(void *buf, int ifindex, int prefix, int family)
{
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  h->nlmsg_type = RTM_NEWADDR;
  h->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
  h->nlmsg_seq = 1;
  struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(h);
  ifa->ifa_family = (unsigned char)family;
  ifa->ifa_prefixlen = (unsigned char)prefix;
  ifa->ifa_index = ifindex;
  return ifa;
}

int netlink_fill_addr(void *buf, size_t cap, int ifindex, int prefix, uint32_t local)
{
  memset(buf, 0, cap);
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  fill_addr_hdr(buf, ifindex, prefix, AF_INET);
  size_t off = NLMSG_HDRLEN + sizeof(struct ifaddrmsg);
  if (fill_addr_attrs(buf, cap, &off, prefix, local) < 0)
    return -1;
  h->nlmsg_len = (uint32_t) off;
  return 0;
}

int netlink_fill_addr6(void *buf, size_t cap, int ifindex, int prefix, const struct net6_addr *addr)
{
  memset(buf, 0, cap);
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  struct ifaddrmsg *ifa = fill_addr_hdr(buf, ifindex, prefix, AF_INET6);
  ifa->ifa_flags = IFA_F_NODAD;
  size_t off = NLMSG_HDRLEN + sizeof(struct ifaddrmsg);
  if (put_attr(buf, cap, &off, IFA_ADDRESS, (struct nla_blob) { addr->b, sizeof(addr->b) }
      ) < 0)
    return -1;
  h->nlmsg_len = (uint32_t) off;
  return 0;
}

int netlink_fill_up(void *buf, size_t cap, int ifindex)
{
  memset(buf, 0, cap);
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  h->nlmsg_type = RTM_NEWLINK;
  h->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  h->nlmsg_seq = 1;
  struct ifinfomsg *ifi = fill_ifinfomsg(h, AF_UNSPEC, IFF_UP);
  ifi->ifi_index = ifindex;
  ifi->ifi_flags = IFF_UP;
  size_t off = NLMSG_HDRLEN + sizeof(struct ifinfomsg);

  h->nlmsg_len = (uint32_t) off;
  return 0;
}

static int fill_route_attrs(void *buf, size_t cap, size_t *off, int oif, uint32_t gw)
{
  if (put_attr_u32(buf, cap, off, RTA_GATEWAY, htonl(gw)) < 0)
    return -1;
  return put_attr_u32(buf, cap, off, RTA_OIF, (uint32_t) oif);
}

static struct rtmsg *fill_route_hdr(void *buf, int family)
{
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  h->nlmsg_type = RTM_NEWROUTE;
  h->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
  h->nlmsg_seq = 1;
  struct rtmsg *rt = (struct rtmsg *)NLMSG_DATA(h);
  rt->rtm_family = (unsigned char)family;
  rt->rtm_table = RT_TABLE_MAIN;
  rt->rtm_protocol = RTPROT_BOOT;
  rt->rtm_scope = RT_SCOPE_UNIVERSE;
  rt->rtm_type = RTN_UNICAST;
  return rt;
}

int netlink_fill_default_route(void *buf, size_t cap, int oif, uint32_t gw)
{
  memset(buf, 0, cap);
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  fill_route_hdr(buf, AF_INET);
  size_t off = NLMSG_HDRLEN + sizeof(struct rtmsg);
  if (fill_route_attrs(buf, cap, &off, oif, gw) < 0)
    return -1;
  h->nlmsg_len = (uint32_t) off;
  return 0;
}

int netlink_fill_default_route6(void *buf, size_t cap, int oif, const struct net6_addr *gw)
{
  memset(buf, 0, cap);
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  fill_route_hdr(buf, AF_INET6);
  size_t off = NLMSG_HDRLEN + sizeof(struct rtmsg);
  if (put_attr(buf, cap, &off, RTA_GATEWAY, (struct nla_blob) { gw->b, sizeof(gw->b) }
      ) < 0)
    return -1;
  if (put_attr_u32(buf, cap, &off, RTA_OIF, (uint32_t) oif) < 0)
    return -1;
  h->nlmsg_len = (uint32_t) off;
  return 0;
}

static int netlink_dump_find_ifname(int fd, const char *name, int *idx)
{
  char dbuf[16384];
  int len = netlink_fill_dump_link(dbuf, sizeof(dbuf));
  if (len < 0)
    return -1;
  if (sys_netlink_send(fd, dbuf, (size_t)len) < 0)
    return -1;
  size_t rlen;
  if (sys_netlink_recv_dump(fd, dbuf, sizeof(dbuf), &rlen) < 0)
    return -1;
  return netlink_find_ifname(dbuf, rlen, name, idx) > 0 ? 0 : -1;
}

int netlink_create_veth(int fd, const char *host, const char *peer, int nsfd, int *host_idx)
{
  char buf[512];
  if (netlink_fill_create_veth(buf, sizeof(buf), host, peer, nsfd) < 0)
    return -1;
  if (sys_netlink_send_ack(fd, buf, ((struct nlmsghdr *)buf)->nlmsg_len) < 0)
    return -1;
  return netlink_dump_find_ifname(fd, host, host_idx);
}

int netlink_alloc_veth_id(int fd, const char *prefix, int *id)
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
  int used;
  if (netlink_max_veth_id(buf, rlen, prefix, &used) == 0) {
    *id = 0;
    return 0;
  }
  if (used >= 126)
    return -1;
  *id = used + 1;
  return 0;
}

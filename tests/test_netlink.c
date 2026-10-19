#include "framework.h"
#include "net/netlink.h"
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/veth.h>

static void test_request_len(void)
{
  size_t len = netlink_request_len();
  ASSERT(len > 0);
  ASSERT(len == NLMSG_SPACE(sizeof(struct rtgenmsg)));
}

static void test_request_fill(void)
{
  char buf[128];
  size_t len = netlink_request_len();
  memset(buf, 0, sizeof(buf));
  netlink_request_fill(buf, len, RTM_GETLINK, NLM_F_DUMP, AF_UNSPEC);
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  ASSERT_EQ(h->nlmsg_len, len);
  ASSERT_EQ(h->nlmsg_type, RTM_GETLINK);
  ASSERT(h->nlmsg_flags & NLM_F_REQUEST);
  ASSERT(h->nlmsg_flags & NLM_F_ROOT);
}

static void test_put_string(void)
{
  char buf[128];
  size_t len = netlink_request_len();
  memset(buf, 0, sizeof(buf));
  netlink_request_fill(buf, len, RTM_GETLINK, NLM_F_DUMP, AF_UNSPEC);
  size_t off = len;
  int r = netlink_put_string(buf, sizeof(buf), &off, IFLA_IFNAME, "eth0");
  ASSERT_EQ(r, 0);
  ASSERT(off > len);
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  ASSERT_EQ(h->nlmsg_len, off);
  struct nlattr *attr = (struct nlattr *)((char *)h + NLMSG_SPACE(sizeof(struct rtgenmsg)));
  ASSERT_EQ(attr->nla_type, IFLA_IFNAME);
  ASSERT_EQ(attr->nla_len, (uint16_t) (sizeof(struct nlattr) + 5));
  ASSERT(memcmp(NLA_DATA(attr), "eth0", 5) == 0);
}

static void test_put_overflow(void)
{
  char buf[32];
  size_t len = netlink_request_len();
  memset(buf, 0, sizeof(buf));
  netlink_request_fill(buf, len, RTM_GETLINK, NLM_F_DUMP, AF_UNSPEC);
  size_t off = len;
  int r = netlink_put_string(buf, sizeof(buf), &off, IFLA_IFNAME,
                             "very-long-interface-name-that-exceeds-buffer");
  ASSERT_EQ(r, -1);
}

static void test_open_close(void)
{
  int fd;
  int r = netlink_open(&fd);
  ASSERT_EQ(r, 0);
  ASSERT(fd > 0);
  netlink_close(fd);
}

static void test_send_recv(void)
{
  int fd;
  ASSERT_EQ(netlink_open(&fd), 0);

  char buf[4096];
  size_t len = netlink_request_len();
  memset(buf, 0, sizeof(buf));
  netlink_request_fill(buf, len, RTM_GETLINK, NLM_F_DUMP, AF_UNSPEC);
  ASSERT_EQ(netlink_send(fd, buf, len), 0);

  char rbuf[16384];
  size_t rlen;
  ASSERT_EQ(netlink_recv(fd, rbuf, sizeof(rbuf), &rlen), 0);
  ASSERT(rlen > 0);
  struct nlmsghdr *h = (struct nlmsghdr *)rbuf;
  ASSERT(h->nlmsg_len <= rlen);
  netlink_close(fd);
}

static void test_recv_dump(void)
{
  int fd;
  ASSERT_EQ(netlink_open(&fd), 0);

  char buf[4096];
  size_t len = netlink_request_len();
  memset(buf, 0, sizeof(buf));
  netlink_request_fill(buf, len, RTM_GETLINK, NLM_F_DUMP, AF_UNSPEC);
  ASSERT_EQ(netlink_send(fd, buf, len), 0);

  char rbuf[65536];
  size_t rlen;
  ASSERT_EQ(netlink_recv_dump(fd, rbuf, sizeof(rbuf), &rlen), 0);
  ASSERT(rlen > 0);
  int found_done = 0;
  struct nlmsghdr *h;
  size_t rem = rlen;
  for (h = (struct nlmsghdr *)rbuf; NLMSG_OK(h, rem); h = NLMSG_NEXT(h, rem)) {
    if (h->nlmsg_type == NLMSG_DONE)
      found_done = 1;
  }
  ASSERT(found_done);
  netlink_close(fd);
}

static void test_fill_addr(void)
{
  char buf[128];
  int r = netlink_fill_addr(buf, sizeof(buf), 10, 31, 0xC0A80001);
  ASSERT_EQ(r, 0);

  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  ASSERT_EQ(h->nlmsg_type, RTM_NEWADDR);
  ASSERT(h->nlmsg_flags & NLM_F_REQUEST);
  ASSERT(h->nlmsg_flags & NLM_F_CREATE);
  ASSERT(h->nlmsg_flags & NLM_F_ACK);

  struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(h);
  ASSERT_EQ(ifa->ifa_family, AF_INET);
  ASSERT_EQ(ifa->ifa_prefixlen, 31);
  ASSERT_EQ(ifa->ifa_index, 10);
}

static void test_fill_addr_buffer_too_small(void)
{
  char buf[32];
  int r = netlink_fill_addr(buf, sizeof(buf), 10, 24, 0xC0A80001);
  ASSERT_EQ(r, -1);
}

static void test_fill_addr_with_broadcast(void)
{
  char buf[128];
  int r = netlink_fill_addr(buf, sizeof(buf), 10, 24, 0xC0A80001);
  ASSERT_EQ(r, 0);

  struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA((struct nlmsghdr *)buf);
  ASSERT_EQ(ifa->ifa_prefixlen, 24);

  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  size_t off = NLMSG_HDRLEN + sizeof(struct ifaddrmsg);
  struct nlattr *attr = (struct nlattr *)((char *)ifa + sizeof(*ifa));
  int rem = (int)(h->nlmsg_len - off);
  int found_local = 0, found_broadcast = 0;
  uint32_t local = 0, broadcast = 0;
  while (NLA_OK(attr, rem)) {
    if (attr->nla_type == IFA_LOCAL) {
      memcpy(&local, NLA_DATA(attr), 4);
      found_local = 1;
    }
    if (attr->nla_type == IFA_BROADCAST) {
      memcpy(&broadcast, NLA_DATA(attr), 4);
      found_broadcast = 1;
    }
    NLA_NEXT(attr, rem);
  }
  ASSERT(found_local);
  ASSERT(found_broadcast);
  ASSERT_EQ(local, htonl(0xC0A80001u));
  ASSERT_EQ(broadcast, htonl(0xC0A800FFu));
}

static void test_fill_up(void)
{
  char buf[128];
  int r = netlink_fill_up(buf, sizeof(buf), 10);
  ASSERT_EQ(r, 0);

  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  ASSERT_EQ(h->nlmsg_type, RTM_NEWLINK);
  ASSERT(h->nlmsg_flags & NLM_F_REQUEST);
  ASSERT(h->nlmsg_flags & NLM_F_ACK);
  ASSERT_EQ(h->nlmsg_len, NLMSG_SPACE(sizeof(struct ifinfomsg)));

  struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(h);
  ASSERT_EQ(ifi->ifi_index, 10);
  ASSERT_EQ(ifi->ifi_flags, (unsigned int)IFF_UP);
  ASSERT_EQ(ifi->ifi_change, (unsigned int)IFF_UP);
}

static struct nlattr *find_nlattr(struct nlattr *a, int alen, int type)
{
  while (NLA_OK(a, alen)) {
    if ((a->nla_type & NLA_TYPE_MASK) == type)
      return a;
    NLA_NEXT(a, alen);
  }
  return NULL;
}

static int peer_net_ns_fd(const void *buf, uint32_t *out)
{
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(h);
  struct nlattr *a = (struct nlattr *)((char *)ifi + NLMSG_ALIGN(sizeof(*ifi)));
  int alen = (int)NLMSG_PAYLOAD(h, sizeof(*ifi));
  struct nlattr *li = find_nlattr(a, alen, IFLA_LINKINFO);
  if (!li)
    return 0;
  struct nlattr *id = find_nlattr((struct nlattr *)NLA_DATA(li),
                                  (int)(li->nla_len - NLA_HDRLEN), IFLA_INFO_DATA);
  if (!id)
    return 0;
  struct nlattr *vp = find_nlattr((struct nlattr *)NLA_DATA(id),
                                  (int)(id->nla_len - NLA_HDRLEN), VETH_INFO_PEER);
  if (!vp || vp->nla_len < NLA_HDRLEN + NLMSG_ALIGN(sizeof(struct ifinfomsg)))
    return 0;
  struct nlattr *p = (struct nlattr *)((char *)NLA_DATA(vp) + NLMSG_ALIGN(sizeof(struct ifinfomsg)));
  struct nlattr *f = find_nlattr(p, (int)(vp->nla_len - NLA_HDRLEN -
                                          NLMSG_ALIGN(sizeof(struct ifinfomsg))), IFLA_NET_NS_FD);
  if (!f || f->nla_len < NLA_HDRLEN + 4)
    return 0;
  memcpy(out, NLA_DATA(f), 4);
  return 1;
}

static void test_fill_default_route(void)
{
  char buf[128];
  int r = netlink_fill_default_route(buf, sizeof(buf), 7, 0xAC180001);
  ASSERT_EQ(r, 0);

  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  ASSERT_EQ(h->nlmsg_type, RTM_NEWROUTE);
  ASSERT(h->nlmsg_flags & NLM_F_REQUEST);
  ASSERT(h->nlmsg_flags & NLM_F_CREATE);
  ASSERT(h->nlmsg_flags & NLM_F_EXCL);
  ASSERT(h->nlmsg_flags & NLM_F_ACK);

  struct rtmsg *rt = (struct rtmsg *)NLMSG_DATA(h);
  ASSERT_EQ(rt->rtm_family, AF_INET);
  ASSERT_EQ(rt->rtm_table, RT_TABLE_MAIN);
  ASSERT_EQ(rt->rtm_protocol, RTPROT_BOOT);
  ASSERT_EQ(rt->rtm_type, RTN_UNICAST);
  ASSERT_EQ(rt->rtm_dst_len, 0);

  struct nlattr *a = (struct nlattr *)((char *)rt + sizeof(*rt));
  int alen = (int)NLMSG_PAYLOAD(h, sizeof(*rt));
  struct nlattr *gw = find_nlattr(a, alen, RTA_GATEWAY);
  struct nlattr *oif = find_nlattr(a, alen, RTA_OIF);
  ASSERT_NOT_NULL(gw);
  ASSERT_NOT_NULL(oif);
  uint32_t gwv = 0, oifv = 0;
  memcpy(&gwv, NLA_DATA(gw), 4);
  memcpy(&oifv, NLA_DATA(oif), 4);
  ASSERT_EQ(gwv, htonl(0xAC180001u));
  ASSERT_EQ(oifv, 7u);
}

static void test_fill_default_route_buffer_too_small(void)
{
  char buf[32];
  int r = netlink_fill_default_route(buf, sizeof(buf), 7, 0xAC180001);
  ASSERT_EQ(r, -1);
}

static void fill6(struct net6_addr *a, unsigned char last)
{
  memset(a, 0, sizeof(*a));
  a->b[0] = 0xfd;
  a->b[1] = 0xca;
  a->b[15] = last;
}

static void test_fill_addr6(void)
{
  char buf[128];
  struct net6_addr a;
  fill6(&a, 0x42);
  int r = netlink_fill_addr6(buf, sizeof(buf), 10, 127, &a);
  ASSERT_EQ(r, 0);

  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  ASSERT_EQ(h->nlmsg_type, RTM_NEWADDR);
  ASSERT(h->nlmsg_flags & NLM_F_REQUEST);
  ASSERT(h->nlmsg_flags & NLM_F_CREATE);
  ASSERT(h->nlmsg_flags & NLM_F_ACK);

  struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(h);
  ASSERT_EQ(ifa->ifa_family, AF_INET6);
  ASSERT_EQ(ifa->ifa_prefixlen, 127);
  ASSERT_EQ(ifa->ifa_index, 10);
  ASSERT(ifa->ifa_flags & IFA_F_NODAD);

  struct nlattr *addr = find_nlattr((struct nlattr *)((char *)ifa + NLMSG_ALIGN(sizeof(*ifa))),
                                    (int)NLMSG_PAYLOAD(h, sizeof(*ifa)), IFA_ADDRESS);
  ASSERT_NOT_NULL(addr);
  ASSERT_EQ(addr->nla_len, (uint16_t) (NLA_HDRLEN + 16));
  ASSERT(memcmp(NLA_DATA(addr), a.b, 16) == 0);
}

static void test_fill_addr6_buffer_too_small(void)
{
  char buf[32];
  struct net6_addr a;
  fill6(&a, 1);
  int r = netlink_fill_addr6(buf, sizeof(buf), 10, 127, &a);
  ASSERT_EQ(r, -1);
}

static void test_fill_default_route6(void)
{
  char buf[128];
  struct net6_addr gw;
  fill6(&gw, 0x01);
  int r = netlink_fill_default_route6(buf, sizeof(buf), 7, &gw);
  ASSERT_EQ(r, 0);

  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  ASSERT_EQ(h->nlmsg_type, RTM_NEWROUTE);
  ASSERT(h->nlmsg_flags & NLM_F_CREATE);
  ASSERT(h->nlmsg_flags & NLM_F_EXCL);
  ASSERT(h->nlmsg_flags & NLM_F_ACK);

  struct rtmsg *rt = (struct rtmsg *)NLMSG_DATA(h);
  ASSERT_EQ(rt->rtm_family, AF_INET6);
  ASSERT_EQ(rt->rtm_table, RT_TABLE_MAIN);
  ASSERT_EQ(rt->rtm_type, RTN_UNICAST);
  ASSERT_EQ(rt->rtm_dst_len, 0);

  struct nlattr *a = (struct nlattr *)((char *)rt + sizeof(*rt));
  int alen = (int)NLMSG_PAYLOAD(h, sizeof(*rt));
  struct nlattr *g = find_nlattr(a, alen, RTA_GATEWAY);
  struct nlattr *o = find_nlattr(a, alen, RTA_OIF);
  ASSERT_NOT_NULL(g);
  ASSERT_NOT_NULL(o);
  ASSERT_EQ(g->nla_len, (uint16_t) (NLA_HDRLEN + 16));
  ASSERT(memcmp(NLA_DATA(g), gw.b, 16) == 0);
  uint32_t oifv = 0;
  memcpy(&oifv, NLA_DATA(o), 4);
  ASSERT_EQ(oifv, 7u);
}

static void test_fill_default_route6_buffer_too_small(void)
{
  char buf[32];
  struct net6_addr gw;
  fill6(&gw, 1);
  int r = netlink_fill_default_route6(buf, sizeof(buf), 7, &gw);
  ASSERT_EQ(r, -1);
}

static void test_fill_create_veth(void)
{
  char buf[512];
  int r = netlink_fill_create_veth(buf, sizeof(buf), "cage-veth0", "eth0", 7);
  ASSERT_EQ(r, 0);

  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  ASSERT_EQ(h->nlmsg_type, RTM_NEWLINK);
  ASSERT(h->nlmsg_flags & NLM_F_REQUEST);
  ASSERT(h->nlmsg_flags & NLM_F_CREATE);
  ASSERT(h->nlmsg_len > NLMSG_HDRLEN);

  struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(h);
  ASSERT_EQ(ifi->ifi_family, AF_UNSPEC);
  ASSERT_EQ(ifi->ifi_change, 0xFFFFFFFFu);

  uint32_t nsfd = 0;
  ASSERT(peer_net_ns_fd(buf, &nsfd));
  ASSERT_EQ(nsfd, 7u);
}

static void test_fill_create_veth_buffer_too_small(void)
{
  char buf[32];
  int r = netlink_fill_create_veth(buf, sizeof(buf), "cage-veth0", "eth0", 7);
  ASSERT_EQ(r, -1);
}

static void test_fill_dump_link(void)
{
  char buf[128];
  int len = netlink_fill_dump_link(buf, sizeof(buf));
  ASSERT(len > 0);
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  ASSERT_EQ(h->nlmsg_type, RTM_GETLINK);
  ASSERT(h->nlmsg_flags & NLM_F_REQUEST);
  ASSERT(h->nlmsg_flags & NLM_F_ROOT);
}

static void test_fill_dump_link_small_buf(void)
{
  char buf[16];
  int len = netlink_fill_dump_link(buf, sizeof(buf));
  ASSERT_EQ(len, -1);
}

static void test_find_ifname(void)
{
  int fd;
  ASSERT_EQ(netlink_open(&fd), 0);
  char buf[4096];
  int len = netlink_fill_dump_link(buf, sizeof(buf));
  ASSERT(len > 0);
  ASSERT_EQ(netlink_send(fd, buf, (size_t)len), 0);
  size_t rlen;
  ASSERT_EQ(netlink_recv_dump(fd, buf, sizeof(buf), &rlen), 0);
  int ifindex;
  int found = netlink_find_ifname(buf, rlen, "lo", &ifindex);
  ASSERT(found > 0);
  ASSERT(ifindex > 0);
  found = netlink_find_ifname(buf, rlen, "nonexistent-iface-xyz", &ifindex);
  ASSERT_EQ(found, 0);
  netlink_close(fd);
}

static void test_max_veth_id(void)
{
  int fd;
  ASSERT_EQ(netlink_open(&fd), 0);
  char buf[4096];
  int len = netlink_fill_dump_link(buf, sizeof(buf));
  ASSERT(len > 0);
  ASSERT_EQ(netlink_send(fd, buf, (size_t)len), 0);
  size_t rlen;
  ASSERT_EQ(netlink_recv_dump(fd, buf, sizeof(buf), &rlen), 0);
  int id;
  int r = netlink_max_veth_id(buf, rlen, "cage-veth", &id);
  ASSERT_EQ(r, 0);
  ASSERT_EQ(id, -1);
  netlink_close(fd);
}

static void test_alloc_veth_id(void)
{
  int fd;
  ASSERT_EQ(netlink_open(&fd), 0);
  int id;
  int r = netlink_alloc_veth_id(fd, "cage-veth", &id);
  ASSERT_EQ(r, 0);
  ASSERT(id >= 0);
  netlink_close(fd);
}

static void test_max_veth_id_crafted(void)
{
  char buf[256];
  memset(buf, 0, sizeof(buf));
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  h->nlmsg_type = RTM_NEWLINK;
  h->nlmsg_flags = NLM_F_REQUEST;
  h->nlmsg_seq = 1;
  struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(h);
  ifi->ifi_family = AF_UNSPEC;
  ifi->ifi_index = 5;
  size_t off = NLMSG_HDRLEN + sizeof(struct ifinfomsg);
  ASSERT_EQ(netlink_put_string(buf, sizeof(buf), &off, IFLA_IFNAME, "cage-veth5"), 0);
  h->nlmsg_len = (uint32_t) off;
  int id;
  int r = netlink_max_veth_id(buf, off, "cage-veth", &id);
  ASSERT_EQ(r, 1);
  ASSERT_EQ(id, 5);
}

SUITE(netlink)
{
  RUN_TEST(request_len);
  RUN_TEST(request_fill);
  RUN_TEST(put_string);
  RUN_TEST(put_overflow);
  RUN_TEST(open_close);
  RUN_TEST(send_recv);
  RUN_TEST(recv_dump);
  RUN_TEST(fill_addr);
  RUN_TEST(fill_addr_buffer_too_small);
  RUN_TEST(fill_addr_with_broadcast);
  RUN_TEST(fill_up);
  RUN_TEST(fill_default_route);
  RUN_TEST(fill_default_route_buffer_too_small);
  RUN_TEST(fill_addr6);
  RUN_TEST(fill_addr6_buffer_too_small);
  RUN_TEST(fill_default_route6);
  RUN_TEST(fill_default_route6_buffer_too_small);
  RUN_TEST(fill_create_veth);
  RUN_TEST(fill_create_veth_buffer_too_small);
  RUN_TEST(fill_dump_link);
  RUN_TEST(fill_dump_link_small_buf);
  RUN_TEST(find_ifname);
  RUN_TEST(max_veth_id);
  RUN_TEST(alloc_veth_id);
  RUN_TEST(max_veth_id_crafted);
}

#ifndef CAGE_NETLINK_H
#define CAGE_NETLINK_H

// cppcheck-suppress missingIncludeSystem
#include <stddef.h>
// cppcheck-suppress missingIncludeSystem
#include <stdint.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/netlink.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/rtnetlink.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/if_link.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/if_addr.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/if.h>

#define NLA_DATA(attr) ((void *)((char *)(attr) + NLA_HDRLEN))
#define NLA_NEXT(attr, len) do { (len) -= NLA_ALIGN((attr)->nla_len); (attr) = (struct nlattr *)((char *)(attr) + NLA_ALIGN((attr)->nla_len)); } while(0)
#define NLA_OK(attr, len) ((len) >= (int)sizeof(struct nlattr) && (attr)->nla_len >= sizeof(struct nlattr) && (attr)->nla_len <= (unsigned int)(len))

int netlink_open(int *fd);
void netlink_close(int fd);
int netlink_send(int fd, const void *buf, size_t len);
int netlink_recv(int fd, void *buf, size_t cap, size_t *len);
int netlink_recv_nb(int fd, void *buf, size_t cap, size_t *len);
size_t netlink_request_len(void);
void netlink_request_fill(void *buf, size_t len, int type, int flags, int family);

int netlink_recv_dump(int fd, void *buf, size_t cap, size_t *len);
int netlink_send_ack(int fd, const void *buf, size_t len);

int netlink_put_string(void *buf, size_t cap, size_t *off, int type, const char *val);

size_t netlink_nest_start(void *buf, size_t *off, int type);
void netlink_nest_end(void *buf, size_t start, const size_t *off);

struct net6_addr {
  unsigned char b[16];
};

int netlink_fill_create_veth(void *buf, size_t cap, const char *host_name, const char *peer_name, int nsfd);
int netlink_fill_addr(void *buf, size_t cap, int ifindex, int prefix, uint32_t local);
int netlink_fill_addr6(void *buf, size_t cap, int ifindex, int prefix, const struct net6_addr *addr);
int netlink_fill_up(void *buf, size_t cap, int ifindex);
int netlink_fill_default_route(void *buf, size_t cap, int oif, uint32_t gw);
int netlink_fill_default_route6(void *buf, size_t cap, int oif, const struct net6_addr *gw);

int netlink_fill_dump_link(void *buf, size_t cap);
int netlink_find_ifname(const void *buf, size_t len, const char *name, int *ifindex);
int netlink_max_veth_id(const void *buf, size_t len, const char *prefix, int *id);
int netlink_create_veth(int fd, const char *host, const char *peer, int nsfd, int *host_idx);
int netlink_alloc_veth_id(int fd, const char *prefix, int *id);

#endif

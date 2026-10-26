#include "net/net.h"
#include "net/netlink.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/socket.h>
// cppcheck-suppress missingIncludeSystem
#include <arpa/inet.h>
// cppcheck-suppress missingIncludeSystem
#include <errno.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/netfilter.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/netfilter/nfnetlink.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/netfilter/nf_tables.h>

static int nft_ack_errno(const void *buf, size_t len)
{
  const struct nlmsghdr *h = (const struct nlmsghdr *)buf;
  if (len < NLMSG_HDRLEN || !NLMSG_OK(h, len))
    return -EINVAL;
  if (h->nlmsg_type != NLMSG_ERROR)
    return 0;
  if (h->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr)))
    return -EINVAL;
  const struct nlmsgerr *e = (const struct nlmsgerr *)NLMSG_DATA(h);
  return e->error;
}

static int nft_drain_rest(int fd, int err)
{
  char ack[4096];
  size_t alen;
  while (sys_netlink_recv_nb(fd, ack, sizeof(ack), &alen) == 0) {
    int e = nft_ack_errno(ack, alen);
    if (e != 0 && err == 0)
      err = e;
  }
  return err;
}

static int nft_recv_ack(int fd)
{
  char ack[4096];
  size_t alen;
  if (sys_netlink_recv(fd, ack, sizeof(ack), &alen) < 0)
    return -1;
  return nft_drain_rest(fd, nft_ack_errno(ack, alen));
}

static int nft_put_be32(void *req, size_t cap, size_t *off, uint16_t type, uint32_t val)
{
  uint32_t be = htonl(val);
  return netlink_put_u32(req, cap, off, type, be);
}

static int nft_chain_hook(void *req, size_t cap, size_t *off)
{
  size_t hoff;
  if (netlink_nest_start(req, cap, off, NFTA_CHAIN_HOOK | NLA_F_NESTED, &hoff) != 0)
    return -1;
  if (nft_put_be32(req, cap, off, NFTA_HOOK_HOOKNUM, NF_INET_POST_ROUTING) != 0)
    return -1;
  if (nft_put_be32(req, cap, off, NFTA_HOOK_PRIORITY, 100) != 0)
    return -1;
  netlink_nest_end(req, hoff, off);
  return 0;
}

struct nft_batch {
  char data[768];
  size_t len;
  uint32_t seq;
};

static void nft_batch_open(struct nft_batch *b)
{
  b->len = 0;
  b->seq = 0;
  struct nlmsghdr *h = (struct nlmsghdr *)b->data;
  memset(h, 0, NLMSG_HDRLEN + sizeof(struct nfgenmsg));
  h->nlmsg_type = NFNL_MSG_BATCH_BEGIN;
  h->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  h->nlmsg_len = NLMSG_HDRLEN + sizeof(struct nfgenmsg);
  struct nfgenmsg *g = (struct nfgenmsg *)NLMSG_DATA(h);
  g->nfgen_family = AF_UNSPEC;
  g->version = NFNETLINK_V0;
  g->res_id = htons(NFNL_SUBSYS_NFTABLES);
  b->len = NLMSG_ALIGN(h->nlmsg_len);
}

static int nft_batch_msg(struct nft_batch *b, uint16_t type, uint16_t flags, void *msg, size_t mlen)
{
  if (b->len + NLMSG_ALIGN(mlen) > sizeof(b->data))
    return -1;
  struct nlmsghdr *h = (struct nlmsghdr *)msg;
  h->nlmsg_len = (uint32_t) mlen;
  h->nlmsg_type = type;
  h->nlmsg_flags = NLM_F_REQUEST | flags;
  h->nlmsg_seq = ++b->seq;
  memcpy(b->data + b->len, msg, mlen);
  b->len += NLMSG_ALIGN(mlen);
  return 0;
}

static int nft_batch_end(struct nft_batch *b)
{
  size_t elen = NLMSG_ALIGN(NLMSG_HDRLEN + sizeof(struct nfgenmsg));
  if (b->len + elen > sizeof(b->data))
    return -1;
  struct nlmsghdr *h = (struct nlmsghdr *)(b->data + b->len);
  memset(h, 0, NLMSG_HDRLEN + sizeof(struct nfgenmsg));
  h->nlmsg_type = NFNL_MSG_BATCH_END;
  h->nlmsg_flags = NLM_F_REQUEST;
  h->nlmsg_len = NLMSG_HDRLEN + sizeof(struct nfgenmsg);
  h->nlmsg_seq = ++b->seq;
  struct nfgenmsg *g = (struct nfgenmsg *)NLMSG_DATA(h);
  g->version = NFNETLINK_V0;
  g->res_id = htons(NFNL_SUBSYS_NFTABLES);
  b->len += elen;
  return 0;
}

static int nft_batch_flush(struct nft_batch *b, int fd)
{
  if (nft_batch_end(b) != 0)
    return -1;
  if (sys_netlink_send(fd, b->data, b->len) < 0)
    return -1;
  return nft_recv_ack(fd);
}

#define NFT_TABLE_NAME "cage-nat"
#define NFT_CHAIN_NAME "postrouting"

#define NFT_INIT(family) \
  memset(&req, 0, sizeof(req)); \
  req.nfg.nfgen_family = (family); \
  req.nfg.version = NFNETLINK_V0; \
  req.nfg.res_id = 0; \
  off = sizeof(struct nlmsghdr) + sizeof(struct nfgenmsg)

#define NFT_PUT_STR(field, val) do { \
  if (netlink_put_string(&req, sizeof(req), &off, (field), (val)) != 0) \
    return -1; \
} while (0)

static int nft_create_table(struct nft_batch *b, int family)
{
  struct {
    struct nlmsghdr nh;
    struct nfgenmsg nfg;
    // cppcheck-suppress unusedStructMember
    char data[128];
  } req;
  size_t off;
  NFT_INIT(family);
  NFT_PUT_STR(NFTA_TABLE_NAME, NFT_TABLE_NAME);
  return nft_batch_msg(b, (NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWTABLE, NLM_F_CREATE | NLM_F_EXCL, &req, off);
}

static int nft_create_chain(struct nft_batch *b, int family)
{
  struct {
    struct nlmsghdr nh;
    struct nfgenmsg nfg;
    // cppcheck-suppress unusedStructMember
    char data[256];
  } req;
  size_t off;
  NFT_INIT(family);
  NFT_PUT_STR(NFTA_CHAIN_TABLE, NFT_TABLE_NAME);
  NFT_PUT_STR(NFTA_CHAIN_NAME, NFT_CHAIN_NAME);
  if (nft_chain_hook(&req, sizeof(req), &off) != 0)
    return -1;
  NFT_PUT_STR(NFTA_CHAIN_TYPE, "nat");
  return nft_batch_msg(b, (NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWCHAIN, NLM_F_CREATE, &req, off);
}

static int nft_put_expr_masq(void *req, size_t cap, size_t *off)
{
  size_t list, elem, data;
  if (netlink_nest_start(req, cap, off, NFTA_RULE_EXPRESSIONS | NLA_F_NESTED, &list) != 0)
    return -1;
  if (netlink_nest_start(req, cap, off, NFTA_LIST_ELEM | NLA_F_NESTED, &elem) != 0)
    return -1;
  if (netlink_put_string(req, cap, off, NFTA_EXPR_NAME, "masq") != 0)
    return -1;
  if (netlink_nest_start(req, cap, off, NFTA_EXPR_DATA | NLA_F_NESTED, &data) != 0)
    return -1;
  netlink_nest_end(req, data, off);
  netlink_nest_end(req, elem, off);
  netlink_nest_end(req, list, off);
  return 0;
}

static int nft_add_masq_rule(struct nft_batch *b, int family)
{
  struct {
    struct nlmsghdr nh;
    struct nfgenmsg nfg;
    // cppcheck-suppress unusedStructMember
    char data[256];
  } req;
  size_t off;
  NFT_INIT(family);
  NFT_PUT_STR(NFTA_RULE_TABLE, NFT_TABLE_NAME);
  NFT_PUT_STR(NFTA_RULE_CHAIN, NFT_CHAIN_NAME);
  if (nft_put_expr_masq(&req, sizeof(req), &off) != 0)
    return -1;
  return nft_batch_msg(b, (NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWRULE, NLM_F_CREATE | NLM_F_APPEND, &req, off);
}

static int nft_build(struct nft_batch *b, int family)
{
  if (nft_create_table(b, family) != 0)
    return -1;
  if (nft_create_chain(b, family) != 0)
    return -1;
  return nft_add_masq_rule(b, family);
}

static int nft_setup_family(int family)
{
  int fd;
  if (sys_netfilter_open(&fd) < 0)
    return -1;
  struct nft_batch b;
  nft_batch_open(&b);
  if (nft_build(&b, family) != 0) {
    close(fd);
    return -1;
  }
  int r = nft_batch_flush(&b, fd);
  close(fd);
  return r == 0 || r == -EEXIST ? 0 : -1;
}

int net_setup_nat(void)
{
  return nft_setup_family(AF_INET);
}

int net_setup_nat6(void)
{
  return nft_setup_family(AF_INET6);
}

// cppcheck-suppress staticFunction
int __sys_netfilter_open(int *fd)
{
  return netfilter_open(fd);
}

weak_alias(__sys_netfilter_open, sys_netfilter_open);

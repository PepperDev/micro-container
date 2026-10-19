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

static int nft_ack_errno(const void *buf)
{
  const struct nlmsghdr *h = (const struct nlmsghdr *)buf;
  if (h->nlmsg_type != NLMSG_ERROR)
    return 0;
  const struct nlmsgerr *e = (const struct nlmsgerr *)NLMSG_DATA(h);
  return e->error;
}

static int nft_drain_rest(int fd, int err)
{
  char ack[4096];
  size_t alen;
  while (sys_netlink_recv_nb(fd, ack, sizeof(ack), &alen) == 0) {
    int e = nft_ack_errno(ack);
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
  return nft_drain_rest(fd, nft_ack_errno(ack));
}

static void *nft_put_attr(void *p, uint16_t type, const void *data, uint16_t dlen)
{
  size_t alen = NLA_ALIGN(sizeof(struct nlattr) + dlen);
  struct nlattr *a = (struct nlattr *)p;
  a->nla_len = (uint16_t) (sizeof(struct nlattr) + dlen);
  a->nla_type = type;
  if (data && dlen)
    memcpy(a + 1, data, dlen);
  return (char *)p + alen;
}

static void *nft_put_str(void *p, uint16_t type, const char *s)
{
  return nft_put_attr(p, type, s, (uint16_t) (strlen(s) + 1));
}

static void *nft_put_be32(void *p, uint16_t type, uint32_t val)
{
  uint32_t be = htonl(val);
  return nft_put_attr(p, type, &be, 4);
}

static void *nft_put_nest(void *p, uint16_t type)
{
  struct nlattr *a = (struct nlattr *)p;
  a->nla_len = (uint16_t) sizeof(struct nlattr);
  a->nla_type = type | NLA_F_NESTED;
  return (char *)p + sizeof(struct nlattr);
}

static size_t nft_nest_off(void *req, size_t off, uint16_t type)
{
  return (size_t)((char *)nft_put_nest((char *)req + off, type) - (char *)req);
}

static size_t nft_str_off(void *req, size_t off, uint16_t type, const char *s)
{
  return (size_t)((char *)nft_put_str((char *)req + off, type, s) - (char *)req);
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

static void nft_batch_msg(struct nft_batch *b, uint16_t type, uint16_t flags, void *msg, size_t mlen)
{
  struct nlmsghdr *h = (struct nlmsghdr *)msg;
  h->nlmsg_len = (uint32_t) mlen;
  h->nlmsg_type = type;
  h->nlmsg_flags = NLM_F_REQUEST | flags;
  h->nlmsg_seq = ++b->seq;
  memcpy(b->data + b->len, msg, mlen);
  b->len += NLMSG_ALIGN(mlen);
}

static int nft_batch_flush(struct nft_batch *b, int fd)
{
  struct nlmsghdr *h = (struct nlmsghdr *)(b->data + b->len);
  memset(h, 0, NLMSG_HDRLEN + sizeof(struct nfgenmsg));
  h->nlmsg_type = NFNL_MSG_BATCH_END;
  h->nlmsg_flags = NLM_F_REQUEST;
  h->nlmsg_len = NLMSG_HDRLEN + sizeof(struct nfgenmsg);
  h->nlmsg_seq = ++b->seq;
  struct nfgenmsg *g = (struct nfgenmsg *)NLMSG_DATA(h);
  g->version = NFNETLINK_V0;
  g->res_id = htons(NFNL_SUBSYS_NFTABLES);
  b->len += NLMSG_ALIGN(h->nlmsg_len);
  if (sys_netlink_send(fd, b->data, b->len) < 0)
    return -1;
  return nft_recv_ack(fd);
}

#define NFT_INIT(family) \
  memset(&req, 0, sizeof(req)); \
  req.nfg.nfgen_family = (family); \
  req.nfg.version = NFNETLINK_V0; \
  req.nfg.res_id = 0; \
  off = sizeof(struct nlmsghdr) + sizeof(struct nfgenmsg)

#define NFT_PUT_STR(field, val) \
  off = (size_t)((char *)nft_put_str((char *)&req + off, (field), (val)) - (char *)&req)

#define NFT_PUT_BE32(field, val) \
  off = (size_t)((char *)nft_put_be32((char *)&req + off, (field), (val)) - (char *)&req)

static void nft_create_table(struct nft_batch *b, int family)
{
  struct {
    struct nlmsghdr nh;
    struct nfgenmsg nfg;
    // cppcheck-suppress unusedStructMember
    char data[128];
  } req;
  size_t off;
  NFT_INIT(family);
  NFT_PUT_STR(NFTA_TABLE_NAME, "cage-nat");
  nft_batch_msg(b, (NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWTABLE, NLM_F_CREATE | NLM_F_EXCL, &req, off);
}

static void nft_create_chain(struct nft_batch *b, int family)
{
  struct {
    struct nlmsghdr nh;
    struct nfgenmsg nfg;
    // cppcheck-suppress unusedStructMember
    char data[256];
  } req;
  size_t off;
  NFT_INIT(family);
  NFT_PUT_STR(NFTA_CHAIN_TABLE, "cage-nat");
  NFT_PUT_STR(NFTA_CHAIN_NAME, "postrouting");
  size_t hoff = off;
  struct nlattr *ha = (struct nlattr *)((char *)&req + off);
  ha->nla_type = NFTA_CHAIN_HOOK | NLA_F_NESTED;
  off += sizeof(struct nlattr);
  NFT_PUT_BE32(NFTA_HOOK_HOOKNUM, NF_INET_POST_ROUTING);
  NFT_PUT_BE32(NFTA_HOOK_PRIORITY, 100);
  ha->nla_len = (uint16_t) (off - hoff);
  NFT_PUT_STR(NFTA_CHAIN_TYPE, "nat");
  nft_batch_msg(b, (NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWCHAIN, NLM_F_CREATE, &req, off);
}

static void nft_set_len(void *req, size_t start, size_t off)
{
  struct nlattr *a = (struct nlattr *)((char *)req + start);
  a->nla_len = (uint16_t) (off - start);
}

static size_t nft_put_expr_masq(void *req, size_t off)
{
  size_t list = off;
  off = nft_nest_off(req, off, NFTA_RULE_EXPRESSIONS);
  size_t elem = off;
  off = nft_nest_off(req, off, NFTA_LIST_ELEM);
  off = nft_str_off(req, off, NFTA_EXPR_NAME, "masq");
  off = nft_nest_off(req, off, NFTA_EXPR_DATA);
  nft_set_len(req, list, off);
  nft_set_len(req, elem, off);
  return off;
}

static void nft_add_masq_rule(struct nft_batch *b, int family)
{
  struct {
    struct nlmsghdr nh;
    struct nfgenmsg nfg;
    // cppcheck-suppress unusedStructMember
    char data[256];
  } req;
  size_t off;
  NFT_INIT(family);
  NFT_PUT_STR(NFTA_RULE_TABLE, "cage-nat");
  NFT_PUT_STR(NFTA_RULE_CHAIN, "postrouting");
  off = nft_put_expr_masq(&req, off);
  nft_batch_msg(b, (NFNL_SUBSYS_NFTABLES << 8) | NFT_MSG_NEWRULE, NLM_F_CREATE | NLM_F_APPEND, &req, off);
}

int netfilter_open(int *fd)
{
  int s = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_NETFILTER);
  if (s < 0)
    return -1;
  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  sa.nl_groups = 0;
  if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    close(s);
    return -1;
  }
  *fd = s;
  return 0;
}

static int nft_setup_family(int family)
{
  int fd;
  if (sys_netfilter_open(&fd) < 0)
    return -1;
  struct nft_batch b;
  nft_batch_open(&b);
  nft_create_table(&b, family);
  nft_create_chain(&b, family);
  nft_add_masq_rule(&b, family);
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

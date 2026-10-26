#include "net/netlink.h"
#include "util/syscall.h"
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <sys/socket.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <linux/veth.h>

static int nl_open(int protocol, int *fd)
{
  int s = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, protocol);
  if (s < 0)
    return -1;
  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    close(s);
    return -1;
  }
  *fd = s;
  return 0;
}

// cppcheck-suppress staticFunction -- used by tests
int netlink_open(int *fd)
{
  return nl_open(NETLINK_ROUTE, fd);
}

int netfilter_open(int *fd)
{
  return nl_open(NETLINK_NETFILTER, fd);
}

// cppcheck-suppress staticFunction
void netlink_close(int fd)
{
  close(fd);
}

// cppcheck-suppress staticFunction
int netlink_send(int fd, const void *buf, size_t len)
{
  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  struct iovec iov = {.iov_base = (void *)buf,.iov_len = len };
  struct msghdr msg = {
    .msg_name = &sa,
    .msg_namelen = sizeof(sa),
    .msg_iov = &iov,
    .msg_iovlen = 1,
  };
  if (sendmsg(fd, &msg, 0) < 0)
    return -1;
  return 0;
}

static int netlink_recv_flags(int fd, void *buf, size_t cap, size_t *len, int flags)
{
  struct iovec iov = {.iov_base = buf,.iov_len = cap };
  struct msghdr msg = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
  };
  ssize_t n = recvmsg(fd, &msg, flags);
  if (n < 0)
    return -1;
  *len = (size_t)n;
  return 0;
}

// cppcheck-suppress staticFunction
int netlink_recv(int fd, void *buf, size_t cap, size_t *len)
{
  return netlink_recv_flags(fd, buf, cap, len, 0);
}

// cppcheck-suppress staticFunction
int netlink_recv_nb(int fd, void *buf, size_t cap, size_t *len)
{
  return netlink_recv_flags(fd, buf, cap, len, MSG_DONTWAIT);
}

size_t netlink_request_len(void)
{
  return NLMSG_SPACE(sizeof(struct rtgenmsg));
}

void netlink_request_fill(void *buf, size_t len, int type, int flags, int family)
{
  struct nlmsghdr *h = (struct nlmsghdr *)buf;
  h->nlmsg_len = (uint32_t) len;
  h->nlmsg_type = (uint16_t) type;
  h->nlmsg_flags = (uint16_t) (NLM_F_REQUEST | flags);
  h->nlmsg_seq = 1;
  h->nlmsg_pid = 0;
  struct rtgenmsg *rt = (struct rtgenmsg *)NLMSG_DATA(h);
  rt->rtgen_family = (unsigned char)family;
}

static int nl_has_done(const void *buf, size_t len)
{
  struct nlmsghdr *h;
  size_t rem = len;
  for (h = (struct nlmsghdr *)buf; NLMSG_OK(h, rem); h = NLMSG_NEXT(h, rem))
    if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR)
      return 1;
  return 0;
}

// cppcheck-suppress staticFunction
int netlink_recv_dump(int fd, void *buf, size_t cap, size_t *len)
{
  size_t total = 0;
  for (;;) {
    if (total >= cap)
      break;
    size_t n;
    if (netlink_recv(fd, (char *)buf + total, cap - total, &n) < 0)
      return -1;
    if (n == 0)
      break;
    total += n;
    if (nl_has_done((char *)buf + total - n, n))
      break;
  }
  *len = total;
  return 0;
}

int netlink_put_attr(void *buf, size_t cap, size_t *off, const struct nl_attr *attr)
{
  if (attr->len > UINT16_MAX - sizeof(struct nlattr))
    return -1;
  size_t alen = NLA_ALIGN(sizeof(struct nlattr) + attr->len);
  if (*off > cap || alen > cap - *off)
    return -1;
  struct nlattr *a = (struct nlattr *)((char *)buf + *off);
  a->nla_len = (uint16_t) (sizeof(struct nlattr) + attr->len);
  a->nla_type = attr->type;
  if (attr->len)
    memcpy(a + 1, attr->data, attr->len);
  *off += alen;
  return 0;
}

int netlink_put_string(void *buf, size_t cap, size_t *off, int type, const char *val)
{
  struct nl_attr attr = {.type = (uint16_t) type,.len = strlen(val) + 1,.data = val };
  if (netlink_put_attr(buf, cap, off, &attr) != 0)
    return -1;
  ((struct nlmsghdr *)buf)->nlmsg_len = (uint32_t) * off;
  return 0;
}

int netlink_put_u32(void *buf, size_t cap, size_t *off, int type, uint32_t val)
{
  struct nl_attr attr = {.type = (uint16_t) type,.len = sizeof(val),.data = &val };
  return netlink_put_attr(buf, cap, off, &attr);
}

int netlink_nest_start(void *buf, size_t cap, size_t *off, int type, size_t *start)
{
  if (*off + sizeof(struct nlattr) > cap)
    return -1;
  struct nlattr *attr = (struct nlattr *)((char *)buf + *off);
  attr->nla_type = (uint16_t) type;
  *start = *off;
  *off += sizeof(struct nlattr);
  return 0;
}

void netlink_nest_end(void *buf, size_t start, const size_t *off)
{
  struct nlattr *attr = (struct nlattr *)((char *)buf + start);
  attr->nla_len = (uint16_t) (*off - start);
}

// cppcheck-suppress staticFunction
int netlink_ack_result(const void *ack, size_t alen)
{
  if (alen < (size_t)NLMSG_HDRLEN)
    return -1;
  const struct nlmsghdr *ah = (const struct nlmsghdr *)ack;
  if (!NLMSG_OK(ah, alen))
    return -1;
  if (ah->nlmsg_type != NLMSG_ERROR)
    return 0;
  if (ah->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr)))
    return -1;
  const struct nlmsgerr *e = (const struct nlmsgerr *)NLMSG_DATA(ah);
  return e->error == 0 ? 0 : -1;
}

// cppcheck-suppress staticFunction
int netlink_send_ack(int fd, const void *buf, size_t len)
{
  if (netlink_send(fd, buf, len) < 0)
    return -1;
  char ack[4096];
  size_t alen;
  if (netlink_recv(fd, ack, sizeof(ack), &alen) < 0)
    return -1;
  return netlink_ack_result(ack, alen);
}

// cppcheck-suppress staticFunction
int __sys_netlink_open(int *fd)
{
  return netlink_open(fd);
}

weak_alias(__sys_netlink_open, sys_netlink_open);

// cppcheck-suppress staticFunction
void __sys_netlink_close(int fd)
{
  netlink_close(fd);
}

weak_alias(__sys_netlink_close, sys_netlink_close);

// cppcheck-suppress staticFunction
int __sys_netlink_send(int fd, const void *buf, size_t len)
{
  return netlink_send(fd, buf, len);
}

weak_alias(__sys_netlink_send, sys_netlink_send);

// cppcheck-suppress staticFunction
int __sys_netlink_recv(int fd, void *buf, size_t cap, size_t *len)
{
  return netlink_recv(fd, buf, cap, len);
}

weak_alias(__sys_netlink_recv, sys_netlink_recv);

// cppcheck-suppress staticFunction
int __sys_netlink_recv_nb(int fd, void *buf, size_t cap, size_t *len)
{
  return netlink_recv_nb(fd, buf, cap, len);
}

weak_alias(__sys_netlink_recv_nb, sys_netlink_recv_nb);

// cppcheck-suppress staticFunction
int __sys_netlink_send_ack(int fd, const void *buf, size_t len)
{
  return netlink_send_ack(fd, buf, len);
}

weak_alias(__sys_netlink_send_ack, sys_netlink_send_ack);

// cppcheck-suppress staticFunction
int __sys_netlink_recv_dump(int fd, void *buf, size_t cap, size_t *len)
{
  return netlink_recv_dump(fd, buf, cap, len);
}

weak_alias(__sys_netlink_recv_dump, sys_netlink_recv_dump);

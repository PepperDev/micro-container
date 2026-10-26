#include "net/net.h"
#include "net/netlink.h"
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>

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

static void used_veth_scan_attrs(struct nlattr *attr, int alen, const char *prefix, unsigned char *used)
{
  while (NLA_OK(attr, alen)) {
    if (attr->nla_type == IFLA_IFNAME) {
      int vid = parse_veth_id((const char *)NLA_DATA(attr), prefix);
      if (vid >= 0 && vid <= CAGE_VETH_MAX_ID)
        used[vid] = 1;
    }
    NLA_NEXT(attr, alen);
  }
}

static void veth_scan_used(const void *buf, size_t len, const char *prefix, unsigned char *used)
{
  const struct nlmsghdr *h;
  size_t rem = len;
  for (h = (const struct nlmsghdr *)buf; NLMSG_OK(h, rem); h = NLMSG_NEXT(h, rem)) {
    if (h->nlmsg_type != RTM_NEWLINK)
      continue;
    struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(h);
    used_veth_scan_attrs((struct nlattr *)((char *)ifi + NLMSG_ALIGN(sizeof(*ifi))),
                         (int)NLMSG_PAYLOAD(h, sizeof(*ifi)), prefix, used);
  }
}

int netlink_lowest_free_veth_id(const void *buf, size_t len, const char *prefix, int *id)
{
  unsigned char used[CAGE_VETH_MAX_ID + 1];
  memset(used, 0, sizeof(used));
  veth_scan_used(buf, len, prefix, used);
  for (int i = 0; i <= CAGE_VETH_MAX_ID; i++) {
    if (!used[i]) {
      *id = i;
      return 0;
    }
  }
  return -1;
}

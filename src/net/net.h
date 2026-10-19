#ifndef CAGE_NET_H
#define CAGE_NET_H

#include "net/netlink.h"
// cppcheck-suppress missingIncludeSystem
#include <stdint.h>

#define SUBNET_A 172
#define SUBNET_B 24

#define NET_IP4(a,b,c,d) ((uint32_t)(((a)<<24)|((b)<<16)|((c)<<8)|(d)))
#define NET_HOST_IP(id) NET_IP4(SUBNET_A, SUBNET_B, 0, (unsigned int)((id) * 2 + 1))
#define NET_CAGE_IP(id) NET_IP4(SUBNET_A, SUBNET_B, 0, (unsigned int)((id) * 2))

#define NET6_PREFIXLEN 127

void net6_addr(int id, int host_end, struct net6_addr *out);

int net_setup_child(int id);
int net_enable_ip_forward(void);
int net_enable_ip6_forward(void);
int net_setup_nat(void);
int net_setup_nat6(void);
int netfilter_open(int *fd);

#endif

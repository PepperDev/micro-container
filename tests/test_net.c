#include "framework.h"
#include "net/net.h"
#include "mock.h"
#include <unistd.h>
#include <errno.h>

static void test_net_setup_child_opens_netlink(void)
{
  mock_sys_reset();
  int r = net_setup_child(1);
  ASSERT_EQ(r, -1);
}

static void test_net_enable_ip_forward(void)
{
  mock_sys_reset();
  _mock_net_ip_forward_ret = 0;
  ASSERT_EQ(net_enable_ip_forward(), 0);
}

static void test_net_setup_nat_opens_socket(void)
{
  mock_sys_reset();
  int r = net_setup_nat();
  ASSERT_EQ(r, 0);
}

static void test_net_setup_child_netlink_open_fails(void)
{
  mock_sys_reset();
  _mock_netlink_open_ret = -1;
  ASSERT_EQ(net_setup_child(1), -1);
}

static void test_net_setup_nat_netfilter_open_fails(void)
{
  mock_sys_reset();
  _mock_netfilter_open_ret = -1;
  ASSERT_EQ(net_setup_nat(), -1);
}

static void test_net_setup_child_recv_dump_fails(void)
{
  mock_sys_reset();
  _mock_netlink_recv_dump_ret = -1;
  ASSERT_EQ(net_setup_child(1), -1);
}

static void test_net_setup_nat_recv_fails(void)
{
  mock_sys_reset();
  _mock_netlink_recv_ret = -1;
  ASSERT_EQ(net_setup_nat(), -1);
}

static void test_netfilter_open_real_call(void)
{
  int fd;
  ASSERT_EQ(netfilter_open(&fd), 0);
  close(fd);
}

static void test_net_setup_nat_ack_non_error(void)
{
  mock_sys_reset();
  _mock_netlink_recv_type = NLMSG_DONE;
  ASSERT_EQ(net_setup_nat(), 0);
}

static void test_net_setup_nat_eexist_is_ok(void)
{
  mock_sys_reset();
  _mock_netlink_recv_errno = -EEXIST;
  ASSERT_EQ(net_setup_nat(), 0);
}

static void test_net_setup_nat_other_error_fails(void)
{
  mock_sys_reset();
  _mock_netlink_recv_errno = -EINVAL;
  ASSERT_EQ(net_setup_nat(), -1);
}

static void test_net6_addr(void)
{
  struct net6_addr a;
  net6_addr(3, 0, &a);
  ASSERT_EQ(a.b[0], 0xfd);
  ASSERT_EQ(a.b[1], 0xca);
  ASSERT_EQ(a.b[2], 0x00);
  ASSERT_EQ(a.b[3], 0x6e);
  ASSERT_EQ(a.b[15], 6);
  net6_addr(3, 1, &a);
  ASSERT_EQ(a.b[15], 7);
  net6_addr(0, 0, &a);
  ASSERT_EQ(a.b[15], 0);
  net6_addr(0, 1, &a);
  ASSERT_EQ(a.b[15], 1);
}

static void test_net_enable_ip6_forward(void)
{
  mock_sys_reset();
  _mock_net_ip6_forward_ret = 0;
  ASSERT_EQ(net_enable_ip6_forward(), 0);
}

static void test_net_setup_nat6_opens_socket(void)
{
  mock_sys_reset();
  ASSERT_EQ(net_setup_nat6(), 0);
}

static void test_net_setup_nat6_netfilter_open_fails(void)
{
  mock_sys_reset();
  _mock_netfilter_open_ret = -1;
  ASSERT_EQ(net_setup_nat6(), -1);
}

static void test_net_setup_nat6_recv_fails(void)
{
  mock_sys_reset();
  _mock_netlink_recv_ret = -1;
  ASSERT_EQ(net_setup_nat6(), -1);
}

static void test_net_setup_nat6_eexist_is_ok(void)
{
  mock_sys_reset();
  _mock_netlink_recv_errno = -EEXIST;
  ASSERT_EQ(net_setup_nat6(), 0);
}

SUITE(net)
{
  RUN_TEST(net_setup_child_opens_netlink);
  RUN_TEST(net_enable_ip_forward);
  RUN_TEST(net_setup_nat_opens_socket);
  RUN_TEST(net_setup_child_netlink_open_fails);
  RUN_TEST(net_setup_nat_netfilter_open_fails);
  RUN_TEST(net_setup_child_recv_dump_fails);
  RUN_TEST(net_setup_nat_recv_fails);
  RUN_TEST(netfilter_open_real_call);
  RUN_TEST(net_setup_nat_ack_non_error);
  RUN_TEST(net_setup_nat_eexist_is_ok);
  RUN_TEST(net_setup_nat_other_error_fails);
  RUN_TEST(net6_addr);
  RUN_TEST(net_enable_ip6_forward);
  RUN_TEST(net_setup_nat6_opens_socket);
  RUN_TEST(net_setup_nat6_netfilter_open_fails);
  RUN_TEST(net_setup_nat6_recv_fails);
  RUN_TEST(net_setup_nat6_eexist_is_ok);
}

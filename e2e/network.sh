#!/bin/sh
# Network namespace (-N): acceptance test ported from the retired
# docs/manual-test-matrix.md. With -N cage must build a veth pair (host side
# cage-veth<N>, sandbox side eth0), give the sandbox 172.24.0.<2N>/31 on eth0
# (host end 172.24.0.<2N+1>, the same /31 block) with loopback up, a default
# route via the host, and NAT the sandbox through the host (cage-nat nftables
# table) -- see lifecycle.md §Network Namespace Setup.
#
# The veth/NAT setup itself works; the host-side checks must run while a
# session is live (teardown removes the pair), so a background holder runs
# across them.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

# Sandbox side: veth peer address, loopback, routes.
e2e_run e2e_exec cage -N -l /altroot -n n1 -u 1000 -g 1000 -- \
  /bin/sh -c '
    ip -o addr
    ip route
    ip route get 8.8.8.8'
e2e_check_rc "-N sandbox run rc" 0 "$E2E_RC"
e2e_check_contains "-N sandbox eth0 has 172.24.0.0/31" "$E2E_OUT" "172.24.0.0/31"
e2e_check_contains "-N sandbox loopback up" "$E2E_OUT" "127.0.0.1/8"
e2e_check_contains "-N sandbox default route via host" "$E2E_OUT" "default via 172.24.0.1"
e2e_check_contains "-N sandbox out-of-subnet probe uses default route" "$E2E_OUT" "8.8.8.8 via 172.24.0.1"
e2e_wait_session_gone n1

# Host side: the veth host end and the host address. The pair only exists
# while an -N session holds the sandbox netns, so run a background holder
# across these checks (veth id 0: no other session may be live).
e2e_exec_d cage -N -l /altroot -n hold -u 1000 -g 1000 -- sleep 5
hold_i=0
while [ "$hold_i" -lt 100 ]; do
  if e2e_root sh -c 'ip -o link | grep -q cage-veth0'; then
    break
  fi
  hold_i=$(( hold_i + 1 ))
  sleep 0.1
done
e2e_run e2e_root sh -c '
  ip -o link | grep -o "cage-veth0"
  ip -o addr | grep -o "172.24.0.1/31"'
e2e_check_contains "-N host veth end cage-veth0 exists" "$E2E_OUT" "cage-veth0"
e2e_check_contains "-N host address 172.24.0.1/31" "$E2E_OUT" "172.24.0.1/31"
e2e_wait_session_gone hold

# Host NAT table. nftables is not in the base image; the matrix installed it.
# Install best-effort, then check only when nft is actually available.
e2e_root apk add --no-cache nftables >/dev/null 2>&1 || :
if e2e_root sh -c 'command -v nft >/dev/null 2>&1'; then
  e2e_run e2e_root sh -c 'nft list ruleset 2>/dev/null | grep -i cage-nat'
  e2e_check_contains "-N host NAT table cage-nat exists" "$E2E_OUT" "cage-nat"
fi

# Reachability from the sandbox to the host veth end (veth id N=0 for the
# first). As root: busybox ping opens a raw ICMP socket, which the unprivileged
# user cannot (per-netns ping_group_range may exclude it). Host and sandbox are
# in the same /31 block, so the peer is on-link and reachable.
e2e_run e2e_exec cage -N -l /altroot -n n2 -u 0 -g 0 -- \
  /bin/sh -c '
    ping -c1 -W2 172.24.0.1
    printf "ping_rc=%s\n" "$?"'
e2e_check_contains "-N sandbox reaches host peer 172.24.0.1" "$E2E_OUT" "ping_rc=0"

# Idempotency: every -N run re-attempts NAT setup, but the shared cage-nat
# table is created once (EXCL; EEXIST = ok) so the masquerade rule must not be
# appended a second time. Reset the table (it normally outlives sessions), then
# create it twice and assert exactly one masquerade rule remains.
if e2e_root sh -c 'command -v nft >/dev/null 2>&1'; then
  e2e_root nft delete table ip cage-nat 2>/dev/null || :
  e2e_exec cage -N -l /altroot -n nat1 -u 1000 -g 1000 -- /bin/sh -c true
  e2e_wait_session_gone nat1
  e2e_exec cage -N -l /altroot -n nat2 -u 1000 -g 1000 -- /bin/sh -c true
  e2e_wait_session_gone nat2
  e2e_run e2e_root sh -c 'nft list chain ip cage-nat postrouting 2>/dev/null | grep -c masquerade'
  e2e_check_eq "-N NAT masquerade rule created once across runs" "1" "$E2E_OUT"
fi

# IPv6 egress. -N gives the sandbox a ULA address, a default route and an ip6
# cage-nat masquerade table, so the address check passes. The fetch only works
# when the daemon host has an IPv6 upstream; the QEMU slirp net provides none, so
# the internet check stays red there. wget prints the caller's public IPv6
# address on success.
e2e_run e2e_exec cage -N -l /altroot -n n6 -u 1000 -g 1000 -- \
  /bin/sh -c 'ip -6 -o addr show dev eth0 scope global'
e2e_check_contains "-N sandbox eth0 has a global IPv6 address" "$E2E_OUT" "inet6"
e2e_wait_session_gone n6

e2e_run e2e_exec cage -N -l /altroot -n n6e -u 1000 -g 1000 -- \
  /bin/sh -c '
    wget -qO- -T8 http://api6.ipify.org
    printf "\nwget6_rc=%s\n" "$?"'
e2e_check_contains "-N sandbox reaches IPv6 internet (api6.ipify.org)" "$E2E_OUT" "wget6_rc=0"
e2e_wait_session_gone n6e

e2e_end

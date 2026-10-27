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
# cage-nat masquerade table, so the address check passes. The internet fetch is
# gated on the daemon container's own IPv6 egress: when the container can reach
# api6, the sandbox must too and a failure is a real cage failure; when the
# container has no IPv6 egress, skip the check. wget prints the caller's public
# IPv6 address on success.
e2e_run e2e_exec cage -N -l /altroot -n n6 -u 1000 -g 1000 -- \
  /bin/sh -c 'ip -6 -o addr show dev eth0 scope global'
e2e_check_contains "-N sandbox eth0 has a global IPv6 address" "$E2E_OUT" "inet6"
e2e_wait_session_gone n6

# Enabling global IPv6 forwarding purges RA-learned default routes on
# interfaces with accept_ra=1, breaking the container's IPv6 connectivity (the
# router default routes disappear). cage must keep all/forwarding off and use
# per-interface force_forwarding instead. These paths are resolved inside the
# remote cage-t container's netns, not in the e2e runner or Docker host.
if docker exec "$CAGE_CTR" test -e /proc/sys/net/ipv6/conf/all/force_forwarding; then
  docker exec "$CAGE_CTR" sh -c 'echo 0 > /proc/sys/net/ipv6/conf/all/forwarding'
  e2e_exec cage -N -l /altroot -n n6f -u 1000 -g 1000 -- true
  e2e_wait_session_gone n6f
  e2e_run docker exec "$CAGE_CTR" sh -c '
    printf "fwd=%s all_ff=%s dflt_ff=%s eth0_ff=%s\n" \
      "$(cat /proc/sys/net/ipv6/conf/all/forwarding)" \
      "$(cat /proc/sys/net/ipv6/conf/all/force_forwarding)" \
      "$(cat /proc/sys/net/ipv6/conf/default/force_forwarding)" \
      "$(cat /proc/sys/net/ipv6/conf/eth0/force_forwarding)"'
  e2e_check_contains "-N leaves container IPv6 forwarding off" "$E2E_OUT" "fwd=0"
  e2e_check_contains "-N sets all force_forwarding" "$E2E_OUT" "all_ff=1"
  e2e_check_contains "-N sets default force_forwarding for new veths" "$E2E_OUT" "dflt_ff=1"
  e2e_check_contains "-N sets force_forwarding on existing interfaces" "$E2E_OUT" "eth0_ff=1"
else
  log "  SKIP -N IPv6 forwarding sysctl checks (kernel lacks force_forwarding)"
fi

if docker exec "$CAGE_CTR" wget -q -O /dev/null -T7 http://api6.ipify.org 2>/dev/null; then
  share=/tmp/cage-ipv6-e2e
  docker exec "$CAGE_CTR" sh -c 'rm -rf "$1"; mkdir -p "$1"; chown "$2:$3" "$1"' \
    _ "$share" "$E2E_UID" "$E2E_GID"
  e2e_exec_d cage -N -l /altroot -n n6e -u 1000 -g 1000 -v "$share:/out" -- \
    /bin/sh -c '
      wget -qO- -T8 http://api6.ipify.org > /out/result 2>&1 &
      wp=$!
      ( sleep 15; kill "$wp" 2>/dev/null ) &
      wd=$!
      wait "$wp"
      rc=$?
      kill "$wd" 2>/dev/null
      printf "\nwget6_rc=%s\n" "$rc" >> /out/result'
  i=0
  while [ "$i" -lt 300 ]; do
    if docker exec "$CAGE_CTR" grep -q 'wget6_rc=' "$share/result" 2>/dev/null; then
      break
    fi
    i=$((i + 1))
    sleep 0.1
  done
  E2E_OUT=
  i=0
  while [ "$i" -lt 20 ]; do
    E2E_OUT=$(docker exec "$CAGE_CTR" cat "$share/result" 2>/dev/null || :)
    case "$E2E_OUT" in
      *wget6_rc=*) break ;;
    esac
    i=$((i + 1))
    sleep 0.1
  done
  e2e_check_contains "-N sandbox reaches IPv6 internet (api6.ipify.org)" "$E2E_OUT" "wget6_rc=0"
  e2e_wait_session_gone n6e
  docker exec "$CAGE_CTR" rm -rf "$share" || :
else
  log "  SKIP -N sandbox reaches IPv6 internet (api6.ipify.org) (container has no IPv6 egress)"
fi

# Parallel -N: veth IDs must be sequential (+2 in IPs)
e2e_exec_d cage -N -l /altroot -n p0 -u 1000 -g 1000 -- sleep 10
e2e_exec_d cage -N -l /altroot -n p1 -u 1000 -g 1000 -- sleep 10
sleep 2
e2e_run e2e_exec cage -n p0 -- ip -o addr show dev eth0
p0ip=$(echo "$E2E_OUT" | grep -oE '172\.24\.[0-9]+\.[0-9]+/31' | head -1)
e2e_run e2e_exec cage -n p1 -- ip -o addr show dev eth0
p1ip=$(echo "$E2E_OUT" | grep -oE '172\.24\.[0-9]+\.[0-9]+/31' | head -1)
p0last=$(echo "$p0ip" | awk -F'[./]' '{print $(NF-1)}')
p1last=$(echo "$p1ip" | awk -F'[./]' '{print $(NF-1)}')
case "$p0last" in
  ''|*[!0-9]*) p0last= ;;
esac
case "$p1last" in
  ''|*[!0-9]*) p1last= ;;
esac
if [ -n "$p0last" ] && [ -n "$p1last" ] && [ $((p1last - p0last)) -eq 2 ]; then
  e2e_pass "-N parallel sessions get sequential veth IPs (+2)"
else
  e2e_fail "-N parallel sessions get sequential veth IPs (+2) (got $p0ip / $p1ip)"
fi
e2e_exec cage -k -n p0 2>/dev/null || docker exec "$CAGE_CTR" sh -c 'rm -f /run/cage/p0.pid' 2>/dev/null || :
e2e_exec cage -k -n p1 2>/dev/null || docker exec "$CAGE_CTR" sh -c 'rm -f /run/cage/p1.pid' 2>/dev/null || :
e2e_wait_session_gone p0
e2e_wait_session_gone p1
# Wait for the kernel to release the killed sessions' veth IDs so the reuse
# test below starts with IDs 0 and 1 free (rp0/rp1 then take them).
veth_i=0
while [ "$veth_i" -lt 200 ]; do
  if ! e2e_root sh -c 'ip -o link | grep -q "cage-veth"' 2>/dev/null; then
    break
  fi
  veth_i=$(( veth_i + 1 ))
  sleep 0.1
done

# Sequential -N: veth ID allocator must reuse freed IDs (same IP)
e2e_run e2e_exec cage -N -l /altroot -n s0 -u 1000 -g 1000 -- ip -o addr show dev eth0
s0ip=$(echo "$E2E_OUT" | grep -oE '172\.24\.[0-9]+\.[0-9]+/31' | head -1)
e2e_exec cage -k -n s0 2>/dev/null || docker exec "$CAGE_CTR" sh -c 'rm -f /run/cage/s0.pid' 2>/dev/null || :
e2e_wait_session_gone s0
e2e_run e2e_exec cage -N -l /altroot -n s1 -u 1000 -g 1000 -- ip -o addr show dev eth0
s1ip=$(echo "$E2E_OUT" | grep -oE '172\.24\.[0-9]+\.[0-9]+/31' | head -1)
e2e_exec cage -k -n s1 2>/dev/null || docker exec "$CAGE_CTR" sh -c 'rm -f /run/cage/s1.pid' 2>/dev/null || :
e2e_wait_session_gone s1
if [ "$s0ip" = "$s1ip" ] && [ -n "$s0ip" ]; then
  e2e_pass "-N sequential sessions reuse veth IP (same)"
else
  e2e_fail "-N sequential sessions reuse veth IP (same) (got $s0ip / $s1ip)"
fi
veth_i=0
while [ "$veth_i" -lt 200 ]; do
  if ! e2e_root sh -c 'ip -o link | grep -q "cage-veth"' 2>/dev/null; then
    break
  fi
  veth_i=$(( veth_i + 1 ))
  sleep 0.1
done

# Parallel then free first, third reuses first's IP
e2e_exec_d cage -N -l /altroot -n rp0 -u 1000 -g 1000 -- sleep 60
e2e_exec_d cage -N -l /altroot -n rp1 -u 1000 -g 1000 -- sleep 60
sleep 2
e2e_run e2e_exec cage -n rp0 -- ip -o addr show dev eth0
rip0=$(echo "$E2E_OUT" | grep -oE '172\.24\.[0-9]+\.[0-9]+/31' | head -1)
e2e_run e2e_exec cage -n rp1 -- ip -o addr show dev eth0
rip1=$(echo "$E2E_OUT" | grep -oE '172\.24\.[0-9]+\.[0-9]+/31' | head -1)
e2e_exec cage -k -n rp0 2>/dev/null || docker exec "$CAGE_CTR" sh -c 'rm -f /run/cage/rp0.pid' 2>/dev/null || :
e2e_wait_session_gone rp0
e2e_run e2e_exec cage -N -l /altroot -n rp2 -u 1000 -g 1000 -- ip -o addr show dev eth0
rip2=$(echo "$E2E_OUT" | grep -oE '172\.24\.[0-9]+\.[0-9]+/31' | head -1)
e2e_exec cage -k -n rp1 2>/dev/null || docker exec "$CAGE_CTR" sh -c 'rm -f /run/cage/rp1.pid' 2>/dev/null || :
e2e_exec cage -k -n rp2 2>/dev/null || docker exec "$CAGE_CTR" sh -c 'rm -f /run/cage/rp2.pid' 2>/dev/null || :
e2e_wait_session_gone rp1
e2e_wait_session_gone rp2
r0l=$(echo "$rip0" | awk -F'[./]' '{print $(NF-1)}')
r1l=$(echo "$rip1" | awk -F'[./]' '{print $(NF-1)}')
r2l=$(echo "$rip2" | awk -F'[./]' '{print $(NF-1)}')
case "$r0l" in ''|*[!0-9]*) r0l= ;; esac
case "$r1l" in ''|*[!0-9]*) r1l= ;; esac
case "$r2l" in ''|*[!0-9]*) r2l= ;; esac
if [ "$rip0" = "$rip2" ] && [ -n "$rip0" ] && [ -n "$r1l" ] && [ -n "$r0l" ] && [ $((r1l - r0l)) -eq 2 ]; then
  e2e_pass "-N reuse freed ID after parallel: rp2 gets same IP as rp0, rp1 is rp0+2"
else
  e2e_fail "-N reuse freed ID after parallel: rp2 gets same IP as rp0, rp1 is rp0+2 (got $rip0/$rip1/$rip2)"
fi

e2e_end

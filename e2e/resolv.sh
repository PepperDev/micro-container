#!/bin/sh
# resolv.conf strategy (lifecycle.md §Resolv.conf Strategy): acceptance test
# ported from the retired docs/manual-test-matrix.md.
#
# Without -N the host netns is shared: a regular non-empty /etc/resolv.conf is
# kept verbatim, while a symlinked or missing one is replaced with the host's
# own file. Under -N the sandbox gets cage's 8.8.8.8 tmpfs node (nlink=0) when
# the host resolver is loopback-only/empty/localhost/invalid, and the host's own
# file (same node, nlink=1) when it is a real resolver.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

restore_resolv() {
  e2e_root sh -c '
    cp -a /tmp/resolv.bak /etc/resolv.conf 2>/dev/null
    rm -f /tmp/resolv.bak'
}
e2e_root sh -c 'cp -a /etc/resolv.conf /tmp/resolv.bak'
trap restore_resolv EXIT

# --- without -N: regular file kept, symlink replaced ----------------------

# A regular non-empty resolver is kept verbatim.
e2e_root sh -c 'printf "nameserver 1.2.3.4\n" > /altroot/etc/resolv.conf'
e2e_run e2e_exec cage -l /altroot -n rsv-reg -u 1000 -g 1000 -- cat /etc/resolv.conf
e2e_check_rc "regular resolv.conf kept rc" 0 "$E2E_RC"
e2e_check_eq "regular resolv.conf kept as-is" "nameserver 1.2.3.4" "$E2E_OUT"

# A symlinked resolv.conf must be unlinked (not followed) and replaced with the
# host's own file.
host_ns=$(e2e_root sh -c "grep '^nameserver' /etc/resolv.conf | head -n1")
e2e_root sh -c '
  if rm -f /altroot/etc/resolv.conf; then
    ln -s /nonexistent/cage-resolv.conf /altroot/etc/resolv.conf
  fi'
e2e_run e2e_exec cage -l /altroot -n rsv-sym -u 1000 -g 1000 -- cat /etc/resolv.conf
e2e_check_rc "symlinked resolv.conf replaced rc" 0 "$E2E_RC"
e2e_check_contains "symlinked resolv.conf -> host resolver" "$E2E_OUT" "$host_ns"
e2e_root sh -c 'rm -f /altroot/etc/resolv.conf'

# --- under -N: guest node identity ----------------------------------------

# resolv_case <name> <host-content> <self|nlink0>: point the host resolver at
# <content>, then run a fresh -N sandbox and inspect the guest resolv.conf node.
resolv_case() {
  rv_name=$1
  rv_fmt=$2
  rv_expect=$3
  e2e_root sh -c "printf '$rv_fmt' > /etc/resolv.conf"
  if [ "$rv_expect" = self ]; then
    rv_host=$(e2e_root stat -c '%d:%i' /etc/resolv.conf)
    rv_want="$rv_host nlink=1"
  else
    rv_want="nlink=0"
  fi
  e2e_run e2e_exec cage -N -l /altroot -n "rv-$rv_name" -u 1000 -g 1000 -- \
    stat -c '%d:%i nlink=%h' /etc/resolv.conf
  e2e_check_rc "-N resolv $rv_name rc" 0 "$E2E_RC"
  e2e_check_contains "-N resolv $rv_name -> $rv_want" "$E2E_OUT" "$rv_want"
}

resolv_case loopback4 'nameserver 127.0.0.1\n' nlink0
resolv_case loopback8 'nameserver 127.0.0.53\n' nlink0
resolv_case loopback6 'nameserver ::1\n' nlink0
resolv_case localhost 'nameserver localhost\n' nlink0
resolv_case empty '' nlink0
resolv_case invalid 'garbage\n' nlink0
resolv_case real 'nameserver 10.0.2.3\n' self

e2e_end

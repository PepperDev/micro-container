#!/bin/sh
# cgroup v2 namespace isolation, no leaked cgroup after exit, and the forced
# cgroup-v1 detection branch (expected to fail on a v2-only kernel).
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

e2e_run e2e_exec cage -C -l /altroot -n c1 -- \
  /bin/sh -c '
    cat /proc/self/cgroup
    ls /sys/fs/cgroup'
e2e_check_rc "cgroup v2 run rc" 0 "$E2E_RC"
e2e_check_contains "cgroup ns shows root" "$E2E_OUT" "0::/"

e2e_run e2e_exec sh -c '
  if ! ls -d /sys/fs/cgroup/cage-* 2>/dev/null; then
    printf "NOLEAK\n"
  fi'
e2e_check_contains "no leaked cage cgroup" "$E2E_OUT" "NOLEAK"

# Forcing the v1 detection path on a v2-only kernel must fail loudly rather than
# silently misbehave.
e2e_run docker exec "$CAGE_CTR" sh -c '
  grep -v cgroup2 /proc/filesystems > /tmp/fakefs
  unshare -m sh -c "mount --bind /tmp/fakefs /proc/filesystems
cage -C -l /altroot -n v1c -- /bin/true"'
e2e_check_contains "cgroup v1 forced fails cleanly" "$E2E_OUT" "mount cgroup"

e2e_end

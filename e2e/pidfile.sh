#!/bin/sh
# -p writes a custom pidfile; -k -p tears the session down and removes it.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

e2e_exec_d sh -c \
  'setsid cage -l /altroot -n pf -p /run/cage-pf.pid -- /bin/sh -c "sleep 60" >/tmp/pf.log 2>&1 </dev/null'
sleep 2

e2e_run e2e_exec cat /run/cage-pf.pid
e2e_check "pidfile holds a pid" test -n "$E2E_OUT"

e2e_exec cage -k -p /run/cage-pf.pid || :
sleep 7

e2e_run e2e_exec sh -c '
  if ! cat /run/cage-pf.pid 2>/dev/null; then
    printf "REMOVED\n"
  fi'
e2e_check_contains "pidfile removed by -k" "$E2E_OUT" "REMOVED"

e2e_end

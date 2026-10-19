#!/bin/sh
# The temporary merged root (/tmp/cage-XXXXXX) must never outlive the child's
# entry: mid-run the pidfile exists but the temp root does not, and both are
# gone after the command finishes.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

e2e_exec_d sh -c \
  'setsid cage -l /altroot -n bg -- /bin/sh -c "sleep 5" >/tmp/bg.log 2>&1 </dev/null'
sleep 2

e2e_run e2e_exec sh -c '
  if [ -f /run/cage/bg.pid ]; then
    printf "PIDFILE\n"
  fi'
e2e_check_contains "mid-run pidfile present" "$E2E_OUT" "PIDFILE"

e2e_run e2e_exec sh -c '
  if ! ls -d /tmp/cage-?????? 2>/dev/null; then
    printf "NONE\n"
  fi'
e2e_check_contains "no temp root mid-run" "$E2E_OUT" "NONE"

sleep 6
e2e_run e2e_exec sh -c '
  if [ -e /run/cage/bg.pid ]; then
    printf "PRESENT\n"
  else
    printf "GONE\n"
  fi'
e2e_check_contains "pidfile removed after exit" "$E2E_OUT" "GONE"

e2e_run e2e_exec sh -c '
  if ! ls -d /tmp/cage-?????? 2>/dev/null; then
    printf "NONE\n"
  fi'
e2e_check_contains "no temp root after exit" "$E2E_OUT" "NONE"

e2e_end

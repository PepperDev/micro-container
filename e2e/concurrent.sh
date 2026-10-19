#!/bin/sh
# Two sessions run at once with distinct appdirs; killing one leaves the other.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

e2e_exec_d sh -c \
  'setsid cage -l /altroot -n a -u 1000 -g 1000 -- /bin/sh -c "sleep 60" >/tmp/a.log 2>&1 </dev/null'
e2e_exec_d sh -c \
  'setsid cage -l /altroot -n b -u 1000 -g 1000 -- /bin/sh -c "sleep 60" >/tmp/b.log 2>&1 </dev/null'
sleep 2

e2e_run e2e_exec sh -c 'ls -d /var/lib/cage/app-a /var/lib/cage/app-b'
e2e_check_contains "app-a exists" "$E2E_OUT" "app-a"
e2e_check_contains "app-b exists" "$E2E_OUT" "app-b"

e2e_run e2e_exec sh -c 'pgrep -x sleep | grep -vx 1 | wc -l'
e2e_check_eq "both sessions running" "2" "$(printf '%s' "$E2E_OUT" | tr -d ' ')"

e2e_exec cage -l /altroot -n a -k || :
sleep 7

e2e_run e2e_exec sh -c 'pgrep -x sleep | grep -vx 1 | wc -l'
e2e_check_eq "killing a leaves b running" "1" "$(printf '%s' "$E2E_OUT" | tr -d ' ')"

e2e_exec cage -l /altroot -n b -k || :
sleep 7

e2e_end

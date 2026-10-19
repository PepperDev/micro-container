#!/bin/sh
# Overlay on the native host lower dir (no -l): writes copy up into the appdir
# while the host files stay untouched, and the appdir persists across runs.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

docker exec "$CAGE_CTR" sh -c '
  rm -f /foo /bar
  touch /foo
  printf "a\n" > /bar'

e2e_run e2e_exec cage -n ov -- /bin/sh -c '
  cat /bar
  printf "b\n" > /bar'
e2e_check_rc "overlay run 1 rc" 0 "$E2E_RC"
e2e_check_contains "overlay run 1 sees host /bar" "$E2E_OUT" "a"

e2e_run e2e_exec cat /bar
e2e_check_eq "host /bar unchanged (copy-up)" "a" "$E2E_OUT"

e2e_run e2e_exec cat /var/lib/cage/app-ov/upper/bar
e2e_check_eq "copy-up lands in appdir upper" "b" "$E2E_OUT"

e2e_run e2e_exec cage -n ov -- cat /bar
e2e_check_eq "overlay run 2 reuses copied-up /bar" "b" "$E2E_OUT"

e2e_end

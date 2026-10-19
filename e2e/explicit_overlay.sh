#!/bin/sh
# Explicit overlay directories (-a/-U/-w) must live on a tmpfs/ext4, not the
# container's overlayfs root.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

docker exec "$CAGE_CTR" sh -c '
  rm -rf /var/lib/cage/x1
  mkdir -p /var/lib/cage/x1/upper /var/lib/cage/x1/work'
e2e_run e2e_exec cage -a /var/lib/cage/x1/app -U /var/lib/cage/x1/upper \
  -w /var/lib/cage/x1/work -l /altroot -n x1 -- /bin/sh -c '
    touch /marker
    ls /marker'
e2e_check_rc "explicit overlay rc" 0 "$E2E_RC"
e2e_check_contains "explicit overlay marker printed" "$E2E_OUT" "marker"

e2e_run e2e_exec sh -c 'ls /var/lib/cage/x1/upper/marker'
e2e_check_rc "marker landed in explicit upper dir" 0 "$E2E_RC"

e2e_end

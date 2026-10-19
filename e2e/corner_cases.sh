#!/bin/sh
# CLI corner cases: missing command, help/version, unknown option/user, and
# volume-spec handling.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

e2e_run e2e_exec cage -l /altroot -n x1c
e2e_check_rc "no command -> rc 1" 1 "$E2E_RC"
e2e_check_contains "no command message" "$E2E_OUT" "no command specified"

e2e_run e2e_exec cage -h
e2e_check_rc "-h rc 0" 0 "$E2E_RC"
e2e_run e2e_exec cage -V
e2e_check_rc "-V rc 0" 0 "$E2E_RC"

e2e_run e2e_exec cage -Z -- /bin/true
e2e_check_rc "unknown option rc 1" 1 "$E2E_RC"
e2e_check_contains "unknown option message" "$E2E_OUT" "unknown option"

e2e_run e2e_exec cage -l /altroot -n x2 -u nosuchuser -- id
e2e_check_rc "unknown user rc 0" 0 "$E2E_RC"
e2e_check_contains "unknown user falls back to nobody" "$E2E_OUT" "uid=65534"

e2e_run e2e_exec cage -l /altroot -n x3 -v :/vol -- /bin/true
e2e_check_rc "malformed volume rc 1" 1 "$E2E_RC"
e2e_check_contains "malformed volume message" "$E2E_OUT" "invalid volume spec"

docker exec "$CAGE_CTR" sh -c 'rm -rf /bare'
e2e_run e2e_exec cage -l /altroot -n x4 -v /bare -- /bin/true
e2e_check_rc "colon-less volume self-map rc 0" 0 "$E2E_RC"
e2e_run e2e_exec stat -c '%F' /bare
e2e_check_eq "colon-less volume created host dir" "directory" "$E2E_OUT"

e2e_run e2e_exec cage -l /altroot -n x5 --
e2e_check_rc "bare -- runs the login shell" 0 "$E2E_RC"

e2e_end

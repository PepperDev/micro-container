#!/bin/sh
# -c changes the guest working directory.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

e2e_run e2e_exec cage -l /altroot -n i6 -c /etc -- pwd
e2e_check_rc "cwd run rc" 0 "$E2E_RC"
e2e_check_eq "cwd is /etc" "/etc" "$E2E_OUT"

e2e_end

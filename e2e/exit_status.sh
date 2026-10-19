#!/bin/sh
# The guest command's exit status is forwarded verbatim.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

e2e_run e2e_exec cage -l /altroot -n i7 -u 1000 -g 1000 -- /bin/sh -c 'exit 42'
e2e_check_rc "exit 42 forwarded" 42 "$E2E_RC"

e2e_run e2e_exec cage -l /altroot -n i7b -- /bin/sh -c 'exit 0'
e2e_check_rc "exit 0 forwarded" 0 "$E2E_RC"

e2e_run e2e_exec cage -l /altroot -n i7c -- /bin/sh -c 'exit 127'
e2e_check_rc "exit 127 forwarded" 127 "$E2E_RC"

e2e_end

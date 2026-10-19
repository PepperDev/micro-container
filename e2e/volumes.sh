#!/bin/sh
# Volume bindings: basic mapping, auto-creation of missing host/guest paths,
# file vs directory typing, and a recursive whole-filesystem bind.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

docker exec "$CAGE_CTR" sh -c '
  mkdir -p /hostvol &&
    printf "DATA\n" > /hostvol/f'
e2e_run e2e_exec cage -l /altroot -n v1 -u 1000 -g 1000 -v /hostvol:/vol -- cat /vol/f
e2e_check_eq "v1 volume data visible" "DATA" "$E2E_OUT"

docker exec "$CAGE_CTR" sh -c 'rm -rf /nope'
e2e_run e2e_exec cage -l /altroot -n v2 -v /nope:/vol -- stat -c '%F' /vol
e2e_check_eq "v2 missing host path -> guest directory" "directory" "$E2E_OUT"
e2e_run e2e_exec stat -c '%F %a' /nope
e2e_check_contains "v2 host path auto-created 0755" "$E2E_OUT" "directory 755"

docker exec "$CAGE_CTR" sh -c '
  rm -rf /tmp/dv /tmp/foo
  mkdir /tmp/foo'
e2e_run e2e_exec cage -l /altroot -n v3 -v /tmp/foo -- stat -c '%F' /tmp/foo
e2e_check_eq "v3 host dir -> guest directory" "directory" "$E2E_OUT"

docker exec "$CAGE_CTR" sh -c '
  rm -f /tmp/bar
  touch /tmp/bar'
e2e_run e2e_exec cage -l /altroot -n v4 -v /tmp/bar -- stat -c '%F' /tmp/bar
e2e_check_contains "v4 host file -> guest regular file" "$E2E_OUT" "regular"

# Recursive bind of /`: the guest /proc belongs to the sandbox pid namespace
# while /host/proc is the container's.
# shellcheck disable=SC2016 # $(sed ...) runs in the guest, not the host
e2e_run e2e_exec cage -n vol -v /:/host -- /bin/sh -c '
  printf "guest=%s\n" "$(sed -n "s/^Pid://p" /proc/self/status)"
  printf "host=%s\n" "$(sed -n "s/^Pid://p" /host/proc/self/status)"'
gp=$(printf '%s\n' "$E2E_OUT" | sed -n 's/^guest=//p' | tr -d ' \t')
hp=$(printf '%s\n' "$E2E_OUT" | sed -n 's/^host=//p' | tr -d ' \t')
e2e_check_ne "recursive bind: guest/host pids differ" "$gp" "$hp"
e2e_run e2e_exec cage -n vol -v /:/host -- test -e /host/proc/1
e2e_check_rc "recursive bind: host /proc visible" 0 "$E2E_RC"

e2e_end

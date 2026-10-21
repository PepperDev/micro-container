#!/bin/sh
# Init script resolution: a path inside the guest root, and an already-open
# file descriptor (/proc/self/fd/N) including when the command also reads its
# own stdin.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

docker exec "$CAGE_CTR" sh -c 'printf "id > /init.out\n" > /altroot/init.sh'
e2e_run e2e_exec cage -l /altroot -n s1 -i /init.sh -- cat /init.out
e2e_check_rc "s1 init path rc" 0 "$E2E_RC"
e2e_check_contains "s1 init ran as root" "$E2E_OUT" "uid=0(root)"

# Init script on fd 3 (script resolved before chroot, reopened after chroot).
e2e_run e2e_exec_i sh <<'REMOTE'
cage -l /altroot -n fd1 -i /proc/self/fd/3 -- /bin/sh -c 'cat /init.out' 3<<INIT
printf 'INIT_RAN\n' > /init.out
INIT
REMOTE
e2e_check_rc "fd1 init from fd rc" 0 "$E2E_RC"
e2e_check_contains "fd1 init from fd 3 ran" "$E2E_OUT" "INIT_RAN"

# Mixed: init on fd 3, command `sh -es` reads its script from stdin (fd 0).
e2e_run e2e_exec_i sh <<'REMOTE'
cage -l /altroot -n fd2 -i /proc/self/fd/3 -- sh -es 3<<INIT <<CMD
printf 'INIT_RAN\n' > /init.out
INIT
printf 'CMD_LINE1\n'
cat /init.out
CMD
REMOTE
e2e_check_rc "fd2 mixed rc" 0 "$E2E_RC"
e2e_check_contains "fd2 command read its own stdin" "$E2E_OUT" "CMD_LINE1"
e2e_check_contains "fd2 init ran from fd 3" "$E2E_OUT" "INIT_RAN"

# Init script exits non-zero: cage must fail and must not run the command.
docker exec "$CAGE_CTR" sh -c 'printf "exit 7\n" > /altroot/init_fail.sh'
e2e_run e2e_exec cage -l /altroot -n s2 -i /init_fail.sh -- sh -c 'echo SHOULD_NOT_RUN'
e2e_check_rc "s2 init fail rc" 1 "$E2E_RC"
if echo "$E2E_OUT" | grep -q "SHOULD_NOT_RUN"; then
  echo "FAIL: command ran after init script failure" >&2
  exit 1
fi

e2e_end

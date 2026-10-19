#!/bin/sh
# Re-entry: a second run with the same -n joins the live session. Spec
# (lifecycle.md §Re-entry Flow rule 5) requires the re-entry to inherit the
# running instance's GUI env (read from /proc/<pid>/environ), ignoring the
# caller's host DISPLAY/XDG_RUNTIME_DIR, and to not re-create GUI mounts/ACLs.
# Also covers plain re-entry with -e overrides and -k teardown.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

if ! e2e_socket_up x11 /tmp/.X11-unix/X0; then
  e2e_fail_fast "socat X11 socket did not start"
fi
docker exec "$CAGE_CTR" sh -c '
  if mkdir -p /srv; then
    chmod 777 /srv
  fi'

# First entry: record the GUI env, then stay alive.
e2e_exec_d sh -c 'setsid env XDG_RUNTIME_DIR=/run/user/1000 DISPLAY=:0 \
  cage -G -l /altroot -n g9 -u 1000 -g 1000 -v /srv:/srv \
  -- /bin/sh -c "
    env | grep -E \"^(DISPLAY|XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\" | sort > /srv/orig.env
    sleep 120" \
  >/tmp/g9.log 2>&1 </dev/null'
sleep 2

e2e_run e2e_exec cat /srv/orig.env
e2e_check_contains "first -G entry DISPLAY=:0" "$E2E_OUT" "DISPLAY=:0"
e2e_check_not_contains "first -G entry sets no XDG_RUNTIME_DIR (X11-only)" "$E2E_OUT" "XDG_RUNTIME_DIR"

# Rule 5: re-entry inherits the instance's GUI env, not the caller's. The
# re-entry advertises a different DISPLAY and XDG_RUNTIME_DIR; the instance's
# values (DISPLAY=:0, no XDG_RUNTIME_DIR) must win.
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/2000 DISPLAY=:2 cage -G -l /altroot \
  -n g9 -u 1000 -g 1000 -v /srv:/srv \
  -- /bin/sh -c "env | grep -E \"^(DISPLAY|XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\" | sort > /srv/re.env"'
e2e_check_rc "re-entry rc" 0 "$E2E_RC"
e2e_run e2e_exec diff /srv/orig.env /srv/re.env
e2e_check_rc "re-entry GUI env inherited from instance (caller DISPLAY ignored)" 0 "$E2E_RC"

# The re-entry need not set DISPLAY at all; the instance's value still applies.
e2e_run e2e_exec sh -c 'env -u DISPLAY -u XDG_RUNTIME_DIR cage -G -l /altroot \
  -n g9 -u 1000 -g 1000 -v /srv:/srv \
  -- /bin/sh -c "env | grep -E \"^(DISPLAY|XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\" | sort > /srv/re2.env"'
e2e_check_rc "re-entry without DISPLAY rc" 0 "$E2E_RC"
e2e_run e2e_exec diff /srv/orig.env /srv/re2.env
e2e_check_rc "re-entry GUI env inherited when caller DISPLAY unset" 0 "$E2E_RC"

# Rule 5: re-entry must not stack GUI mounts. Count them in the session's own
# mount namespace via the pidfile pid before and after another re-entry. Fail
# loudly if the pidfile is unreadable rather than comparing two empty strings.
# shellcheck disable=SC2016 # $p and $(cat) run in the container, not the host
x11_mounts() {
  e2e_exec sh -c '
    if ! p=$(cat /run/cage/g9.pid 2>/dev/null); then
      exit 1
    fi
    if [ -z "$p" ]; then
      exit 1
    fi
    grep -c "/tmp/.X11-unix" "/proc/$p/mountinfo" || :'
}
before=$(x11_mounts)
e2e_check "g9 session has an X11 mount before re-entry" test "${before:--1}" -ge 1
e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 DISPLAY=:0 cage -G -l /altroot \
  -n g9 -u 1000 -g 1000 -- /bin/true'
after=$(x11_mounts)
e2e_check_eq "re-entry does not re-create X11 mounts" "$before" "$after"

e2e_exec cage -k -n g9 || :
sleep 7

# Plain re-entry: -e overrides apply on re-entry.
e2e_exec_d sh -c 'setsid cage -l /altroot -n demo -u 1000 -g 1000 -e FOO=x \
  -- /bin/sh -c "sleep 120" >/tmp/demo.log 2>&1 </dev/null'
sleep 2
# shellcheck disable=SC2016 # \$FOO is expanded by the guest shell, not the host
e2e_run e2e_exec sh -c 'cage -l /altroot -n demo -u 1000 -g 1000 -e FOO=y \
  -- /bin/sh -c "printf \"FOO=%s\\n\" \"\$FOO\""'
e2e_check_contains "re-entry -e override applied" "$E2E_OUT" "FOO=y"

e2e_exec cage -l /altroot -n demo -k || :
sleep 7
e2e_run e2e_exec sh -c '
  if ! command -v pgrep >/dev/null 2>&1; then
    printf "PGREP_MISSING\n"
    exit 0
  fi
  pgrep -x sleep > /tmp/sl.txt
  if [ "$?" -gt 1 ]; then
    printf "PGREP_FAILED\n"
    exit 0
  fi
  if grep -vx 1 /tmp/sl.txt >/dev/null; then
    printf "ALIVE\n"
  else
    printf "GONE\n"
  fi'
e2e_check_contains "re-entry session torn down by -k" "$E2E_OUT" "GONE"

e2e_socket_down x11 /tmp/.X11-unix/X0
e2e_end

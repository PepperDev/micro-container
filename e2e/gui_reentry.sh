#!/bin/sh
# GUI re-entry (-G joining a live instance): the guest GUI environment must be
# inherited from the original first run (read from /proc/<pid>/environ), not
# recomputed from the re-entry's host env, uid, or guest user. Only explicit
# `-e` flags on the re-entry may change the result.
#
# This duplicates the Wayland/Pulse env-mapping coverage of e2e/gui_wayland.sh
# and e2e/pulse.sh for the re-entry path. Directory ownership is intentionally
# not checked here: a re-entry creates nothing (mounts/ACLs/dirs belong to the
# first entry).
#
# Intended behavior: docs/lifecycle.md §GUI Setup (re-entry note) and §Re-entry
# Flow rule 5.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

# GUI env keys cage may set/forward (mirrors key_is_gui in src/util/env.c).
GUIKEYS='DISPLAY|XDG_RUNTIME_DIR|WAYLAND_DISPLAY|PULSE_SERVER|PULSE_COOKIE|XDG_SESSION_TYPE|XDG_CURRENT_DESKTOP|GDK_BACKEND|QT_QPA_PLATFORM|SDL_VIDEODRIVER|CLUTTER_BACKEND|MOZ_ENABLE_WAYLAND|_JAVA_AWT_WM_NONREPARENTING'

wait_file() {
  wf_i=0
  while [ "$wf_i" -lt 100 ]; do
    if e2e_exec test -s "$1" 2>/dev/null; then
      return 0
    fi
    wf_i=$(( wf_i + 1 ))
    sleep 0.1
  done
  return 1
}

# Host GUI fixtures: X11, Wayland and Pulse sockets under the first run's host
# XDG_RUNTIME_DIR, plus a shared volume for the guest to record its env.
if ! e2e_socket_up x11 /tmp/.X11-unix/X0; then
  e2e_fail_fast "socat X11 socket did not start"
fi
if ! e2e_socket_up wayland /run/user/1000/wayland-0; then
  e2e_fail_fast "socat Wayland socket did not start"
fi
if ! e2e_socket_up pulse /run/user/1000/pulse/native; then
  e2e_fail_fast "socat Pulse socket did not start"
fi
e2e_root sh -c '
  if mkdir -p /srv; then
    chmod 777 /srv
  fi'

# --- first entry: record the instance GUI env and stay alive --------------

e2e_exec_d sh -c "$(
  cat <<EOF
setsid env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/1000 \
  WAYLAND_DISPLAY=wayland-0 cage -G -l /altroot -n gre -u 1000 -g 1000 \
  -v /srv:/srv -e PULSE_SERVER=unix:/tmp/gpsock -e PULSE_COOKIE=/tmp/gcookie \
  -- /bin/sh -c "env | grep -E \\"^($GUIKEYS)=\\" | sort > /srv/orig.env
sleep 300" \
  >/tmp/gre.log 2>&1 </dev/null
EOF
)"

# gre1: the baseline instance env.
if ! e2e_check "gre1 first entry wrote instance GUI env" wait_file /srv/orig.env; then
  e2e_exec cat /tmp/gre.log >&2 || :
fi
e2e_run e2e_exec cat /srv/orig.env
e2e_check_contains "gre1 instance DISPLAY from host" "$E2E_OUT" "DISPLAY=:0"
e2e_check_contains "gre1 instance XDG_RUNTIME_DIR from host" "$E2E_OUT" "XDG_RUNTIME_DIR=/run/user/1000"
e2e_check_contains "gre1 instance WAYLAND_DISPLAY from host" "$E2E_OUT" "WAYLAND_DISPLAY=wayland-0"
e2e_check_contains "gre1 instance Wayland detected" "$E2E_OUT" "XDG_SESSION_TYPE=wayland"
e2e_check_contains "gre1 instance PULSE_SERVER from -e" "$E2E_OUT" "PULSE_SERVER=unix:/tmp/gpsock"
e2e_check_contains "gre1 instance PULSE_COOKIE from -e" "$E2E_OUT" "PULSE_COOKIE=/tmp/gcookie"

# gre2: re-entry with conflicting host env (same guest uid) must match exactly.
e2e_run e2e_exec sh -c "$(
  cat <<EOF
env DISPLAY=:9 XDG_RUNTIME_DIR=/run/user/2000 \
  WAYLAND_DISPLAY=wayland-9 PULSE_SERVER=unix:/nope PULSE_COOKIE=/nope/cookie \
  SUDO_UID=2000 HOME=/home/e2e cage -G -l /altroot -n gre -u 1000 -g 1000 \
  -v /srv:/srv -- /bin/sh -c "env | grep -E \\"^($GUIKEYS)=\\" | sort > /srv/re.env"
EOF
)"
e2e_check_rc "gre2 re-entry rc" 0 "$E2E_RC"
e2e_run e2e_exec diff /srv/orig.env /srv/re.env
e2e_check_rc "gre2 re-entry GUI env inherits instance (host env ignored)" 0 "$E2E_RC"

# gre3: re-entry with host GUI env unset must still match exactly.
e2e_run e2e_exec sh -c "$(
  cat <<EOF
env -u DISPLAY -u XDG_RUNTIME_DIR -u WAYLAND_DISPLAY \
  -u PULSE_SERVER -u PULSE_COOKIE -u SUDO_UID cage -G -l /altroot -n gre \
  -u 1000 -g 1000 -v /srv:/srv \
  -- /bin/sh -c "env | grep -E \\"^($GUIKEYS)=\\" | sort > /srv/re_unset.env"
EOF
)"
e2e_check_rc "gre3 re-entry rc" 0 "$E2E_RC"
e2e_run e2e_exec diff /srv/orig.env /srv/re_unset.env
e2e_check_rc "gre3 re-entry GUI env inherits when host env unset" 0 "$E2E_RC"

# gre4: re-entry as a different host uid must match exactly (guest uid same).
# `id -u` runs in the container shell, so it reports the invoking host uid.
e2e_run docker exec -u 1002:1002 "$CAGE_CTR" sh -c "$(
  cat <<EOF
id -u
env DISPLAY=:7 XDG_RUNTIME_DIR=/run/user/1002 WAYLAND_DISPLAY=wayland-7 \
  PULSE_SERVER=unix:/h2 SUDO_UID=1002 cage -G -l /altroot -n gre -u 1000 -g 1000 \
  -v /srv:/srv -- /bin/sh -c "env | grep -E \\"^($GUIKEYS)=\\" | sort > /srv/re_uid.env"
EOF
)"
e2e_check_rc "gre4 re-entry rc" 0 "$E2E_RC"
e2e_check_contains "gre4 re-entry invoked as its own host uid" "$E2E_OUT" "1002"
e2e_run e2e_exec diff /srv/orig.env /srv/re_uid.env
e2e_check_rc "gre4 re-entry GUI env inherits across differing host uid" 0 "$E2E_RC"

# gre5: re-entry with a different guest user must match exactly (GUI env does
# not depend on the re-entry user).
e2e_run e2e_exec sh -c "$(
  cat <<EOF
env DISPLAY=:3 XDG_RUNTIME_DIR=/run/user/2000 cage -G -l /altroot -n gre \
  -u 2000 -g 2000 -v /srv:/srv \
  -- /bin/sh -c "id -u
env | grep -E \\"^($GUIKEYS)=\\" | sort > /srv/re_gu.env"
EOF
)"
e2e_check_rc "gre5 re-entry rc" 0 "$E2E_RC"
e2e_check_contains "gre5 re-entry runs as requested guest uid" "$E2E_OUT" "2000"
e2e_run e2e_exec diff /srv/orig.env /srv/re_gu.env
e2e_check_rc "gre5 re-entry GUI env inherits across differing guest user" 0 "$E2E_RC"

# gre6: explicit -e on the re-entry overrides the inherited value; every other
# GUI var is still the instance's.
e2e_run e2e_exec sh -c "$(
  cat <<EOF
env DISPLAY=:9 XDG_RUNTIME_DIR=/run/user/9999 cage -G -l /altroot -n gre \
  -u 1000 -g 1000 \
  -e XDG_RUNTIME_DIR=/other -e DISPLAY=:42 -e PULSE_SERVER=unix:/ov -e FOO=bar \
  -- /bin/sh -c "env | grep -E \\"^($GUIKEYS|FOO)=\\" | sort"
EOF
)"
e2e_check_rc "gre6 re-entry rc" 0 "$E2E_RC"
e2e_check_contains "gre6 -e XDG_RUNTIME_DIR overrides inherited" "$E2E_OUT" "XDG_RUNTIME_DIR=/other"
e2e_check_contains "gre6 -e DISPLAY overrides inherited" "$E2E_OUT" "DISPLAY=:42"
e2e_check_contains "gre6 -e PULSE_SERVER overrides inherited" "$E2E_OUT" "PULSE_SERVER=unix:/ov"
e2e_check_contains "gre6 custom -e exported" "$E2E_OUT" "FOO=bar"
e2e_check_contains "gre6 non-overridden WAYLAND_DISPLAY still inherited" "$E2E_OUT" "WAYLAND_DISPLAY=wayland-0"
e2e_check_contains "gre6 non-overridden PULSE_COOKIE still inherited" "$E2E_OUT" "PULSE_COOKIE=/tmp/gcookie"
e2e_check_not_contains "gre6 re-entry host XDG_RUNTIME_DIR ignored" "$E2E_OUT" "/run/user/9999"
e2e_check_not_contains "gre6 re-entry host DISPLAY ignored" "$E2E_OUT" "DISPLAY=:9"

# gre7: overriding only PULSE_COOKIE leaves PULSE_SERVER inherited.
e2e_run e2e_exec sh -c "$(
  cat <<EOF
env -u PULSE_COOKIE -u PULSE_SERVER cage -G -l /altroot -n gre -u 1000 -g 1000 \
  -e PULSE_COOKIE=/ovcookie \
  -- /bin/sh -c "env | grep -E \\"^(PULSE_COOKIE|PULSE_SERVER)=\\" | sort"
EOF
)"
e2e_check_rc "gre7 re-entry rc" 0 "$E2E_RC"
e2e_check_contains "gre7 -e PULSE_COOKIE overrides inherited" "$E2E_OUT" "PULSE_COOKIE=/ovcookie"
e2e_check_contains "gre7 PULSE_SERVER still inherited" "$E2E_OUT" "PULSE_SERVER=unix:/tmp/gpsock"

# gre8: re-entry without -G does not read the instance env; with the host GUI
# env unset the guest sees no GUI variables at all.
e2e_run e2e_exec sh -c "$(
  cat <<EOF
env -u DISPLAY -u XDG_RUNTIME_DIR -u WAYLAND_DISPLAY \
  -u PULSE_SERVER -u PULSE_COOKIE -u SUDO_UID cage -l /altroot -n gre \
  -u 1000 -g 1000 -- /bin/sh -c "env | grep -E \\"^($GUIKEYS)=\\" | sort"
EOF
)"
e2e_check_rc "gre8 plain re-entry rc" 0 "$E2E_RC"
e2e_check_eq "gre8 plain re-entry does not read instance GUI env" "" "$E2E_OUT"

e2e_exec cage -k -n gre || :
e2e_wait_session_gone gre || :
e2e_socket_down x11 /tmp/.X11-unix/X0
e2e_socket_down wayland /run/user/1000/wayland-0
e2e_socket_down pulse /run/user/1000/pulse/native
e2e_end

#!/bin/sh
# GUI X11: host DISPLAY passed through as-is, socket owner ignored, -e wins,
# and DISPLAY discovery from /tmp/.X11-unix when the host DISPLAY is unset.
# Uses a socat-simulated X11 socket (no Xvfb).
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

if ! e2e_socket_up x11 /tmp/.X11-unix/X0; then
  e2e_fail_fast "socat X11 socket did not start"
fi

e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 DISPLAY=:0 cage -G -l /altroot \
  -n g1 -u 1000 -g 1000 -- sh -c "env | sort | grep -E \"^(DISPLAY|XDG_RUNTIME_DIR|WAYLAND_DISPLAY|PULSE_SERVER)=\""'
e2e_check_rc "g1 rc" 0 "$E2E_RC"
e2e_check_eq "g1 host DISPLAY passed through; no XDG for pure X11" "DISPLAY=:0" "$E2E_OUT"

docker exec "$CAGE_CTR" chown 4321:4321 /tmp/.X11-unix/X0
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 DISPLAY=:0 cage -G -l /altroot \
  -n g4 -u 1000 -g 1000 -- sh -c "env | grep ^DISPLAY="'
e2e_check_contains "g4 socket owned by another user is ignored" "$E2E_OUT" "DISPLAY=:0"
docker exec "$CAGE_CTR" chown 0:0 /tmp/.X11-unix/X0

e2e_run e2e_exec sh -c 'DISPLAY=:0 cage -G -l /altroot -n g5 -u 1000 -g 1000 \
  -e DISPLAY=:2 -e XDG_RUNTIME_DIR=/custom -e WAYLAND_DISPLAY=w2 -e PULSE_SERVER=unix:/x/p \
  -- sh -c "env | sort | grep -E \"^(DISPLAY|XDG_RUNTIME_DIR|WAYLAND_DISPLAY|PULSE_SERVER)=\""'
e2e_check_contains "g5 -e DISPLAY wins" "$E2E_OUT" "DISPLAY=:2"
e2e_check_contains "g5 -e XDG_RUNTIME_DIR wins" "$E2E_OUT" "XDG_RUNTIME_DIR=/custom"
e2e_check_contains "g5 -e WAYLAND_DISPLAY wins" "$E2E_OUT" "WAYLAND_DISPLAY=w2"
e2e_check_contains "g5 -e PULSE_SERVER wins" "$E2E_OUT" "PULSE_SERVER=unix:/x/p"

e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 env -u DISPLAY -u WAYLAND_DISPLAY \
  cage -G -l /altroot -n g6 -u 1000 -g 1000 \
  -- sh -c "env | sort | grep -E \"^(DISPLAY|XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\""'
e2e_check_eq "g6 discovers lowest X node as :0.0" "DISPLAY=:0.0" "$E2E_OUT"

e2e_socket_down x11 /tmp/.X11-unix/X0
e2e_end

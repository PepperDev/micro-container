#!/bin/sh
# Socket ACL access: cage grants the guest uid rw- through a POSIX ACL on the
# host socket. Verified two ways: getfacl from the container, and an actual
# connect+write from inside the cage (socat) whose message must reach the socat
# server on the host side of the bind mount.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

# socat for the in-guest client, installed through cage into this instance's
# overlay only (/var/lib/cage/app-gacl/upper/...).
e2e_guest_install gacl socat

# ---- X11 ----
if ! e2e_socket_up x11 /tmp/.X11-unix/X0; then
  e2e_fail_fast "x11 socket did not start"
fi
docker exec "$CAGE_CTR" sh -c '
  chown 0:0 /tmp/.X11-unix/X0
  chmod 755 /tmp/.X11-unix/X0
  setfacl -b /tmp/.X11-unix/X0'
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 DISPLAY=:0 cage -G -l /altroot \
  -n gacl -u 1000 -g 1000 -- sh -c "printf PING-X11 | socat - UNIX-CONNECT:/tmp/.X11-unix/X0"'
e2e_check_rc "x11: in-guest connect via -G rc" 0 "$E2E_RC"
sleep 0.3
e2e_run e2e_socket_recv x11
e2e_check_contains "x11: message reached server (ACL granted)" "$E2E_OUT" "PING-X11"
e2e_run e2e_exec getfacl -n /tmp/.X11-unix/X0
e2e_check_contains "x11: getfacl shows user:1000:rw-" "$E2E_OUT" "user:1000:rw-"
e2e_socket_down x11 /tmp/.X11-unix/X0

# ---- WAYLAND ----
if ! e2e_socket_up wayland /run/user/1000/wayland-0; then
  e2e_fail_fast "wayland socket did not start"
fi
docker exec "$CAGE_CTR" sh -c '
  chown 0:0 /run/user/1000/wayland-0
  chmod 755 /run/user/1000/wayland-0
  setfacl -b /run/user/1000/wayland-0'
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 cage -G -l /altroot \
  -n gacl -u 1000 -g 1000 -- sh -c "printf PING-WL | socat - UNIX-CONNECT:/run/user/1000/wayland-0"'
e2e_check_rc "wayland: in-guest connect via -G rc" 0 "$E2E_RC"
sleep 0.3
e2e_run e2e_socket_recv wayland
e2e_check_contains "wayland: message reached server (ACL granted)" "$E2E_OUT" "PING-WL"
e2e_run e2e_exec getfacl -n /run/user/1000/wayland-0
e2e_check_contains "wayland: getfacl shows user:1000:rw-" "$E2E_OUT" "user:1000:rw-"
e2e_socket_down wayland /run/user/1000/wayland-0

# ---- PULSE ----
if ! e2e_socket_up pulse /run/user/1000/pulse/native; then
  e2e_fail_fast "pulse socket did not start"
fi
docker exec "$CAGE_CTR" sh -c '
  chown 0:0 /run/user/1000/pulse/native
  chmod 755 /run/user/1000/pulse/native
  setfacl -b /run/user/1000/pulse/native'
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot \
  -n gacl -u 1000 -g 1000 -- sh -c "printf PING-PULSE | socat - UNIX-CONNECT:/run/user/1000/pulse/native"'
e2e_check_rc "pulse: in-guest connect via -G rc" 0 "$E2E_RC"
sleep 0.3
e2e_run e2e_socket_recv pulse
e2e_check_contains "pulse: message reached server (ACL granted)" "$E2E_OUT" "PING-PULSE"
e2e_run e2e_exec getfacl -n /run/user/1000/pulse/native
e2e_check_contains "pulse: getfacl shows user:1000:rw-" "$E2E_OUT" "user:1000:rw-"
e2e_socket_down pulse /run/user/1000/pulse/native

e2e_end

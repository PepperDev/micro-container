#!/bin/sh
# GUI Wayland socket + runtime-dir resolution.
#
# Covers host XDG_RUNTIME_DIR / WAYLAND_DISPLAY / SUDO_UID resolution and guest
# -e overrides, and asserts the host socket is bind-mounted at the guest path by
# checking the target is a socket (cage touches a regular file first, so a
# socket can only be the bind mount).
#
# Intended behavior (docs/lifecycle.md §GUI Setup): host XDG_RUNTIME_DIR falls
# back to /run/user/${SUDO_UID:-<real uid>} when empty or unset; guest
# XDG_RUNTIME_DIR defaults to /run/user/<uid> unless overridden by -e.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

# Host sockets: the default runtime dir and a second one for the SUDO_UID
# fallback. /run/user/<n> is root-owned, so create the extra dir as root.
if ! e2e_socket_up wayland /run/user/1000/wayland-0; then
  e2e_fail_fast "socat wayland socket did not start"
fi
docker exec "$CAGE_CTR" sh -c '
  rm -rf /run/user/2000
  mkdir -p /run/user/2000 &&
    chown 1000:1000 /run/user/2000'
if ! e2e_socket_up wayland2 /run/user/2000/wayland-0; then
  e2e_fail_fast "socat wayland2 socket did not start"
fi

# wy1: host XDG_RUNTIME_DIR and WAYLAND_DISPLAY both set, guest -e overrides both.
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
  cage -G -l /altroot -n wy1 -u 1000 -g 1000 \
  -e XDG_RUNTIME_DIR=/gxrd1 -e WAYLAND_DISPLAY=w2 -- sh -c "
    env | sort | grep -E \"^(XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\"
    stat -c \"%n %u:%g %a\" /gxrd1
    if [ -S /gxrd1/w2 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "wy1 rc" 0 "$E2E_RC"
e2e_check_contains "wy1 guest XDG_RUNTIME_DIR override" "$E2E_OUT" "XDG_RUNTIME_DIR=/gxrd1"
e2e_check_contains "wy1 guest WAYLAND_DISPLAY override" "$E2E_OUT" "WAYLAND_DISPLAY=w2"
e2e_check_contains "wy1 guest XDG dir owned by guest uid 0700" "$E2E_OUT" "/gxrd1 1000:1000 700"
e2e_check_contains "wy1 host socket mapped at guest -e path" "$E2E_OUT" "SOCK-OK"

# wy2: host XDG_RUNTIME_DIR unset, SUDO_UID (2000) differs from the real uid
# (1000); resolver must fall back to /run/user/2000.
e2e_run e2e_exec sh -c 'env -u XDG_RUNTIME_DIR SUDO_UID=2000 WAYLAND_DISPLAY=wayland-0 \
  cage -G -l /altroot -n wy2 -u 1000 -g 1000 \
  -e XDG_RUNTIME_DIR=/gxrd2 -e WAYLAND_DISPLAY=w3 -- sh -c "
    env | sort | grep -E \"^(XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\"
    if [ -S /gxrd2/w3 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "wy2 rc" 0 "$E2E_RC"
e2e_check_contains "wy2 guest XDG_RUNTIME_DIR override" "$E2E_OUT" "XDG_RUNTIME_DIR=/gxrd2"
e2e_check_contains "wy2 SUDO_UID fallback socket mapped" "$E2E_OUT" "SOCK-OK"

# wy3: host XDG_RUNTIME_DIR empty (not unset), SUDO_UID (2000) differs from the
# real uid (1000); spec says empty resolves like unset -> /run/user/2000.
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR= SUDO_UID=2000 WAYLAND_DISPLAY=wayland-0 \
  cage -G -l /altroot -n wy3 -u 1000 -g 1000 \
  -e XDG_RUNTIME_DIR=/gxrd3 -e WAYLAND_DISPLAY=w4 -- sh -c "
    env | sort | grep -E \"^(XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\"
    if [ -S /gxrd3/w4 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "wy3 rc" 0 "$E2E_RC"
e2e_check_contains "wy3 empty host XDG_RUNTIME_DIR falls back to SUDO_UID" "$E2E_OUT" "SOCK-OK"

# wy4: host XDG_RUNTIME_DIR and SUDO_UID both unset; cage is setuid (effective
# uid 0 != real uid 1000), so fall back to the real uid /run/user/1000.
e2e_run e2e_exec sh -c 'env -u XDG_RUNTIME_DIR -u SUDO_UID WAYLAND_DISPLAY=wayland-0 \
  cage -G -l /altroot -n wy4 -u 1000 -g 1000 \
  -e XDG_RUNTIME_DIR=/gxrd4 -e WAYLAND_DISPLAY=w5 -- sh -c "
    env | sort | grep -E \"^(XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\"
    if [ -S /gxrd4/w5 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "wy4 rc" 0 "$E2E_RC"
e2e_check_contains "wy4 real-uid fallback socket mapped" "$E2E_OUT" "SOCK-OK"

# wy5: host XDG_RUNTIME_DIR set, WAYLAND_DISPLAY unset -> default wayland-0.
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 env -u WAYLAND_DISPLAY \
  cage -G -l /altroot -n wy5 -u 1000 -g 1000 \
  -e XDG_RUNTIME_DIR=/gxrd5 -e WAYLAND_DISPLAY=w6 -- sh -c "
    env | sort | grep -E \"^(XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\"
    if [ -S /gxrd5/w6 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "wy5 rc" 0 "$E2E_RC"
e2e_check_contains "wy5 default host WAYLAND_DISPLAY socket mapped" "$E2E_OUT" "SOCK-OK"

# wy6: no guest override; guest XDG_RUNTIME_DIR is generated from the guest uid
# and the socket lands there, with the resolved env vars injected.
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
  cage -G -l /altroot -n wy6 -u 1000 -g 1000 -- sh -c "
    env | sort | grep -E \"^(XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\"
    stat -c \"%n %u:%g %a\" /run/user/1000
    if [ -S /run/user/1000/wayland-0 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "wy6 rc" 0 "$E2E_RC"
e2e_check_contains "wy6 generated XDG_RUNTIME_DIR" "$E2E_OUT" "XDG_RUNTIME_DIR=/run/user/1000"
e2e_check_contains "wy6 forwarded WAYLAND_DISPLAY" "$E2E_OUT" "WAYLAND_DISPLAY=wayland-0"
e2e_check_contains "wy6 generated XDG dir owned by guest uid 0700" "$E2E_OUT" "/run/user/1000 1000:1000 700"
e2e_check_contains "wy6 generated-path socket mapped" "$E2E_OUT" "SOCK-OK"

# wy7: guest XDG_RUNTIME_DIR generated from uid, but WAYLAND_DISPLAY overridden.
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
  cage -G -l /altroot -n wy7 -u 1000 -g 1000 \
  -e WAYLAND_DISPLAY=w7 -- sh -c "
    env | sort | grep -E \"^(XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\"
    if [ -S /run/user/1000/w7 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "wy7 rc" 0 "$E2E_RC"
e2e_check_contains "wy7 generated XDG_RUNTIME_DIR kept" "$E2E_OUT" "XDG_RUNTIME_DIR=/run/user/1000"
e2e_check_contains "wy7 overridden WAYLAND_DISPLAY mapped under generated dir" "$E2E_OUT" "SOCK-OK"

# wy8: only guest XDG_RUNTIME_DIR overridden; guest WAYLAND_DISPLAY inherits the
# host value and the socket lands under the overridden dir.
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
  cage -G -l /altroot -n wy8 -u 1000 -g 1000 \
  -e XDG_RUNTIME_DIR=/gxrd8 -- sh -c "
    env | sort | grep -E \"^(XDG_RUNTIME_DIR|WAYLAND_DISPLAY)=\"
    if [ -S /gxrd8/wayland-0 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "wy8 rc" 0 "$E2E_RC"
e2e_check_contains "wy8 overridden XDG_RUNTIME_DIR" "$E2E_OUT" "XDG_RUNTIME_DIR=/gxrd8"
e2e_check_contains "wy8 inherited WAYLAND_DISPLAY mapped under override" "$E2E_OUT" "SOCK-OK"

# wy9: distinct guest uid (4321) with a nested custom -e XDG_RUNTIME_DIR; every
# created dir must be owned by the running guest uid, not root or the caller.
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
  cage -G -l /altroot -n wy9 -u 4321 -g 4321 \
  -e XDG_RUNTIME_DIR=/aa/bb/cc -e WAYLAND_DISPLAY=w -- sh -c "
    stat -c \"%n %u:%g %a\" /aa/bb/cc
    if [ -S /aa/bb/cc/w ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "wy9 rc" 0 "$E2E_RC"
e2e_check_contains "wy9 nested XDG leaf owned by running uid 0700" "$E2E_OUT" "/aa/bb/cc 4321:4321 700"
e2e_check_contains "wy9 socket mapped under nested override" "$E2E_OUT" "SOCK-OK"

# wy10: distinct guest uid (4321) with the uid-generated runtime dir.
e2e_run e2e_exec sh -c 'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
  cage -G -l /altroot -n wy10 -u 4321 -g 4321 -- sh -c "
    stat -c \"%n %u:%g %a\" /run/user/4321
    if [ -S /run/user/4321/wayland-0 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "wy10 rc" 0 "$E2E_RC"
e2e_check_contains "wy10 generated dir owned by running uid 0700" "$E2E_OUT" "/run/user/4321 4321:4321 700"
e2e_check_contains "wy10 socket mapped in generated dir" "$E2E_OUT" "SOCK-OK"

e2e_socket_down wayland /run/user/1000/wayland-0
e2e_socket_down wayland2 /run/user/2000/wayland-0
e2e_end

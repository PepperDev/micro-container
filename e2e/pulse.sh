#!/bin/sh
# GUI PulseAudio socket + cookie resolution.
#
# Socket: covers host PULSE_SERVER (default path, custom unix: path, unset) and
# guest -e PULSE_SERVER / -e XDG_RUNTIME_DIR combinations, asserting the host
# socket is bind-mounted at the guest path (a socket at the target can only be
# the bind mount; cage touches a regular file first).
#
# Cookie: covers the host resolution chain (PULSE_COOKIE, XDG_CONFIG_HOME,
# SUDO_HOME, HOME, legacy .pulse-cookie) and the guest path chain
# (-e PULSE_COOKIE, -e XDG_CONFIG_HOME, guest HOME).
#
# Intended behavior: docs/lifecycle.md §GUI Setup.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

# Host fixtures: a default pulse socket, a custom-path socket, and cookie files
# for every step of the host resolution chain.
if ! e2e_socket_up pulse /run/user/1000/pulse/native; then
  e2e_fail_fast "socat pulse socket did not start"
fi
if ! e2e_socket_up hpsock /tmp/hp/hsock; then
  e2e_fail_fast "socat custom pulse socket did not start"
fi
docker exec "$CAGE_CTR" sh -c '
  rm -rf /tmp/hc /tmp/hcfg /home/alt /home/sudohome /home/legacy
  mkdir -p /tmp/hc /tmp/hcfg/pulse /home/alt/.config/pulse \
    /home/sudohome/.config/pulse /home/legacy
  printf "HOSTCOOKIE-DIRECT\n" > /tmp/hc/cookie
  printf "HOSTCOOKIE-CFG\n" > /tmp/hcfg/pulse/cookie
  printf "HOSTCOOKIE-HOME\n" > /home/alt/.config/pulse/cookie
  printf "HOSTCOOKIE-SUDOHOME\n" > /home/sudohome/.config/pulse/cookie
  printf "HOSTCOOKIE-LEGACY\n" > /home/legacy/.pulse-cookie
  chown -R 1000:1000 /tmp/hc /tmp/hcfg /home/alt /home/sudohome /home/legacy'

# --- socket resolution -------------------------------------------------

# ps1: host PULSE_SERVER unset -> default $XDG_RUNTIME_DIR/pulse/native.
e2e_run e2e_exec sh -c 'env -u PULSE_SERVER XDG_RUNTIME_DIR=/run/user/1000 \
  cage -G -l /altroot -n ps1 -u 1000 -g 1000 -- sh -c "
    env | sort | grep -E \"^(PULSE_SERVER|XDG_RUNTIME_DIR)=\"
    stat -c \"%n %u:%g %a\" /run/user/1000/pulse
    if [ -S /run/user/1000/pulse/native ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps1 rc" 0 "$E2E_RC"
e2e_check_contains "ps1 XDG_RUNTIME_DIR set" "$E2E_OUT" "XDG_RUNTIME_DIR=/run/user/1000"
e2e_check_not_contains "ps1 PULSE_SERVER not set by default" "$E2E_OUT" "PULSE_SERVER="
e2e_check_contains "ps1 pulse dir owned by guest uid 0700" "$E2E_OUT" "/run/user/1000/pulse 1000:1000 700"
e2e_check_contains "ps1 default socket mapped" "$E2E_OUT" "SOCK-OK"

# ps2: host PULSE_SERVER custom unix: path; guest -e PULSE_SERVER different.
e2e_run e2e_exec sh -c 'PULSE_SERVER=unix:/tmp/hp/hsock cage -G -l /altroot \
  -n ps2 -u 1000 -g 1000 -e PULSE_SERVER=unix:/tmp/gs2 -- sh -c "
    env | sort | grep -E \"^(PULSE_SERVER|XDG_RUNTIME_DIR)=\"
    if [ -S /tmp/gs2 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps2 rc" 0 "$E2E_RC"
e2e_check_contains "ps2 guest PULSE_SERVER override" "$E2E_OUT" "PULSE_SERVER=unix:/tmp/gs2"
e2e_check_contains "ps2 custom host socket mapped at guest path" "$E2E_OUT" "SOCK-OK"

# ps3: host PULSE_SERVER unset (default host socket), guest -e PULSE_SERVER custom.
e2e_run e2e_exec sh -c 'env -u PULSE_SERVER XDG_RUNTIME_DIR=/run/user/1000 \
  cage -G -l /altroot -n ps3 -u 1000 -g 1000 \
  -e PULSE_SERVER=unix:/tmp/gs3 -- sh -c "
    env | sort | grep -E \"^(PULSE_SERVER|XDG_RUNTIME_DIR)=\"
    if [ -S /tmp/gs3 ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps3 rc" 0 "$E2E_RC"
e2e_check_contains "ps3 guest PULSE_SERVER override" "$E2E_OUT" "PULSE_SERVER=unix:/tmp/gs3"
e2e_check_contains "ps3 default host socket mapped at guest custom path" "$E2E_OUT" "SOCK-OK"

# ps4: host default socket, guest -e XDG_RUNTIME_DIR override -> default target
# moves under the overridden dir.
e2e_run e2e_exec sh -c 'env -u PULSE_SERVER XDG_RUNTIME_DIR=/run/user/1000 \
  cage -G -l /altroot -n ps4 -u 1000 -g 1000 \
  -e XDG_RUNTIME_DIR=/gpulse4 -- sh -c "
    env | sort | grep -E \"^(PULSE_SERVER|XDG_RUNTIME_DIR)=\"
    stat -c \"%n %u:%g %a\" /gpulse4 /gpulse4/pulse
    if [ -S /gpulse4/pulse/native ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps4 rc" 0 "$E2E_RC"
e2e_check_contains "ps4 overridden XDG_RUNTIME_DIR" "$E2E_OUT" "XDG_RUNTIME_DIR=/gpulse4"
e2e_check_contains "ps4 override dir owned by guest uid 0700" "$E2E_OUT" "/gpulse4 1000:1000 700"
e2e_check_contains "ps4 pulse subdir owned by guest uid 0700" "$E2E_OUT" "/gpulse4/pulse 1000:1000 700"
e2e_check_contains "ps4 socket mapped under overridden dir" "$E2E_OUT" "SOCK-OK"

# ps5: host XDG_RUNTIME_DIR unset (real-uid fallback), guest -e XDG_RUNTIME_DIR.
e2e_run e2e_exec sh -c 'env -u PULSE_SERVER -u XDG_RUNTIME_DIR -u SUDO_UID \
  cage -G -l /altroot -n ps5 -u 1000 -g 1000 \
  -e XDG_RUNTIME_DIR=/gpulse5 -- sh -c "
    env | sort | grep -E \"^(PULSE_SERVER|XDG_RUNTIME_DIR)=\"
    if [ -S /gpulse5/pulse/native ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps5 rc" 0 "$E2E_RC"
e2e_check_contains "ps5 socket mapped under overridden dir" "$E2E_OUT" "SOCK-OK"

# ps6: guest -e PULSE_SERVER empty -> intended as unset -> default target.
e2e_run e2e_exec sh -c 'env -u PULSE_SERVER XDG_RUNTIME_DIR=/run/user/1000 \
  cage -G -l /altroot -n ps6 -u 1000 -g 1000 -e PULSE_SERVER= -- sh -c "
    env | sort | grep -E \"^(PULSE_SERVER|XDG_RUNTIME_DIR)=\"
    if [ -S /run/user/1000/pulse/native ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps6 rc" 0 "$E2E_RC"
e2e_check_contains "ps6 empty PULSE_SERVER kept in env" "$E2E_OUT" "PULSE_SERVER="
e2e_check_contains "ps6 empty PULSE_SERVER falls back to default target" "$E2E_OUT" "SOCK-OK"

# ps7: guest -e XDG_RUNTIME_DIR empty -> intended as unset -> uid-generated dir.
e2e_run e2e_exec sh -c 'env -u PULSE_SERVER XDG_RUNTIME_DIR=/run/user/1000 \
  cage -G -l /altroot -n ps7 -u 1000 -g 1000 -e XDG_RUNTIME_DIR= -- sh -c "
    if [ -S /run/user/1000/pulse/native ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps7 rc" 0 "$E2E_RC"
e2e_check_contains "ps7 empty XDG_RUNTIME_DIR falls back to uid dir" "$E2E_OUT" "SOCK-OK"

# ps8: guest -e PULSE_SERVER whose parent directory does not exist in the guest
# root; the parent must be created under the root before the bind mount.
e2e_run e2e_exec sh -c 'env -u PULSE_SERVER XDG_RUNTIME_DIR=/run/user/1000 \
  cage -G -l /altroot -n ps8 -u 1000 -g 1000 \
  -e PULSE_SERVER=unix:/run/cage-e2e-pulse/gsock -- sh -c "
    if [ -S /run/cage-e2e-pulse/gsock ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps8 rc" 0 "$E2E_RC"
e2e_check_contains "ps8 custom parent created under guest root" "$E2E_OUT" "SOCK-OK"

# ps9: distinct guest uid (4321), custom -e XDG_RUNTIME_DIR; the XDG dir and the
# pulse subdir must be owned by the running guest uid.
e2e_run e2e_exec sh -c 'env -u PULSE_SERVER XDG_RUNTIME_DIR=/run/user/1000 \
  cage -G -l /altroot -n ps9 -u 4321 -g 4321 -e XDG_RUNTIME_DIR=/gd -- sh -c "
    stat -c \"%n %u:%g %a\" /gd /gd/pulse
    if [ -S /gd/pulse/native ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps9 rc" 0 "$E2E_RC"
e2e_check_contains "ps9 override dir owned by running uid 0700" "$E2E_OUT" "/gd 4321:4321 700"
e2e_check_contains "ps9 pulse subdir owned by running uid 0700" "$E2E_OUT" "/gd/pulse 4321:4321 700"
e2e_check_contains "ps9 socket mapped under override" "$E2E_OUT" "SOCK-OK"

# ps10: distinct guest uid (4321), uid-generated runtime dir.
e2e_run e2e_exec sh -c 'env -u PULSE_SERVER XDG_RUNTIME_DIR=/run/user/1000 \
  cage -G -l /altroot -n ps10 -u 4321 -g 4321 -- sh -c "
    stat -c \"%n %u:%g %a\" /run/user/4321/pulse
    if [ -S /run/user/4321/pulse/native ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps10 rc" 0 "$E2E_RC"
e2e_check_contains "ps10 generated pulse dir owned by running uid 0700" "$E2E_OUT" "/run/user/4321/pulse 4321:4321 700"
e2e_check_contains "ps10 socket mapped in generated dir" "$E2E_OUT" "SOCK-OK"

# ps11: distinct guest uid (4321), custom -e PULSE_SERVER with a missing parent;
# the created parent must be owned by the running guest uid with mode 0700.
e2e_run e2e_exec sh -c 'env -u PULSE_SERVER XDG_RUNTIME_DIR=/run/user/1000 \
  cage -G -l /altroot -n ps11 -u 4321 -g 4321 \
  -e PULSE_SERVER=unix:/run/cage-e2e-pulse/gsock -- sh -c "
    stat -c \"%n %u:%g %a\" /run/cage-e2e-pulse
    if [ -S /run/cage-e2e-pulse/gsock ]; then
      printf \"SOCK-OK\\n\"
    else
      printf \"SOCK-MISSING\\n\"
    fi"'
e2e_check_rc "ps11 rc" 0 "$E2E_RC"
e2e_check_contains "ps11 custom parent owned by running uid 0700" "$E2E_OUT" "/run/cage-e2e-pulse 4321:4321 700"
e2e_check_contains "ps11 custom parent socket mapped" "$E2E_OUT" "SOCK-OK"

# --- cookie resolution -------------------------------------------------

# pc1: host PULSE_COOKIE wins; guest default path under HOME.
e2e_run e2e_exec sh -c 'HOME=/home/alt PULSE_COOKIE=/tmp/hc/cookie \
  XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc1 -u 1000 -g 1000 \
  -e HOME=/home/alt -- sh -c "
    env | sort | grep -E \"^(PULSE_COOKIE|HOME)=\"
    cat /home/alt/.config/pulse/cookie 2>&1"'
e2e_check_rc "pc1 rc" 0 "$E2E_RC"
e2e_check_contains "pc1 env HOME" "$E2E_OUT" "HOME=/home/alt"
e2e_check_not_contains "pc1 PULSE_COOKIE not exported" "$E2E_OUT" "PULSE_COOKIE="
e2e_check_contains "pc1 PULSE_COOKIE source mounted at guest HOME" "$E2E_OUT" "HOSTCOOKIE-DIRECT"

# pc2: host XDG_CONFIG_HOME is the source, guest default path under HOME.
e2e_run e2e_exec sh -c 'env -u PULSE_COOKIE HOME=/home/alt XDG_CONFIG_HOME=/tmp/hcfg \
  XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc2 -u 1000 -g 1000 \
  -e HOME=/home/alt -- sh -c "cat /home/alt/.config/pulse/cookie 2>&1"'
e2e_check_rc "pc2 rc" 0 "$E2E_RC"
e2e_check_contains "pc2 XDG_CONFIG_HOME source mounted" "$E2E_OUT" "HOSTCOOKIE-CFG"

# pc3: host SUDO_HOME is preferred over HOME as the source.
e2e_run e2e_exec sh -c 'env -u PULSE_COOKIE -u XDG_CONFIG_HOME HOME=/home/alt \
  SUDO_HOME=/home/sudohome XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot \
  -n pc3 -u 1000 -g 1000 -e HOME=/home/alt -- sh -c "cat /home/alt/.config/pulse/cookie 2>&1"'
e2e_check_rc "pc3 rc" 0 "$E2E_RC"
e2e_check_contains "pc3 SUDO_HOME source mounted" "$E2E_OUT" "HOSTCOOKIE-SUDOHOME"

# pc4: host HOME is the source.
e2e_run e2e_exec sh -c 'env -u PULSE_COOKIE -u XDG_CONFIG_HOME -u SUDO_HOME \
  HOME=/home/alt XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc4 \
  -u 1000 -g 1000 -e HOME=/home/alt -- sh -c "cat /home/alt/.config/pulse/cookie 2>&1"'
e2e_check_rc "pc4 rc" 0 "$E2E_RC"
e2e_check_contains "pc4 HOME source mounted" "$E2E_OUT" "HOSTCOOKIE-HOME"

# pc5: legacy host fallback $HOME/.pulse-cookie.
e2e_run e2e_exec sh -c 'env -u PULSE_COOKIE -u XDG_CONFIG_HOME -u SUDO_HOME \
  HOME=/home/legacy XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc5 \
  -u 1000 -g 1000 -e HOME=/home/legacy -- sh -c "cat /home/legacy/.config/pulse/cookie 2>&1"'
e2e_check_rc "pc5 rc" 0 "$E2E_RC"
e2e_check_contains "pc5 legacy .pulse-cookie source mounted" "$E2E_OUT" "HOSTCOOKIE-LEGACY"

# pc6: guest -e PULSE_COOKIE overrides the guest path.
e2e_run e2e_exec sh -c 'HOME=/home/alt PULSE_COOKIE=/tmp/hc/cookie \
  XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc6 -u 1000 -g 1000 \
  -e HOME=/home/alt -e PULSE_COOKIE=/gc/cookie -- sh -c "
    env | sort | grep -E \"^(PULSE_COOKIE|HOME)=\"
    stat -c \"%n %u:%g %a\" /gc
    cat /gc/cookie 2>&1"'
e2e_check_rc "pc6 rc" 0 "$E2E_RC"
e2e_check_contains "pc6 guest PULSE_COOKIE exported" "$E2E_OUT" "PULSE_COOKIE=/gc/cookie"
e2e_check_contains "pc6 PULSE_COOKIE parent owned by guest uid 0755" "$E2E_OUT" "/gc 1000:1000 755"
e2e_check_contains "pc6 cookie mounted at guest override path" "$E2E_OUT" "HOSTCOOKIE-DIRECT"

# pc7: guest -e XDG_CONFIG_HOME moves the guest cookie target.
e2e_run e2e_exec sh -c 'HOME=/home/alt PULSE_COOKIE=/tmp/hc/cookie \
  XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc7 -u 1000 -g 1000 \
  -e HOME=/home/alt -e XDG_CONFIG_HOME=/gcfg -- sh -c "
    env | sort | grep -E \"^(XDG_CONFIG_HOME|HOME)=\"
    stat -c \"%n %u:%g %a\" /gcfg /gcfg/pulse
    cat /gcfg/pulse/cookie 2>&1"'
e2e_check_rc "pc7 rc" 0 "$E2E_RC"
e2e_check_contains "pc7 guest XDG_CONFIG_HOME exported" "$E2E_OUT" "XDG_CONFIG_HOME=/gcfg"
e2e_check_contains "pc7 XDG_CONFIG_HOME owned by guest uid 0755" "$E2E_OUT" "/gcfg 1000:1000 755"
e2e_check_contains "pc7 XDG_CONFIG_HOME/pulse owned by guest uid 0755" "$E2E_OUT" "/gcfg/pulse 1000:1000 755"
e2e_check_contains "pc7 cookie mounted under guest XDG_CONFIG_HOME" "$E2E_OUT" "HOSTCOOKIE-DIRECT"

# pc8: guest -e HOME moves the guest cookie target.
e2e_run e2e_exec sh -c 'env -u PULSE_COOKIE -u XDG_CONFIG_HOME -u SUDO_HOME HOME=/home/alt \
  XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc8 -u 1000 -g 1000 \
  -e HOME=/guser -- sh -c "
    env | grep ^HOME=
    stat -c \"%n %u:%g %a\" /guser /guser/.config /guser/.config/pulse
    cat /guser/.config/pulse/cookie 2>&1"'
e2e_check_rc "pc8 rc" 0 "$E2E_RC"
e2e_check_contains "pc8 guest HOME override exported" "$E2E_OUT" "HOME=/guser"
e2e_check_contains "pc8 guest HOME owned by guest uid 0755" "$E2E_OUT" "/guser 1000:1000 755"
e2e_check_contains "pc8 guest HOME/.config owned by guest uid 0755" "$E2E_OUT" "/guser/.config 1000:1000 755"
e2e_check_contains "pc8 guest HOME/.config/pulse owned by guest uid 0755" "$E2E_OUT" "/guser/.config/pulse 1000:1000 755"
e2e_check_contains "pc8 cookie mounted under guest HOME" "$E2E_OUT" "HOSTCOOKIE-HOME"

# pc9: distinct guest uid (4321), guest -e HOME; the last HOME component and the
# .config / .config/pulse dirs must be owned by the running guest uid, 0755.
e2e_run e2e_exec sh -c 'env -u PULSE_COOKIE -u XDG_CONFIG_HOME -u SUDO_HOME HOME=/home/alt \
  XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc9 -u 4321 -g 4321 \
  -e HOME=/gh -- sh -c "
    stat -c \"%n %u:%g %a\" /gh /gh/.config /gh/.config/pulse
    cat /gh/.config/pulse/cookie 2>&1"'
e2e_check_rc "pc9 rc" 0 "$E2E_RC"
e2e_check_contains "pc9 guest HOME owned by running uid 0755" "$E2E_OUT" "/gh 4321:4321 755"
e2e_check_contains "pc9 guest HOME/.config owned by running uid 0755" "$E2E_OUT" "/gh/.config 4321:4321 755"
e2e_check_contains "pc9 guest HOME/.config/pulse owned by running uid 0755" "$E2E_OUT" "/gh/.config/pulse 4321:4321 755"
e2e_check_contains "pc9 cookie mounted under guest HOME" "$E2E_OUT" "HOSTCOOKIE-HOME"

# pc10: distinct guest uid (4321), guest -e XDG_CONFIG_HOME.
e2e_run e2e_exec sh -c 'HOME=/home/alt PULSE_COOKIE=/tmp/hc/cookie \
  XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc10 -u 4321 -g 4321 \
  -e HOME=/home/alt -e XDG_CONFIG_HOME=/ghc -- sh -c "
    stat -c \"%n %u:%g %a\" /ghc /ghc/pulse
    cat /ghc/pulse/cookie 2>&1"'
e2e_check_rc "pc10 rc" 0 "$E2E_RC"
e2e_check_contains "pc10 XDG_CONFIG_HOME owned by running uid 0755" "$E2E_OUT" "/ghc 4321:4321 755"
e2e_check_contains "pc10 XDG_CONFIG_HOME/pulse owned by running uid 0755" "$E2E_OUT" "/ghc/pulse 4321:4321 755"

# pc11: distinct guest uid (4321), guest -e PULSE_COOKIE whose parent must be
# created and owned by the running guest uid, 0755.
e2e_run e2e_exec sh -c 'HOME=/home/alt PULSE_COOKIE=/tmp/hc/cookie \
  XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc11 -u 4321 -g 4321 \
  -e HOME=/home/alt -e PULSE_COOKIE=/ghx/c -- sh -c "
    stat -c \"%n %u:%g %a\" /ghx
    cat /ghx/c 2>&1"'
e2e_check_rc "pc11 rc" 0 "$E2E_RC"
e2e_check_contains "pc11 PULSE_COOKIE parent owned by running uid 0755" "$E2E_OUT" "/ghx 4321:4321 755"

# pc12: distinct guest uid (4321), nested guest -e HOME; intermediate parents are
# root-owned 0755, the last component and .config chain belong to the guest uid.
e2e_run e2e_exec sh -c 'env -u PULSE_COOKIE -u XDG_CONFIG_HOME -u SUDO_HOME HOME=/home/alt \
  XDG_RUNTIME_DIR=/run/user/1000 cage -G -l /altroot -n pc12 -u 4321 -g 4321 \
  -e HOME=/a/b/c -- sh -c "
    stat -c \"%n %u:%g %a\" /a /a/b /a/b/c"'
e2e_check_rc "pc12 rc" 0 "$E2E_RC"
e2e_check_contains "pc12 intermediate parent root-owned 0755" "$E2E_OUT" "/a 0:0 755"
e2e_check_contains "pc12 nested intermediate parent root-owned 0755" "$E2E_OUT" "/a/b 0:0 755"
e2e_check_contains "pc12 last HOME component owned by running uid 0755" "$E2E_OUT" "/a/b/c 4321:4321 755"

e2e_socket_down pulse /run/user/1000/pulse/native
e2e_socket_down hpsock /tmp/hp/hsock
e2e_end

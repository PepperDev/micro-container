#!/bin/sh
#
# Shared harness for cage end-to-end tests (POSIX sh / busybox ash).
#
# A test script includes this file, then:
#
#   . "$(dirname "$0")/_common.sh"
#   e2e_begin "$(basename "$0" .sh)"
#   ...
#   e2e_check "description" some command
#   e2e_end
#
# On include, e2e_ensure_container() validates the docker CLI and a reachable
# (remote) docker server, then provisions, idempotently:
#   * $CAGE_CTR: appdir on a tmpfs, /run/user/1000, /tmp/.X11-unix;
#   * /altroot: an alpine minirootfs (kept pristine — no extra packages);
#   * container tools socat and acl (installed with `apk add`);
#   * the freshly built cage binary.
#
# Guest-side tools (e.g. socat) are never baked into /altroot: a test installs
# them through cage itself, so they live only in that instance's overlay, e.g.
# /var/lib/cage/app-<name>/upper/...  See e2e_guest_install.
#
# Tests create but never remove the container, so they can be run standalone and
# repeatedly. Only e2e/_all.sh tears everything down.
#
# Every check appends "STATUS<TAB>description" to $E2E_RESULTS/<test>.tsv; that
# is the common output e2e/_all.sh turns into the summary.

set -eu

E2E_DIR=$(
  cd "$(dirname "$0")" &&
    pwd
)
REPO_ROOT=$(
  cd "$E2E_DIR/.." &&
    pwd
)

CAGE_IMAGE=${CAGE_E2E_IMAGE:-alpine:3.24.2}
CAGE_CTR=${CAGE_E2E_CONTAINER:-cage-t}
CAGE_BIN=${CAGE_E2E_BIN:-$REPO_ROOT/bin/cage}
MINIROOTFS_BASE=https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/x86_64
MINIROOTFS_TARBALL=alpine-minirootfs-3.24.2-x86_64.tar.gz
MINIROOTFS_URL=${CAGE_E2E_MINIROOTFS_URL:-$MINIROOTFS_BASE/$MINIROOTFS_TARBALL}
E2E_WORK=${CAGE_E2E_WORK:-/tmp/cage-e2e}
E2E_RESULTS=${CAGE_E2E_RESULTS:-$E2E_WORK/results}
E2E_CTR_WORK=/tmp/cage-e2e
E2E_UID=${CAGE_E2E_UID:-1000}
E2E_GID=${CAGE_E2E_GID:-1000}
E2E_USER=${CAGE_E2E_USER:-e2e}
E2E_HOME=${CAGE_E2E_HOME:-/home/e2e}
E2E_STRACE=${CAGE_E2E_STRACE:-0}
E2E_STRACE_DIR=${CAGE_E2E_STRACE_DIR:-/tmp/cage-e2e/traces}
E2E_STRACE_CTR_DIR=/tmp/cage-e2e/strace

E2E_TEST_NAME=
E2E_RESULTS_FILE=
E2E_PASS=0
E2E_FAIL=0
E2E_OUT=
E2E_RC=0

log() {
  printf '%s\n' "$*"
}

e2e_fail_fast() {
  printf 'e2e: %s\n' "$1" >&2
  exit 2
}

# --- docker / container -----------------------------------------------

e2e_require_docker() {
  if ! command -v docker >/dev/null 2>&1; then
    e2e_fail_fast "docker CLI not found"
  fi
  if ! docker info >/dev/null 2>&1; then
    e2e_fail_fast "docker server not reachable (docker info failed)"
  fi
}

# --- docker exec helpers ----------------------------------------------
#
# Test commands run as the unprivileged container user by default; cage
# escalates through its setuid-root install (repaired by a root run if needed).
# Root is reserved for provisioning, /altroot setup, socket servers, and
# fixtures that create or own root state.

e2e_exec() {
  if [ "$E2E_STRACE" = 1 ]; then
    _e2e_strace_wrap_root "$@"
    return $?
  fi
  docker exec -u "$E2E_UID:$E2E_GID" "$CAGE_CTR" "$@"
}

e2e_exec_i() {
  if [ "$E2E_STRACE" = 1 ]; then
    _e2e_strace_wrap_root "$@"
    return $?
  fi
  docker exec -i -u "$E2E_UID:$E2E_GID" "$CAGE_CTR" "$@"
}

e2e_exec_d() {
  if [ "$E2E_STRACE" = 1 ]; then
    _e2e_strace_wrap_root "$@"
    return $?
  fi
  docker exec -d -u "$E2E_UID:$E2E_GID" "$CAGE_CTR" "$@"
}

# Root inside the remote e2e container, never the e2e runner/Docker host.
e2e_root() {
  docker exec "$CAGE_CTR" "$@"
}

e2e_ctr_running() {
  docker ps --format '{{.Names}}' 2>/dev/null | grep -qx -- "$CAGE_CTR"
}

e2e_ctr_exists() {
  docker ps -a --format '{{.Names}}' 2>/dev/null | grep -qx -- "$CAGE_CTR"
}

e2e_build() {
  if [ "${CAGE_E2E_SKIP_BUILD:-}" = 1 ]; then
    return 0
  fi
  log "e2e: building cage (make all)"
  if ! make -C "$REPO_ROOT" all >/dev/null; then
    e2e_fail_fast "make all failed"
  fi
  if [ ! -x "$CAGE_BIN" ]; then
    e2e_fail_fast "$CAGE_BIN missing after build"
  fi
}

e2e_new_container() {
  if e2e_ctr_exists; then
    docker rm -f "$CAGE_CTR" >/dev/null
  fi
  log "e2e: creating container $CAGE_CTR ($CAGE_IMAGE)"
  # --init: docker-init (PID1) reaps orphans, so sanitize's SIGKILLs never
  # accumulate as Z-state children that pgrep would count. The supervisor must
  # not be named "sleep": e2e_sanitize pkill -9 -x sleep would take it down.
  if ! docker run -d \
    --name "$CAGE_CTR" \
    --init \
    --privileged \
    --cgroupns=host \
    "$CAGE_IMAGE" \
    tail -f /dev/null >/dev/null
  then
    e2e_fail_fast "docker run failed"
  fi
}

# Idempotent provisioning. Safe to call against a container in any state.
e2e_provision() {
  # appdir/upperdir must sit on tmpfs: overlayfs rejects an overlayfs upperdir
  # and cage exits 1 silently.
  docker exec "$CAGE_CTR" sh -c '
    if ! grep -q " /var/lib/cage " /proc/mounts; then
      mkdir -p /var/lib/cage
      mount -t tmpfs -o size=1G tmpfs /var/lib/cage
    fi'

  if ! docker exec "$CAGE_CTR" test -x /altroot/bin/sh 2>/dev/null; then
    log "e2e: provisioning /altroot (alpine minirootfs)"
    docker exec "$CAGE_CTR" sh -c '
      if ! grep -q " /altroot " /proc/mounts; then
        mkdir -p /altroot
        mount -t tmpfs -o size=512M tmpfs /altroot
      fi'
    if ! docker exec "$CAGE_CTR" sh -c "
      wget -q -O /tmp/minirootfs.tar.gz '$MINIROOTFS_URL' &&
      tar -xzf /tmp/minirootfs.tar.gz -C /altroot &&
      rm -f /tmp/minirootfs.tar.gz"
    then
      e2e_fail_fast "failed to populate /altroot from $MINIROOTFS_URL"
    fi
  fi
  # root is already in the minirootfs passwd; add a well-known unprivileged user.
  docker exec "$CAGE_CTR" sh -c '
    mkdir -p /altroot/etc
    if ! grep -q "^dev:" /altroot/etc/passwd 2>/dev/null; then
      printf "dev:x:1000:100::/home/dev:/bin/sh\n" >> /altroot/etc/passwd
    fi'

  # Container-side tools only (guest tools are installed through cage).
  if ! docker exec "$CAGE_CTR" sh -c \
    'command -v socat >/dev/null 2>&1 && command -v getfacl >/dev/null 2>&1'
  then
    log "e2e: installing container packages (socat, acl)"
    docker exec "$CAGE_CTR" apk add --no-cache socat acl >/dev/null 2>&1
  fi
  if [ "$E2E_STRACE" = 1 ] && ! docker exec "$CAGE_CTR" sh -c 'command -v strace >/dev/null 2>&1'; then
    log "e2e: installing container packages (strace)"
    docker exec "$CAGE_CTR" apk add --no-cache -q strace >/dev/null 2>&1
  fi

  # Unprivileged container user: tests run as this user and rely on cage's
  # setuid install to escalate. sudo is deliberately not installed here — only
  # the single test that expects the sudo path installs it, so an unexpected
  # sudo call anywhere else fails instead of silently succeeding.
  docker exec "$CAGE_CTR" sh -c '
    if ! id -u "$1" >/dev/null 2>&1; then
      addgroup -g "$2" "$1" 2>/dev/null || :
      adduser -D -u "$3" -G "$1" -h "$4" -s /bin/sh "$1"
    fi
    mkdir -p "$4"
    chown "$3:$2" "$4"
  ' _ "$E2E_USER" "$E2E_GID" "$E2E_UID" "$E2E_HOME"

  # GUI fixtures: a runtime dir owned by the test user and the X11 socket dir.
  docker exec "$CAGE_CTR" sh -c '
    mkdir -p /run/user/1000 /tmp/.X11-unix
    chmod 700 /run/user/1000
    chmod 1777 /tmp/.X11-unix
    chown "$1:$2" /run/user/1000
  ' _ "$E2E_UID" "$E2E_GID"
}

e2e_install_cage() {
  e2e_build
  docker cp "$CAGE_BIN" "$CAGE_CTR:/usr/local/bin/cage" >/dev/null
  docker exec "$CAGE_CTR" sh -c \
    'chown 0:0 /usr/local/bin/cage && chmod 4755 /usr/local/bin/cage'
}

e2e_ensure_container() {
  e2e_require_docker
  if ! e2e_ctr_running; then
    e2e_new_container
  fi
  e2e_provision
  e2e_install_cage
}

# Kill leftovers from a previous (possibly crashed) run so a test always starts
# from a known state. Never removes the altroot/appdir tmpfs mounts themselves.
e2e_sanitize() {
  docker exec "$CAGE_CTR" sh -c '
    pkill -9 -x cage 2>/dev/null
    pkill -9 -f "[U]NIX-LISTEN" 2>/dev/null
    pkill -9 -x sleep 2>/dev/null
    grep -oE "/[^ ]*/(altroot|var/lib/cage)[^ ]*" /proc/mounts | sort -ru |
      while IFS= read -r mp; do
        case "$mp" in
          /altroot|/var/lib/cage) continue ;;
        esac
        umount -l "$mp" 2>/dev/null
      done
    umount -l /var/lib/cage-loop 2>/dev/null
    losetup -a 2>/dev/null | grep cage-loop | sed "s/:.*//" |
      while IFS= read -r d; do
        losetup -d "$d" 2>/dev/null
      done
    rm -f /var/lib/cage-loop.img 2>/dev/null
    rm -rf /var/lib/cage/app-* /var/lib/cage/x1 2>/dev/null
    rm -rf /tmp/.X11-unix/X* /run/user/1000/wayland-* /run/user/1000/pulse 2>/dev/null
    rm -f /run/cage/*.pid /run/cage-pf.pid /foo /bar /bare 2>/dev/null
    rm -rf /nope /run/cage-e2e-pulse 2>/dev/null
    rm -rf "$1/recv" 2>/dev/null
  ' _ "$E2E_CTR_WORK" >/dev/null 2>&1 || :
}

# Full teardown: host-side cgroups (the container runs with --cgroupns=host, so
# its /sys/fs/cgroup is the docker host's mount) then the container.
e2e_teardown() {
  if e2e_ctr_running; then
    docker exec "$CAGE_CTR" sh -c \
      'find /sys/fs/cgroup -maxdepth 1 -name "cage-*" -type d -exec rmdir {} +' \
      >/dev/null 2>&1 || :
  fi
  if e2e_ctr_exists; then
    docker rm -f "$CAGE_CTR" >/dev/null 2>&1
  fi
  log "e2e: container $CAGE_CTR removed"
}

# --- reusable socat unix-socket server --------------------------------

# e2e_socket_up <name> <path>: start (inside the container) a socat server that
# appends everything it receives on <socket-path> to $E2E_CTR_WORK/recv/<name>.
e2e_socket_up() {
  up_name=$1
  up_path=$2
  e2e_socket_down "$up_name" "$up_path"
  # shellcheck disable=SC2016 # $1..$3 expand in the container shell, not the host
  e2e_exec sh -c '
    mkdir -p "$1/recv" "$(dirname "$2")"
    : > "$1/recv/$3"
  ' _ "$E2E_CTR_WORK" "$up_path" "$up_name"
  # shellcheck disable=SC2016 # $1..$3 expand in the container shell, not the host
  e2e_exec_d sh -c '
    socat -u UNIX-LISTEN:"$1",fork,unlink-early - >>"$2/recv/$3" 2>"$2/recv/$3.err"
  ' _ "$up_path" "$E2E_CTR_WORK" "$up_name"
  up_i=0
  while [ "$up_i" -lt 100 ]; do
    if e2e_exec test -S "$up_path" 2>/dev/null; then
      return 0
    fi
    up_i=$(( up_i + 1 ))
    sleep 0.1
  done
  log "e2e: socat socket '$up_name' did not come up at $up_path" >&2
  return 1
}

e2e_socket_down() {
  docker exec "$CAGE_CTR" sh -c '
    pkill -f "[U]NIX-LISTEN:$1" 2>/dev/null
    rm -f "$1"
  ' _ "$2" >/dev/null 2>&1 || :
}

e2e_socket_recv() {
  # shellcheck disable=SC2016 # $1 and $2 expand in the container shell, not the host
  e2e_exec sh -c 'cat "$1/recv/$2" 2>/dev/null' _ "$E2E_CTR_WORK" "$1"
}

# --- strace helpers ---------------------------------------------------

_e2e_strace_wrap() {
  local trace_name trace_file
  trace_name="${E2E_TEST_NAME:-e2e}-$(date +%s)-$$-$RANDOM"
  trace_file="$E2E_STRACE_CTR_DIR/${trace_name}.trace"
  docker exec -u "$E2E_UID:$E2E_GID" "$CAGE_CTR" sh -c 'mkdir -p "$1"' _ "$E2E_STRACE_CTR_DIR" >/dev/null 2>&1 || :
  docker exec -u "$E2E_UID:$E2E_GID" "$CAGE_CTR" sh -c 't="$1"; shift; strace -tt -f -o "$t" "$@"' _ "$trace_file" "$@"
}

_e2e_strace_wrap_root() {
  local trace_name trace_file
  trace_name="${E2E_TEST_NAME:-e2e}-$(date +%s)-$$-$RANDOM"
  trace_file="$E2E_STRACE_CTR_DIR/${trace_name}.trace"
  docker exec "$CAGE_CTR" sh -c 'mkdir -p "$1"' _ "$E2E_STRACE_CTR_DIR" >/dev/null 2>&1 || :
  docker exec "$CAGE_CTR" sh -c 't="$1"; shift; strace -tt -f -o "$t" "$@"' _ "$trace_file" "$@"
}

_e2e_strace_collect() {
  if [ "$E2E_STRACE" != 1 ]; then
    return 0
  fi
  mkdir -p "$E2E_STRACE_DIR"
  docker exec "$CAGE_CTR" sh -c '
    if [ -d "$1" ] && [ "$(ls -A "$1" 2>/dev/null | wc -l)" -gt 0 ]; then
      tar -C "$1" -cf - . 2>/dev/null
    fi
  ' _ "$E2E_STRACE_CTR_DIR" | tar -C "$E2E_STRACE_DIR" -xf - 2>/dev/null || :
  docker exec "$CAGE_CTR" sh -c 'rm -rf "$1"/*' _ "$E2E_STRACE_CTR_DIR" >/dev/null 2>&1 || :
}

# --- cage helpers -----------------------------------------------------

# Wait until a session is fully torn down (its default pidfile is gone). cage
# tears sessions down asynchronously: a same-`-n` run started before teardown
# finishes is treated as a re-entry, which by design skips GUI mounts and socket
# ACLs, so callers that expect a fresh first entry must wait here first.
e2e_wait_session_gone() {
  wait_name=$1
  wait_i=0
  while [ "$wait_i" -lt 200 ]; do
    if ! e2e_exec test -e "/run/cage/$wait_name.pid" 2>/dev/null; then
      return 0
    fi
    wait_i=$(( wait_i + 1 ))
    sleep 0.1
  done
  log "e2e: session '$wait_name' still present after wait" >&2
  return 1
}

# Install a package inside an instance's guest root, so the tool lives only in
# that instance's overlay (/var/lib/cage/app-<name>/upper/...). Idempotent.
e2e_guest_install() {
  inst_name=$1
  inst_pkg=$2
  # shellcheck disable=SC2016 # $1 and $2 expand in the container shell, not the host
  e2e_exec sh -c '
    cage -l /altroot -n "$1" -- sh -c "
      if ! command -v $2 >/dev/null 2>&1; then
        apk add --no-cache $2
      fi"
  ' _ "$inst_name" "$inst_pkg" >/dev/null 2>&1
  e2e_wait_session_gone "$inst_name"
}

# --- assertions -------------------------------------------------------

e2e_begin() {
  E2E_TEST_NAME="$1"
  mkdir -p "$E2E_RESULTS"
  E2E_RESULTS_FILE="$E2E_RESULTS/$E2E_TEST_NAME.tsv"
  : > "$E2E_RESULTS_FILE"
  E2E_PASS=0
  E2E_FAIL=0
  log ""
  log "### $E2E_TEST_NAME"
}

_e2e_record() {
  printf '%s\t%s\n' "$1" "$2" >>"$E2E_RESULTS_FILE"
  if [ "$1" = PASS ]; then
    E2E_PASS=$(( E2E_PASS + 1 ))
    log "  ok   $2"
  else
    E2E_FAIL=$(( E2E_FAIL + 1 ))
    log "  FAIL $2"
  fi
}

e2e_pass() {
  _e2e_record PASS "$1"
}

e2e_fail() {
  _e2e_record FAIL "$1"
}

# e2e_check <description> <command...>
e2e_check() {
  check_desc=$1
  shift
  if "$@"; then
    e2e_pass "$check_desc"
  else
    e2e_fail "$check_desc"
  fi
}

# e2e_check_eq <description> <expected> <actual>
e2e_check_eq() {
  if [ "$2" = "$3" ]; then
    e2e_pass "$1"
  else
    e2e_fail "$1 (expected [$2], got [$3])"
  fi
}

# e2e_check_ne <description> <a> <b>
e2e_check_ne() {
  if [ "$2" != "$3" ]; then
    e2e_pass "$1"
  else
    e2e_fail "$1 (both [$2])"
  fi
}

# e2e_check_contains <description> <haystack> <needle>
e2e_check_contains() {
  case "$2" in
    *"$3"*) e2e_pass "$1" ;;
    *) e2e_fail "$1 (missing [$3] in [$2])" ;;
  esac
}

# e2e_check_not_contains <description> <haystack> <needle>
e2e_check_not_contains() {
  case "$2" in
    *"$3"*) e2e_fail "$1 (unexpected [$3])" ;;
    *) e2e_pass "$1" ;;
  esac
}

# e2e_check_rc <description> <expected_rc> <actual_rc>
e2e_check_rc() {
  if [ "$2" = "$3" ]; then
    e2e_pass "$1"
  else
    e2e_fail "$1 (expected rc=$2, got rc=$3)"
  fi
}

# e2e_run <command...>: capture combined output in $E2E_OUT and status in $E2E_RC
# shellcheck disable=SC2034  # the captured values are read by the callers
e2e_run() {
  if E2E_OUT=$("$@" 2>&1); then
    E2E_RC=0
  else
    E2E_RC=$?
  fi
}

e2e_end() {
  _e2e_strace_collect
  log "### $E2E_TEST_NAME: $E2E_PASS passed, $E2E_FAIL failed"
  if [ "$E2E_STRACE" = 1 ]; then
    log "e2e: strace traces copied to $E2E_STRACE_DIR"
  fi
  [ "$E2E_FAIL" -eq 0 ]
}

# --- bootstrap on include ---------------------------------------------

e2e_ensure_container

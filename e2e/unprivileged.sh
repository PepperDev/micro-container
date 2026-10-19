#!/bin/sh
# Repair of a wrongly-installed cage, and unprivileged invocation.
#
# With cage neither root-owned nor setuid, a root run self-heals it in place
# (no sudo involved), while an unprivileged run escalates through `sudo --fix`
# and fails, naming sudo, when sudo is absent. sudo is installed only for the
# sub-case that expects it, so any use elsewhere fails loudly.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

sudoers_file=/etc/sudoers.d/e2e

# Leave cage neither root-owned nor setuid: the broken state under test.
break_install() {
  e2e_root chown "$E2E_UID:$E2E_GID" /usr/local/bin/cage
  e2e_root chmod 0755 /usr/local/bin/cage
}

# Echo SETUID_OK only when cage is owned by root and setuid.
# shellcheck disable=SC2016 # $(stat ...) runs in the container, not the host
setuid_root_marker() {
  e2e_root sh -c '
    if [ -u /usr/local/bin/cage ] && [ "$(stat -c %u /usr/local/bin/cage)" = 0 ]; then
      printf "SETUID_OK\n"
    fi'
}

# The here-doc is expanded by the host shell and fed to the container as root.
write_sudoers() {
  docker exec -i "$CAGE_CTR" sh <<EOF
mkdir -p /etc/sudoers.d
printf '%s ALL=(ALL) NOPASSWD: ALL\n' "$E2E_USER" > "$sudoers_file"
chmod 0440 "$sudoers_file"
EOF
}

# Poll until the repair is visible; a lone docker exec can come back empty under
# load, and this also tolerates any delay in the repair taking effect.
wait_setuid_root() {
  setuid_tries=0
  while [ "$setuid_tries" -lt 50 ]; do
    if [ "$(setuid_root_marker)" = "SETUID_OK" ]; then
      return 0
    fi
    setuid_tries=$(( setuid_tries + 1 ))
    sleep 0.1
  done
  return 1
}

# apk occasionally fails to take its database lock under load; retry until the
# package is actually present rather than trusting apk's exit status.
has_sudo() {
  e2e_root sh -c 'command -v sudo >/dev/null 2>&1'
}

install_sudo() {
  sudo_tries=0
  while ! has_sudo; do
    if [ "$sudo_tries" -ge 10 ]; then
      return 1
    fi
    e2e_root apk add sudo >/dev/null 2>&1 || :
    sudo_tries=$(( sudo_tries + 1 ))
    sleep 1
  done
}

remove_sudo() {
  e2e_root rm -f "$sudoers_file"
  sudo_tries=0
  while has_sudo; do
    if [ "$sudo_tries" -ge 10 ]; then
      return 1
    fi
    e2e_root apk del sudo >/dev/null 2>&1 || :
    sudo_tries=$(( sudo_tries + 1 ))
    sleep 1
  done
}

remove_sudo >/dev/null 2>&1 || :

# --- 1. a root run self-heals, with no sudo available -----------------
if has_sudo; then
  e2e_fail "sudo unexpectedly present before the self-heal case"
fi
break_install
e2e_run e2e_root cage -l /altroot -n heal1 -- /bin/true
e2e_check_rc "root run on a broken install rc" 0 "$E2E_RC"
e2e_check "root run self-heals owner and setuid" wait_setuid_root

# --- 2. without sudo, an unprivileged run fails naming sudo -----------
break_install
e2e_run e2e_exec cage -l /altroot -n u0 -- /bin/true
e2e_check_rc "no setuid and no sudo -> rc 1" 1 "$E2E_RC"
e2e_check_contains "escalation error names sudo" "$E2E_OUT" "sudo"

# --- 3. with passwordless sudo, an unprivileged run repairs then runs --
break_install
if ! install_sudo; then
  e2e_fail_fast "could not install sudo for the auto-fix case"
fi
write_sudoers
e2e_run e2e_exec cage -l /altroot -n heal2 -- id
e2e_check_rc "unprivileged run auto-fixes via sudo" 0 "$E2E_RC"
e2e_check_contains "unprivileged run is root inside" "$E2E_OUT" "uid=0(root)"
e2e_check "sudo path restored owner and setuid" wait_setuid_root

# Restore the suite invariant: cage setuid-root and sudo absent.
e2e_root chown 0:0 /usr/local/bin/cage
e2e_root chmod 4755 /usr/local/bin/cage
if remove_sudo; then
  sudo_state=absent
else
  sudo_state=present
fi
e2e_check_eq "sudo absent after the sudo case" "absent" "$sudo_state"

e2e_end

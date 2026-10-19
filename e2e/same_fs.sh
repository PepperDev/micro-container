#!/bin/sh
# Same-filesystem lowerdir/upperdir (overlayfs can't nest) falls back to a
# shared ext4 loopback image, cached at /var/lib/cage-loop for the container's
# lifetime. Drops the appdir tmpfs so the appdir would land on the lowerdir's
# filesystem, observes the loopback mid-run, then restores the tmpfs.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

docker exec "$CAGE_CTR" umount /var/lib/cage

e2e_exec_d sh -c \
  'setsid cage -n d1 -- /bin/sh -c "sleep 30" >/tmp/d1.log 2>&1 </dev/null'
sleep 2

e2e_run e2e_exec cat /tmp/d1.log
e2e_check_contains "same-fs loopback message" "$E2E_OUT" "using ext4 loopback"

e2e_run e2e_exec sh -c 'mount | grep "/var/lib/cage-loop"'
e2e_check_contains "ext4 loop mounted mid-run" "$E2E_OUT" "type ext4"

e2e_run e2e_exec sh -c '
  if [ -d /var/lib/cage-loop/app-d1 ]; then
    printf "APPDIR\n"
  fi'
e2e_check_contains "appdir relocated onto the loopback" "$E2E_OUT" "APPDIR"

e2e_run docker exec "$CAGE_CTR" sh -c '
  if ! dmesg > /tmp/dm.txt 2>/dev/null; then
    printf "DMESG_UNREADABLE\n"
    exit 0
  fi
  if grep -q "EXT4-fs error" /tmp/dm.txt; then
    printf "EXT4_ERROR\n"
  else
    printf "NONE\n"
  fi'
e2e_check_contains "ext4 loop mounted cleanly" "$E2E_OUT" "NONE"

e2e_exec cage -k -n d1 >/dev/null 2>&1 || :
sleep 6

# Restore the tmpfs appdir and drop the loopback.
docker exec "$CAGE_CTR" sh -c '
  umount /var/lib/cage-loop 2>/dev/null
  while IFS= read -r d; do
    losetup -d "$d" 2>/dev/null
  done <<EOF
$(losetup -a 2>/dev/null | grep cage-loop | sed "s/:.*//")
EOF
  rm -f /var/lib/cage-loop.img
  mount -t tmpfs -o size=1G tmpfs /var/lib/cage'

e2e_end

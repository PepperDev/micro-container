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

while docker exec "$CAGE_CTR" umount /var/lib/cage 2>/dev/null; do :; done
if docker exec "$CAGE_CTR" sh -c 'grep -q " /var/lib/cage " /proc/mounts'; then
  e2e_fail_fast "cannot unmount /var/lib/cage"
fi

docker exec "$CAGE_CTR" rm -rf /var/lib/cage-loop/app-d1
e2e_exec_d sh -c \
  'setsid cage -n d1 -- /bin/sh -c "sleep 30" >/tmp/d1.log 2>&1 </dev/null'
i=0
while [ "$i" -lt 100 ]; do
  if e2e_exec test -d /var/lib/cage-loop/app-d1; then
    break
  fi
  i=$(( i + 1 ))
  sleep 0.1
done

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

# Loop volume expected in host mount ns when same filesystem
e2e_root sh -c 'test ! -d /var/lib/cage-loop/app-samefs'
e2e_exec cage -n samefs true 2>/dev/null || docker exec "$CAGE_CTR" sh -c 'cage -n samefs true' 2>/dev/null || :
sleep 1
e2e_root sh -c 'test -d /var/lib/cage-loop/app-samefs'
e2e_check_rc "samefs loop appdir created in host mount ns" 0 "$?"
e2e_exec cage -k -n samefs 2>/dev/null || :
rm -rf /var/lib/cage-loop/app-samefs 2>/dev/null || :

# Restore the tmpfs appdir and drop the loopback.
docker exec "$CAGE_CTR" sh -c '
  while umount /var/lib/cage-loop 2>/dev/null; do :; done
  while IFS= read -r d; do
    losetup -d "$d" 2>/dev/null
  done <<EOF
$(losetup -a 2>/dev/null | grep cage-loop | sed "s/:.*//")
EOF
  rm -f /var/lib/cage-loop.img
  while umount /var/lib/cage 2>/dev/null; do :; done
  mount -t tmpfs -o size=1G tmpfs /var/lib/cage'

e2e_end

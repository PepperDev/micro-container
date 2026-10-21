#!/bin/sh
# Appdir/upperdir/workdir resolution matrix: custom and computed values over
# each other — not-samefs layouts, pidfile interplay, and the samefs
# ext4-loopback corner cases (docs/cli-arguments.md -a/-U/-w/-n/-p,
# docs/lifecycle.md §Samefs / Ext4 Loopback Flow).
#
# The samefs loopdir corner cases encode the derived-loopdir contract
# (parent(computed_appdir)-loop, suffix skip, custom -w kept under loopdir /
# fatal outside): they FAIL against the current binary and turn green once
# the TODO samefs loopback fix lands.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

# Leftovers from earlier or crashed runs (e2e_sanitize only clears app-* names
# and the default loop).
# shellcheck disable=SC2016 # $mp/$d expand in the container shell, not the host
e2e_root sh -c '
  for mp in /var/lib/cage-loop /other/dir-loop /tmp2/dir-loop /tmp3/dir-loop \
    /ad8custom-loop; do
    umount "$mp" 2>/dev/null || :
  done
  losetup -a 2>/dev/null | grep -- "-loop" | sed "s/:.*//" |
    while IFS= read -r d; do
      losetup -d "$d" 2>/dev/null
    done
  rm -f /var/lib/cage-loop.img /other/dir-loop.img /tmp2/dir-loop.img \
    /tmp3/dir-loop.img /ad8custom-loop.img 2>/dev/null
  rm -rf /var/lib/cage/ad1 /var/lib/cage/ad2 /var/lib/cage/ad4 /var/lib/cage/ad5 \
    /var/lib/cage/ad6 /var/lib/cage/up4 /ad8custom /ad8custom-loop /myw /ad9up \
    /up9x /other /tmp2 /tmp3 2>/dev/null'

# kill_session <cage -k args...>: teardown detached -- the -k wait can
# outlive the attached-exec idle budget, so never watch it on a stream.
kill_session() {
  e2e_exec_d sh -c 'setsid cage -k "$@" >/dev/null 2>&1 </dev/null' _ "$@"
}

# --- not-samefs: /var/lib/cage on tmpfs, -l /altroot (different fs) --------

# custom -a, no -U: upperdir = ${appdir}/upper, workdir = ${appdir}/work,
# -n drives only the pidfile.
e2e_exec_d sh -c 'setsid cage -l /altroot -a /var/lib/cage/ad1 -n ad1 -- /bin/sh -c "touch /marker; sleep 60" >/tmp/ad1.log 2>&1 </dev/null'
sleep 2
e2e_run e2e_exec sh -c 'ls /var/lib/cage/ad1/upper/marker /var/lib/cage/ad1/work 2>/dev/null'
e2e_check_rc "custom -a layout rc" 0 "$E2E_RC"
e2e_check_contains "-a without -U: upperdir = appdir/upper" "$E2E_OUT" "ad1/upper/marker"
e2e_check_contains "-a without -U: workdir = appdir/work" "$E2E_OUT" "ad1/work"
e2e_run e2e_exec sh -c 'test ! -e /var/lib/cage/app-ad1 && printf ABSENT'
e2e_check_contains "-n not used for appdir when -a given" "$E2E_OUT" "ABSENT"
e2e_run e2e_exec cat /run/cage/ad1.pid
e2e_check "-n drives default pidfile path" test -n "$E2E_OUT"
kill_session -n ad1
e2e_wait_session_gone ad1

# default appdir from -n (no -a, no -U).
e2e_exec_d sh -c 'setsid cage -l /altroot -n ad2 -- /bin/sh -c "touch /marker; sleep 60" >/tmp/ad2.log 2>&1 </dev/null'
sleep 2
e2e_run e2e_exec sh -c 'ls /var/lib/cage/app-ad2/upper/marker 2>/dev/null'
e2e_check_rc "-n default appdir marker rc" 0 "$E2E_RC"
e2e_check_contains "-n without -a: appdir = app-\${name}" "$E2E_OUT" "app-ad2/upper/marker"
kill_session -n ad2
e2e_wait_session_gone ad2

# default appdir without -n: /var/lib/cage/default, pidfile /run/cage/pid.
e2e_exec_d sh -c 'setsid cage -l /altroot -- /bin/sh -c "touch /marker; sleep 60" >/tmp/ad3.log 2>&1 </dev/null'
sleep 2
e2e_run e2e_exec sh -c 'ls /var/lib/cage/default/upper/marker 2>/dev/null'
e2e_check_rc "default appdir marker rc" 0 "$E2E_RC"
e2e_check_contains "no -n no -a: appdir = default" "$E2E_OUT" "default/upper/marker"
e2e_run e2e_exec cat /run/cage/pid
e2e_check "no -n: default pidfile /run/cage/pid" test -n "$E2E_OUT"
kill_session
i=0
while [ "$i" -lt 100 ]; do
  if e2e_exec test ! -e /run/cage/pid 2>/dev/null; then
    break
  fi
  i=$(( i + 1 ))
  sleep 0.1
done
e2e_check "default pidfile removed by -k" test "$i" -lt 100

# custom -a with -U: upperdir = -U, workdir stays ${appdir}/work.
e2e_exec_d sh -c 'setsid cage -l /altroot -a /var/lib/cage/ad4 -n ad4 -U /var/lib/cage/up4 -- /bin/sh -c "touch /marker; sleep 60" >/tmp/ad4.log 2>&1 </dev/null'
sleep 2
e2e_run e2e_exec sh -c 'ls /var/lib/cage/up4/marker /var/lib/cage/ad4/work 2>/dev/null'
e2e_check_rc "custom -a -U layout rc" 0 "$E2E_RC"
e2e_check_contains "-U overrides appdir/upper" "$E2E_OUT" "up4/marker"
e2e_check_contains "-a -U keeps appdir work" "$E2E_OUT" "ad4/work"
e2e_run e2e_exec sh -c 'test ! -e /var/lib/cage/ad4/upper && printf NOUPPER'
e2e_check_contains "-U set: no appdir/upper" "$E2E_OUT" "NOUPPER"
e2e_run e2e_exec sh -c 'test ! -e /var/lib/cage/app-ad4 && printf ABSENT'
e2e_check_contains "-a set: no app-\${name} appdir" "$E2E_OUT" "ABSENT"
kill_session -n ad4
e2e_wait_session_gone ad4

# custom -p without -n: pidfile = -p, appdir = -a.
e2e_exec_d sh -c 'setsid cage -l /altroot -a /var/lib/cage/ad5 -p /run/cage-ad5.pid -- /bin/sh -c "touch /marker; sleep 60" >/tmp/ad5.log 2>&1 </dev/null'
sleep 2
e2e_run e2e_exec sh -c 'ls /var/lib/cage/ad5/upper/marker 2>/dev/null'
e2e_check_rc "custom -a -p marker rc" 0 "$E2E_RC"
e2e_run e2e_exec cat /run/cage-ad5.pid
e2e_check "-p writes its custom pidfile" test -n "$E2E_OUT"
e2e_run e2e_exec sh -c 'test ! -e /run/cage/pid && printf NOPID'
e2e_check_contains "no -n: default pidfile unused" "$E2E_OUT" "NOPID"
kill_session -p /run/cage-ad5.pid
i=0
while [ "$i" -lt 100 ]; do
  if e2e_exec test ! -e /run/cage-ad5.pid 2>/dev/null; then
    break
  fi
  i=$(( i + 1 ))
  sleep 0.1
done
e2e_check "custom pidfile removed by -k" test "$i" -lt 100

# -p with -a and -n: -n is obsolete, the pidfile follows -p only.
e2e_exec_d sh -c 'setsid cage -l /altroot -a /var/lib/cage/ad6 -n ad6 -p /run/cage-ad6.pid -- /bin/sh -c "touch /marker; sleep 60" >/tmp/ad6.log 2>&1 </dev/null'
sleep 2
e2e_run e2e_exec sh -c 'ls /var/lib/cage/ad6/upper/marker 2>/dev/null'
e2e_check_rc "custom -a -n -p marker rc" 0 "$E2E_RC"
e2e_run e2e_exec cat /run/cage-ad6.pid
e2e_check "-p wins over -n for the pidfile" test -n "$E2E_OUT"
e2e_run e2e_exec sh -c 'test ! -e /run/cage/ad6.pid && printf NOPID'
e2e_check_contains "-n obsolete when -p given" "$E2E_OUT" "NOPID"
kill_session -p /run/cage-ad6.pid
i=0
while [ "$i" -lt 100 ]; do
  if e2e_exec test ! -e /run/cage-ad6.pid 2>/dev/null; then
    break
  fi
  i=$(( i + 1 ))
  sleep 0.1
done
e2e_check "custom pidfile (with -n) removed by -k" test "$i" -lt 100

# --- samefs: drop the appdir tmpfs so the appdir shares the lowerdir's fs --

docker exec "$CAGE_CTR" umount /var/lib/cage

# default appdir -> /var/lib/cage-loop/app-${name}.
e2e_exec_d sh -c 'setsid cage -n ad7 -- /bin/sh -c "sleep 60" >/tmp/ad7.log 2>&1 </dev/null'
sleep 3
e2e_run e2e_exec cat /tmp/ad7.log
e2e_check_contains "default samefs logs ext4 loopback" "$E2E_OUT" "using ext4 loopback"
e2e_run e2e_exec sh -c 'test -d /var/lib/cage-loop/app-ad7/upper && printf LOOPDIR'
e2e_check_contains "default appdir relocated to cage-loop" "$E2E_OUT" "LOOPDIR"
kill_session -n ad7
e2e_wait_session_gone ad7

# custom -a (single-level, at the filesystem root): the parent is the appdir
# itself, so the loopdir is <appdir>-loop and the leaf stays under it.
e2e_exec_d sh -c 'setsid cage -a /ad8custom -n ad8 -- /bin/sh -c "touch /marker; sleep 60" >/tmp/ad8.log 2>&1 </dev/null'
sleep 3
e2e_run e2e_exec sh -c 'ls /ad8custom-loop/ad8custom/upper/marker 2>/dev/null'
e2e_check_rc "custom -a single-level samefs marker rc" 0 "$E2E_RC"
e2e_check_contains "custom -a single-level: leaf under own -loop" "$E2E_OUT" "ad8custom-loop/ad8custom/upper/marker"
e2e_run e2e_exec sh -c 'test -f /ad8custom-loop.img && printf IMG'
e2e_check_contains "custom -a single-level: image beside loopdir" "$E2E_OUT" "IMG"
kill_session -n ad8
e2e_wait_session_gone ad8

# custom -a, multi-level outside /var/lib: loopdir = parent + -loop, with the
# computed upperdir/workdir as leaves; the missing parent chain is created.
e2e_exec_d sh -c 'setsid cage -a /other/dir/xpto -n ad10 -- /bin/sh -c "touch /marker; sleep 60" >/tmp/ad10.log 2>&1 </dev/null'
sleep 3
e2e_run e2e_exec sh -c 'ls /other/dir-loop/xpto/upper/marker /other/dir-loop/xpto/work 2>/dev/null'
e2e_check_rc "custom -a multi-level samefs layout rc" 0 "$E2E_RC"
e2e_check_contains "multi-level -a: leaf under parent-loop" "$E2E_OUT" "dir-loop/xpto/upper/marker"
e2e_check_contains "multi-level -a: computed workdir follows" "$E2E_OUT" "dir-loop/xpto/work"
e2e_run e2e_exec sh -c 'test -f /other/dir-loop.img && printf IMG'
e2e_check_contains "multi-level -a: image /other/dir-loop.img" "$E2E_OUT" "IMG"
kill_session -n ad10
e2e_wait_session_gone ad10

# -a whose parent already ends in -loop: used as-is — no -loop-loop.
e2e_exec_d sh -c 'setsid cage -a /tmp2/dir-loop/xpto -n ad11 -- /bin/sh -c "touch /marker; sleep 60" >/tmp/ad11.log 2>&1 </dev/null'
sleep 3
e2e_run e2e_exec sh -c 'ls /tmp2/dir-loop/xpto/upper/marker 2>/dev/null'
e2e_check_rc "-loop parent samefs marker rc" 0 "$E2E_RC"
e2e_check_contains "-loop parent kept as-is" "$E2E_OUT" "dir-loop/xpto/upper/marker"
e2e_run e2e_exec sh -c 'test -f /tmp2/dir-loop.img && test ! -e /tmp2/dir-loop-loop.img && printf NODOUBLE'
e2e_check_contains "no -loop-loop image" "$E2E_OUT" "NODOUBLE"
kill_session -n ad11
e2e_wait_session_gone ad11

# appdir inside the already-mounted default loopdir: same path, upper lands on
# the existing loop; no relocation nesting.
e2e_exec_d sh -c 'setsid cage -a /var/lib/cage-loop/second -n ad12 -- /bin/sh -c "touch /marker; sleep 60" >/tmp/ad12.log 2>&1 </dev/null'
sleep 3
e2e_run e2e_exec sh -c 'ls /var/lib/cage-loop/second/upper/marker 2>/dev/null'
e2e_check_rc "in-loopdir -a marker rc" 0 "$E2E_RC"
e2e_check_contains "in-loopdir -a: upper on the existing loop" "$E2E_OUT" "second/upper/marker"
e2e_run e2e_exec sh -c 'test ! -e /var/lib/cage-loop-loop && printf NONEST'
e2e_check_contains "no -loop-loop dir" "$E2E_OUT" "NONEST"
kill_session -n ad12
e2e_wait_session_gone ad12

# custom -w outside the derived loopdir on samefs: the workdir cannot
# accompany the relocated upper (kernel: same mount as upperdir) — fatal.
e2e_run e2e_exec cage -w /myw -n adw -- /bin/sh -c true
e2e_check_rc "custom -w outside loopdir fails" 1 "$E2E_RC"

# custom -w under the loopdir: kept as-is; the overlay work lives inside it
# and the computed appdir/work is not created.
e2e_exec_d sh -c 'setsid cage -w /var/lib/cage-loop/mywork -n adw2 -- /bin/sh -c "touch /marker; sleep 60" >/tmp/adw2.log 2>&1 </dev/null'
sleep 3
e2e_run e2e_exec sh -c 'test -f /var/lib/cage-loop/app-adw2/upper/marker && test -d /var/lib/cage-loop/mywork'
e2e_check_rc "custom -w under loopdir layout rc" 0 "$E2E_RC"
e2e_run e2e_exec sh -c 'test -f /var/lib/cage-loop/app-adw2/upper/marker && printf UPPER'
e2e_check_contains "custom -w under loopdir: upper on loop" "$E2E_OUT" "UPPER"
e2e_run e2e_exec sh -c 'test -d /var/lib/cage-loop/mywork && printf KEPT'
e2e_check_contains "custom -w under loopdir kept" "$E2E_OUT" "KEPT"
e2e_run e2e_exec sh -c 'test ! -e /var/lib/cage-loop/app-adw2/work && printf NOWORK'
e2e_check_contains "computed workdir not created when -w kept" "$E2E_OUT" "NOWORK"
kill_session -n adw2
e2e_wait_session_gone adw2

# custom -a with custom -w under its (fresh, derived) loopdir: both kept.
e2e_exec_d sh -c 'setsid cage -a /tmp3/dir/xpto -w /tmp3/dir-loop/othework -n adw3 -- /bin/sh -c "touch /marker; sleep 60" >/tmp/adw3.log 2>&1 </dev/null'
sleep 3
e2e_run e2e_exec sh -c 'test -f /tmp3/dir-loop/xpto/upper/marker && test -d /tmp3/dir-loop/othework'
e2e_check_rc "custom -a + -w under its loopdir layout rc" 0 "$E2E_RC"
e2e_run e2e_exec sh -c 'test -f /tmp3/dir-loop/xpto/upper/marker && printf UPPER'
e2e_check_contains "custom -a + kept -w: upper on loop" "$E2E_OUT" "UPPER"
e2e_run e2e_exec sh -c 'test -d /tmp3/dir-loop/othework && printf KEPT'
e2e_check_contains "custom -a + kept -w: workdir kept" "$E2E_OUT" "KEPT"
kill_session -n adw3
e2e_wait_session_gone adw3

# -U without -a on samefs: the upperdir shares the lowerdir's filesystem --
# a contract violation (cli-arguments.md -U), rejected by the overlay guard.
e2e_run e2e_exec cage -n ad9 -U /ad9up -- /bin/sh -c true
e2e_check_rc "samefs custom -U fails" 1 "$E2E_RC"
e2e_check_contains "samefs custom -U: overlay guard" "$E2E_OUT" \
  "overlay-on-overlay is not supported"

# custom -U with -a on samefs: the -U guard wins; -a does not bypass it.
e2e_run e2e_exec cage -a /other/dir3/xpto -U /up9x -n ad9x -- /bin/sh -c true
e2e_check_rc "samefs custom -U with -a fails" 1 "$E2E_RC"
e2e_check_contains "samefs custom -U with -a: overlay guard" "$E2E_OUT" \
  "overlay-on-overlay is not supported"

# Restore the appdir tmpfs and drop every derived loopback.
docker exec "$CAGE_CTR" sh -c '
  for mp in /var/lib/cage-loop /other/dir-loop /tmp2/dir-loop /tmp3/dir-loop \
    /ad8custom-loop; do
    umount "$mp" 2>/dev/null || :
  done
  losetup -a 2>/dev/null | grep -- "-loop" | sed "s/:.*//" |
    while IFS= read -r d; do
      losetup -d "$d" 2>/dev/null
    done
  rm -f /var/lib/cage-loop.img /other/dir-loop.img /tmp2/dir-loop.img \
    /tmp3/dir-loop.img /ad8custom-loop.img 2>/dev/null
  rm -rf /other /tmp2 /tmp3 /ad8custom /ad8custom-loop /myw /ad9up /up9x 2>/dev/null
  mount -t tmpfs -o size=1G tmpfs /var/lib/cage'

# samefs must not trigger when lowerdir is not a parent of upperdir even if they
# share the same filesystem. Put both under a normal tmpfs (/optfs) so the
# container's overlay root does not make the appdir an overlay-on-overlay.
docker exec "$CAGE_CTR" sh -c '
  umount /optfs 2>/dev/null || :
  rm -rf /optfs /optfs-loop /optfs-loop.img
  mkdir -p /optfs
  mount -t tmpfs -o size=1G tmpfs /optfs
  cp -a /altroot /optfs/lower
  mkdir -p /optfs/appdir
'
e2e_run e2e_exec cage -l /optfs/lower -a /optfs/appdir -- /bin/sh -c 'echo OK'
e2e_check_rc "sibling lower/upper samefs should not loopback rc" 0 "$E2E_RC"
e2e_check_contains "sibling lower/upper runs" "$E2E_OUT" "OK"
e2e_run e2e_exec sh -c 'test ! -e /optfs-loop && test ! -e /optfs.appdir-loop && printf NOLOOP'
e2e_check_contains "no loop created for sibling lower/upper" "$E2E_OUT" "NOLOOP"
e2e_run e2e_exec sh -c 'test -d /optfs/appdir/upper && printf HASUPPER'
e2e_check_contains "upper created under appdir" "$E2E_OUT" "HASUPPER"

# lowerdir is a parent of the appdir but the appdir is on a different
# filesystem (tmpfs mounted under the lower tree) — loopback must not trigger.
docker exec "$CAGE_CTR" sh -c '
  umount /optfs2/root/app 2>/dev/null || :
  umount /optfs2 2>/dev/null || :
  rm -rf /optfs2 /optfs2-loop /optfs2-loop.img
  mkdir -p /optfs2
  mount -t tmpfs -o size=1G tmpfs /optfs2
  cp -a /altroot /optfs2/root
  mkdir -p /optfs2/root/app
  mount -t tmpfs tmpfs /optfs2/root/app
'
e2e_run e2e_exec cage -l /optfs2/root -a /optfs2/root/app -- /bin/sh -c 'echo OK2'
e2e_check_rc "lower parent of appdir but appdir on tmpfs rc" 0 "$E2E_RC"
e2e_check_contains "lower parent of appdir on tmpfs runs" "$E2E_OUT" "OK2"
e2e_run e2e_exec sh -c 'test ! -e /optfs2/root-loop && test ! -e /optfs2/root/app-loop && printf NOLOOP'
e2e_check_contains "no loop for tmpfs appdir under lower" "$E2E_OUT" "NOLOOP"

docker exec "$CAGE_CTR" sh -c '
  umount /optfs2/root/app 2>/dev/null || :
  while umount /optfs2 2>/dev/null; do :; done
  while umount /optfs 2>/dev/null; do :; done
  rm -rf /optfs /optfs2 /optfs-loop /optfs2-loop /optfs-loop.img /optfs2-loop.img
  rm -rf /tmp/test /tmp/test-loop /tmp/test-loop.img'

# Appdir on its own ordinary filesystem (a tmpfs mounted at /appfs) with the
# lowerdir and the appdir as siblings under it. Both layouts must run without a
# loopback: (A) lowerdir empty and the rootfs planted in the appdir's upper,
# (B) lowerdir holding the rootfs and the upper computed.
docker exec "$CAGE_CTR" sh -c '
  umount /appfs 2>/dev/null || :
  rm -rf /appfs /appfs-loop /appfs-loop.img
  mkdir -p /appfs
  mount -t tmpfs -o size=1G tmpfs /appfs
  mkdir -p /appfs/app/lower
  cp -a /altroot /appfs/app/upper
'
e2e_run e2e_exec cage -l /appfs/app/lower -a /appfs/app -- /bin/sh -c 'echo OKA'
e2e_check_rc "samefs empty lower, seeded upper rc" 0 "$E2E_RC"
e2e_check_contains "samefs empty lower, seeded upper runs" "$E2E_OUT" "OKA"
e2e_run e2e_exec sh -c 'test ! -e /appfs-loop && test ! -e /appfs/app-loop && printf NOLOOP'
e2e_check_contains "no loop for empty lower, seeded upper" "$E2E_OUT" "NOLOOP"

docker exec "$CAGE_CTR" sh -c '
  umount /appfs 2>/dev/null || :
  rm -rf /appfs /appfs-loop /appfs-loop.img
  mkdir -p /appfs
  mount -t tmpfs -o size=1G tmpfs /appfs
  mkdir -p /appfs/app
  cp -aT /altroot /appfs/app/lower
'
e2e_run e2e_exec cage -l /appfs/app/lower -a /appfs/app -- /bin/sh -c 'echo OKB'
e2e_check_rc "samefs full lower, computed upper rc" 0 "$E2E_RC"
e2e_check_contains "samefs full lower, computed upper runs" "$E2E_OUT" "OKB"
e2e_run e2e_exec sh -c 'test ! -e /appfs-loop && test ! -e /appfs/app-loop && printf NOLOOP'
e2e_check_contains "no loop for full lower, computed upper" "$E2E_OUT" "NOLOOP"

docker exec "$CAGE_CTR" sh -c '
  umount /appfs 2>/dev/null || :
  rm -rf /appfs /appfs-loop /appfs-loop.img'

e2e_end

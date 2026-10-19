# Manual Test: Kernel Module Loading

How to probe cage's module loading (`modprobe_load()` — see
[internals.md §Modprobe Loading Strategy](internals.md#modprobe-loading-strategy))
in a kernel where the module is **not yet loaded**.

## Why the e2e suite cannot do this

The `e2e/` container shares the daemon's kernel. In that kernel `veth` is
already loaded (Docker created the container's own veth pair) and the container
has no `/lib/modules` at all, so a `-N` run never reaches cage's module loading
path. To exercise it, cage must run on a kernel that boots without `veth`, with
a module tree that cage can read. This procedure builds exactly that: an Alpine
netboot kernel and initrd, a minimal squashfs rootfs, and the netboot modloop as
a read-only disk, all under QEMU/KVM inside a container.

Use this only when module loading itself is under test; every other behaviour
belongs in `e2e/` (see [perform_actions.md §perform manual test](perform_actions.md#perform-manual-test)).

Validated on Alpine 3.24.2 netboot, kernel `6.18.52-0-lts`, with KVM inside a
remote Docker daemon. The rootfs is disposable and its root password is the
literal `pass`; never expose the SSH port beyond the loopback of the machine
running `dbclient`.

## 1. Container

The container needs `/dev/kvm` and the QEMU/squashfs toolchain:

```sh
docker run -d --name cage-modprobe --device /dev/kvm \
  -p 127.0.0.1:2222:2222 alpine:3.24.2 sleep infinity
docker exec cage-modprobe apk add --no-cache \
  qemu-system-x86_64 qemu-img squashfs-tools wget tar openssl dropbear-dbclient
```

The published port is only reachable when the daemon is local. With a remote
daemon (the usual setup here) run `dbclient` **inside** the container as shown
below; the port publication is then harmless. `dropbear-dbclient` provides
`dbclient`; Alpine's `dropbear` package ships the server and `dropbearkey`
only.

If the daemon has no `/dev/kvm`, stop and report the blocker — without KVM the
VM boots too slowly to be useful.

## 2. Kernel, initrd and modloop

The netboot tarball supplies the three files; download it on the host and ship
them with `docker cp` (`docker cp` works against a remote daemon, bind mounts
into a remote daemon do not).

```sh
cd /tmp/opencode
wget -q -O alpine-netboot.tar.gz \
  https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/x86_64/alpine-netboot-3.24.2-x86_64.tar.gz
mkdir -p netboot && tar -xzf alpine-netboot.tar.gz -C netboot
docker cp netboot/boot/vmlinuz-lts   cage-modprobe:/tmp/vmlinuz-lts
docker cp netboot/boot/initramfs-lts cage-modprobe:/tmp/initramfs-lts
docker cp netboot/boot/modloop-lts   cage-modprobe:/tmp/modloop-lts
```

The initramfs carries only a boot subset of modules (no `veth`, no `ext4`), so
the guest root **must** be squashfs; an ext4 disk cannot be mounted by this
initrd. The initramfs always modprobes `loop squashfs` (plus the rootfs type),
so `loop` is loaded from the first second and cannot be probed unloaded (see
[§Probe results](#probe-results)).

## 3. Guest rootfs

Build a minimal rootfs in the container: the Alpine minirootfs, the base layout
so `getty`/`ip`/`sshd` exist, and an inittab that starts them directly.

```sh
docker exec cage-modprobe sh -c '
set -eu
cd /tmp
wget -q -O minirootfs.tar.gz \
  https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/x86_64/alpine-minirootfs-3.24.2-x86_64.tar.gz
rm -rf /rf && mkdir /rf && tar -xzf minirootfs.tar.gz -C /rf
apk add --root /rf --repository https://dl-cdn.alpinelinux.org/alpine/v3.24/main \
  alpine-base dropbear
H=$(openssl passwd -6 pass)
sed -i "s|^root:[^:]*:|root:$H:|" /rf/etc/shadow
cat > /rf/etc/inittab <<"IT"
::wait:/sbin/ip link set eth0 up
::wait:/sbin/ip addr add 10.0.2.15/24 dev eth0
::wait:/sbin/ip route add default via 10.0.2.2
::respawn:/sbin/getty -L 115200 ttyS0 vt100
::respawn:/usr/sbin/dropbear -R -p 22
::ctrlaltdel:/sbin/reboot
IT
rm -f /rootfs.squashfs
mksquashfs /rf /rootfs.squashfs -comp xz -noappend -all-root
'
```

Notes:

- `10.0.2.15/24` and `10.0.2.2` are QEMU's user-mode network (slirp) guest and
  gateway addresses; the static address avoids `udhcpc`, which needs
  `af_packet` loaded and is not worth arranging.
- `dropbear -R` writes host keys at startup. They are new on every boot, so a
  `dbclient` client that cached a key from a previous boot aborts with a host
  key mismatch; remove `~/.ssh/known_hosts` before connecting (see step 5).
- OpenRC is installed by `alpine-base` but not used: the netboot-derived rootfs
  has no runlevel symlinks, and this VM needs only an address, a serial console
  and sshd. The inittab starts all three directly.

## 4. Boot

Two read-only virtio disks: `vda` is the rootfs (the initramfs mounts it at
`/media/root-ro` and builds an overlay root on top), `vdb` is the modloop, which
holds the full module tree. Serial goes to a TCP socket so it can be read from
the container without a console client.

```sh
docker exec -d cage-modprobe sh -c 'exec qemu-system-x86_64 \
  -name cage-modprobe -enable-kvm -m 2048 -smp 2 \
  -display none -serial tcp:127.0.0.1:5555,server=on,wait=off \
  -kernel /tmp/vmlinuz-lts -initrd /tmp/initramfs-lts \
  -append "console=ttyS0 root=/dev/vda rootfstype=squashfs overlaytmpfs=yes modules=squashfs,overlay,virtio_pci,virtio_blk,virtio_net" \
  -drive file=/rootfs.squashfs,if=virtio,format=raw,readonly=on \
  -drive file=/tmp/modloop-lts,if=virtio,format=raw,readonly=on \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22 \
  -device virtio-net-pci,netdev=n0 >/tmp/qemu.log 2>&1'
```

Boot to the login prompt takes a few seconds. To watch or use the console
(fallback when SSH does not come up), relay the serial stream inside the
container: append to `/tmp/cmds` to send input, read `/tmp/console.out` for
output. Log in as `root` / `pass`.

```sh
docker exec cage-modprobe sh -c \
  ': > /tmp/cmds; : > /tmp/console.out; setsid sh -c "tail -f /tmp/cmds | nc 127.0.0.1 5555 > /tmp/console.out 2>&1" </dev/null >/dev/null 2>&1 &'
docker exec cage-modprobe cat /tmp/console.out
docker exec cage-modprobe sh -c 'printf "root\n" >> /tmp/cmds'
```

## 5. Connect

`dbclient` runs inside the container, reaching QEMU's forwarded port on its own
loopback:

```sh
docker exec cage-modprobe sh -c \
  'rm -f ~/.ssh/known_hosts; DROPBEAR_PASSWORD=pass dbclient -y -p 2222 root@127.0.0.1 "uname -r"'
```

## 6. Guest preparation

Cage needs three things inside the guest:

1. the module tree at `/lib/modules/$(uname -r)` — the modloop nests its
   payload one level deeper than the kernel expects
   (`modules/<kver>/...` inside the squashfs), so bind the nested directory into
   place;
2. a read-only sandbox lower (`-l`): the guest's own read-only root at
   `/media/root-ro`;
3. an appdir whose upper/work are on a real tmpfs — the guest root is overlayfs
   and cage rejects an overlayfs upperdir (`overlay-on-overlay is not
   supported`).

```sh
docker exec cage-modprobe sh -c 'DROPBEAR_PASSWORD=pass dbclient -y -p 2222 root@127.0.0.1 "sh -s" <<"SH"
set -eu
KVER=$(uname -r)
mkdir -p /mnt/ml "/lib/modules/$KVER"
mount -t squashfs -o loop /dev/vdb /mnt/ml
mount --bind "/mnt/ml/modules/$KVER" "/lib/modules/$KVER"
mkdir -p /altroot
mount --bind /media/root-ro /altroot
mkdir -p /var/lib/cage
mount -t tmpfs -o size=512M tmpfs /var/lib/cage
printf "nameserver 1.1.1.1\n" > /etc/resolv.conf
lsmod | grep veth || echo veth_unloaded
SH'
```

The last line is the precondition: `veth` must be unloaded before the probe.

## 7. Install cage

The binary is the same static, setuid-root build the e2e harness uses:

```sh
docker cp bin/cage cage-modprobe:/tmp/cage
docker exec cage-modprobe sh -c \
  'DROPBEAR_PASSWORD=pass dbclient -y -p 2222 root@127.0.0.1 "cat > /root/cage; chown root:root /root/cage; chmod 4755 /root/cage" < /tmp/cage'
```

## 8. Probe

Run as root inside the guest. Step 2 proves cage loads `veth` itself; steps 3–4
prove the failure mode when the module tree is missing; steps 5–6 prove the
failure is not sticky.

```sh
docker exec cage-modprobe sh -c 'DROPBEAR_PASSWORD=pass dbclient -y -p 2222 root@127.0.0.1 "sh -s" <<"SH"
KVER=$(uname -r); DEP=/lib/modules/$KVER
echo "=== 1. baseline: veth must be unloaded ==="
lsmod | grep veth || echo veth_unloaded
echo "=== 2. cage -N with module tree present ==="
/root/cage -N -l /altroot -n m1 -u 1000 -g 1000 -- /bin/sh -c "ip -o addr show eth0"
echo cage_rc=$?
lsmod | grep veth || echo veth_still_unloaded
echo "=== 3. unload veth and hide the module tree ==="
rmmod veth; echo rmmod_rc=$?
umount "$DEP"; echo umount_rc=$?
ls "$DEP/modules.dep" 2>&1
echo "=== 4. cage -N with module tree missing (must fail loudly) ==="
/root/cage -N -l /altroot -n m2 -u 1000 -g 1000 -- /bin/sh -c true
echo cage_rc=$?
echo "=== 5. restore module tree, veth still unloaded ==="
mount --bind "/mnt/ml/modules/$KVER" "$DEP"; echo bind_rc=$?
lsmod | grep veth || echo veth_unloaded
echo "=== 6. cage -N again (must load veth itself) ==="
/root/cage -N -l /altroot -n m3 -u 1000 -g 1000 -- /bin/sh -c "ip -o addr show eth0"
echo cage_rc=$?
lsmod | grep veth || echo veth_still_unloaded
SH'
```

### Probe results

Expected on the validated kernel (`6.18.52-0-lts`):

| Step | Condition | Expected |
|------|-----------|----------|
| 1 | fresh boot | `veth_unloaded` |
| 2 | module tree present, `veth` unloaded | `cage_rc=0`; sandbox `eth0` has `172.24.0.0/31`; `lsmod` shows `veth` afterwards — cage loaded it |
| 3 | `rmmod veth`, module tree hidden | `rmmod_rc=0`, `umount_rc=0`, `modules.dep` not found |
| 4 | module tree missing | `cage: unable to open /lib/modules/<kver>/modules.builtin`, `cage_rc=1`; `veth` stays unloaded |
| 5 | tree restored, `veth` still unloaded | `bind_rc=0`, `veth_unloaded` |
| 6 | tree present again | `cage_rc=0`; sandbox `eth0` up; `lsmod` shows `veth` — same result as step 2 |

Interpretation: cage's module loading path is correct when the module files are
available (`modules.builtin`, `modules.dep` and the `.ko` under
`/lib/modules/$(uname -r)`); cage loads `veth` with `init_module(2)` and the
`-N` run then completes normally. When the module tree is missing, cage fails
loudly *before* any netlink work with the error above and exit status 1 — it
does not silently create a broken namespace. The commonly observed
"`-N` failure without the module loaded" is this missing-tree case (e.g. a
plain `alpine:<ver>` container, where `/lib/modules` does not exist); it is not
a defect in the loading path.

`loop` is always loaded by this initramfs (it mounts the squashfs root) and
`overlay` is loaded for the initramfs overlay root, so neither can be observed
unloaded here; unmounting everything that pins them first is not worth the
fragility. Their load points are exercised indirectly by any overlay/loop run
on a kernel where they are built in.

## 9. Cleanup

```sh
docker rm -f cage-modprobe
```

Everything (VM disk, rootfs, module tree) lives inside the container; the host
sees no state change. `dbclient` caches host keys in the container's
`~/.ssh/known_hosts`, which disappears with the container.

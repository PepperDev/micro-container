# Micro-container
If you are tired of the huge footprint Docker deamon does on your system it is made for you, a simply and easy container isolation software, you can run applications on jail without requiring any deamon running forever.

**Warning**: this software is considered to be insecure, it can let unprivileged users to run instructions as root and potentially damaging your system, use it at your own risk.

## Download
Download the avaiable version at [releases](https://github.com/PepperDev/micro-container/releases) page.

    $ wget -qO- https://github.com/PepperDev/micro-container/releases/download/v0.3.0-rc4/cage.amd64 |
      sudo install -v -o 0 -g 0 -m 04755 /dev/stdin /usr/local/bin/cage

## Build
`$ make`

## Install
`$ sudo make install`

## Examples
To run a container based on your own host system:

    $ cage
    # or
    $ cage -n name

To run an Alpine based system:

    rootdir=/var/lib/microcontainer-root/alpine
    if [ ! -d "$rootdir" ]; then
      sudo mkdir -p "$rootdir"
      wget -qO- \
        'https://dl-cdn.alpinelinux.org/alpine/v3.17/releases/x86_64/alpine-minirootfs-3.17.1-x86_64.tar.gz' |
        sudo tar -xzf- -C "$rootdir"
    fi
    # Then simply:
    cage -l "$rootdir" -n alpine
    # Easy peasy, if you want other name than alpine, just inform the root
    cage -l "$rootdir" -n other-container-name

If you want an unprivileged Alpine system you can do instead:

    rootdir=/var/lib/microcontainer-root/alpine
    if [ ! -d "$rootdir" ]; then
      tmp="$(mktemp -d)"
      mkdir "$tmpdir/none" "$tmpdir/root"
      wget -qO- \
        'https://dl-cdn.alpinelinux.org/alpine/v3.17/releases/x86_64/alpine-minirootfs-3.17.1-x86_64.tar.gz' |
        sudo tar -xzf- -C "$tmpdir/root"
      cage \
        -a "$tmpdir/app" \
        -p "$tmpdir/pid" \
        -l "$tmpdir/none" \
        -U "$tmpdir/root" \
        -- \
        /bin/sh -s <<\EOF
    apk add --no-cache sudo
    addgroup -g 1000 user
    adduser -D -G user -u 1000 -s /bin/ash -h /home/user user
    printf 'user ALL=(ALL:ALL) NOPASSWD: ALL\n' > /etc/sudoers.d/nopass
    EOF
      sudo mkdir -p "$(dirname "$rootdir")"
      sudo rm -rf "${rootdir}.tmp"
      sudo mv "$tmpdir/root" "${rootdir}.tmp"
      sudo mv "${rootdir}.tmp" "$rootdir"
      sudo rm -rf "$tmpdir"
    fi
    # Then simply:
    cage -l "$rootdir" -u 1000 -g 1000 -n alpine

If you want a gui capable user:

    cage -G -u "$(id -u)" -g "$(id -g):$(id -G | tr ' ' ':')"

If you want an unprivileged Alpine system with dynamic user creation you can do instead:

    rootdir=/var/lib/microcontainer-root/alpine
    if [ ! -d "$rootdir" ]; then
      sudo mkdir -p "$rootdir"
      wget -qO- \
        'https://dl-cdn.alpinelinux.org/alpine/v3.17/releases/x86_64/alpine-minirootfs-3.17.1-x86_64.tar.gz' |
        sudo tar -xzf- -C "$rootdir"
    fi
    # Then simply:
    cage -l "$rootdir" -i /proc/self/fd/3 -u "$(id -u)" -g "$(id -g)" 3<<EOF
    if [ ! -f /usr/bin/sudo ]; then
      apk add --no-cache sudo
      addgroup -g $(id -g) user
      adduser -D -G user -u $(id -u) -s /bin/ash -h /home/user user
      printf 'user ALL=(ALL:ALL) NOPASSWD: ALL\n' > /etc/sudoers.d/nopass
    fi
    EOF

## Usage
    cage [options...] [--] [command...]

    -a appdir
        application base dir default to "/var/lib/microcontainer/default"
    if application name is empty or "/var/lib/microcontainer/app-${name}"
    if application name is given, used only to compute upperdir and workdir

    -c currentdir
        directory command will run inside the container, default to "/"

    -e env
        add environment variable in the format key=value

    -G
        setup environment and volumes to support graphical applications

    -g group
        group name or uid to run command, default to 0, can contain
    multiples values separated by colon

    -h
        print help

    -i initscript
        script to be run inside the container as root before command

    -k
        stop/kill current instance

    -l
        lower directory default to "/"

    -n name
        applicaion name used only to compute appdir and pidfile

    -p pidfile
        pid file default to "/run/microcontainer/pid" if application name
    is empty or "/run/microcontainer/${name}.pid" if application name is
    given

    -U upperdir
        upper directory default to "${appdir}/upper", must not have
    lowerdir as a the parent directory in the same filesystem, otherwise a
    loop in "${upperdir}/../.." will be created if possible

    -u user
        user name or uid to run command, default to 0, can follow groups
    separated by colon

    -V
        print version

    -v volume
        map a volume from the host to the instance, if contains a colon
    the value before is considered in the host and after in the instance

    -w workdir
        overlay work directory default to "${appdir}/work", must be in the
    same filesystem as upperdir

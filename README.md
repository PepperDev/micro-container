# Micro-container
If you are tired of the huge footprint Docker deamon does on your system it is made for you, a simply and easy container isolation software, you can run applications on jail without requiring any deamon running forever.

## Examples
To run a container based on your own host system:

    $ container
    # or
    $ container -n name

To run an Alpine based system:

    if [ ! -d /var/lib/micro-container/root-alpine ]; then
        sudo mkdir -p /var/lib/micro-container/root-alpine
        wget -qO- \
            'https://github.com/gliderlabs/docker-alpine/raw/rootfs/library-3.9/x86_64/versions/library-3.9/x86_64/rootfs.tar.xz' |
            sudo tar -xJv -C /var/lib/micro-container/root-alpine
    fi
    # Then simply:
    $ container alpine
    # Easy peasy, if you want other name than alpine, just inform the root
    $ container -l /var/lib/micro-container/root-alpine -n other-container-name

## Usage
	container [-] [-r rootdir] [-b basedir] [-l lowerdir] [-n name] [-w workdir] [-u user:group] [[-v hostvolume:guestvolume]...] [-i initscript] [-s shutdownscript] [[--] command args...]

	rootdir - default to "/var/lib/micro-container"

	basedir - default to "$HOME/.app"

	lowerdir - default to "/"

	name - default to blank

	workdir - default to blank

	user:group - default to the current user and group

	hostvolume:guestvolume - default to empty

	initscript - default to blank

	shutdownscript - default to blank

	command - default to blank

## Download
Not avaiable yet, comming soon...

## Build
`$ make`

## Install
`$ sudo make install`

## Notes
The only parameter that can be set multiple times is `-v`, all others will take the last value if set more than once.

If not exists `rootdir` is created with mode `0700` and root as owner.

If not exists `basedir` is created with default mode and current user as owner.

`rootdir` and `basedir` can be the same, in that case the `basedir` creation take precedence.

If `name` is blank `targetdir` will be `${rootdir}/app/root` otherwise it is `${rootdir}/app-${name}/root`.

If `name` is blank `appdir` will be `${basedir}/app` otherwise it is `${basedir}/app-${name}`.

The value of `upperdir` will be `${appdir}/upper`.

The value of `workdir` will be `${appdir}/work`.

`workdir` is required to be in the same filesystem as `upperdir`

Multiple values can be specified on `lowerdir` separated by colon, the first value will be the root, all next is required to be in the same tree.

# Micro-container

If you are tired of the huge footprint Docker deamon does on your system it is made for you, a simply and easy container isolation software, you can run applications on jail without requiring any deamon running forever.

## Usage

To run a container based on your own host system:

    $ container
    # or
    $ container name

To run an Alpine based system:

    if [ ! -d /var/lib/micro-container/root-alpine ]; then
        sudo mkdir -p /var/lib/micro-container/root-alpine
        wget -qO- \
            'https://github.com/gliderlabs/docker-alpine/raw/rootfs/library-3.7/x86_64/versions/library-3.7/x86_64/rootfs.tar.xz' \
            | sudo tar -xJv -C /var/lib/micro-container/root-alpine
    fi
    # Then simply:
    $ container alpine
    # Easy peasy, if you want other name than alpine, just inform the root
    $ container -r /var/lib/micro-container/root-alpine other-container-name

## Download
Not avaiable yet, comming soon...

## Build
`$ make`

## Install
`$ sudo make install`

# cage

Simple, easy container-like process isolation software. Standalone — no daemon, no OCI runtime, minimal footprint.

## Project

- **Maintainer:** Tiago Augusto Pimenta
- **Release page:** [https://github.com/PepperDev/micro-container/releases](https://github.com/PepperDev/micro-container/releases)

## Features

- Linux namespace isolation (PID, mount, network, cgroup)
- No daemon — single-shot execution
- Static binary — zero external runtime dependencies

## Build

Prerequisites: `musl-gcc`, `make`, `indent`, `cppcheck`, `lizard`, `gcov`

    make
    sudo make install

Binary is placed at `bin/cage` and installed to `$(PREFIX)/bin` (default `/usr/local`).

## Usage

    cage [options] [command] [args...]

Examples:

| Command | Description |
|---------|-------------|
| `cage /bin/sh` | Run a shell in an isolated environment |
| `cage -u 1000 -l /mnt/rootfs /bin/bash` | Run as user 1000 with custom lower dir |
| `cage -e PATH=/usr/bin -v /data:/mnt/data /bin/app` | Add env vars and map volumes |
| `cage -G -i /tmp/setup.sh /bin/gui-program` | Init script with GUI support |
| `cage -k -n myapp` | Kill a running instance by name |
| `cage -n myapp /bin/sh`     | Re-enter existing instance (implicit when running) |

See [docs/cli-arguments.md](docs/cli-arguments.md) for full CLI reference.

## License

MIT

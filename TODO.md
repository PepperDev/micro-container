# TODO

## Backlog

- [ ] Mount recursive overlays for non-system partitions (see [docs/lifecycle.md §Container Lifecycle](docs/lifecycle.md#container-lifecycle))
- [ ] Give the e2e docker host an IPv6 upstream so the `-N` IPv6 internet check in `e2e/network.sh` passes (`wget -qO- http://api6.ipify.org`). The `-N` sandbox now has IPv6 addressing, a default route, IPv6 forwarding and NAT66, but the e2e daemon runs in a QEMU slirp VM with no IPv6 forwarding (`docker run --net=host alpine wget -qO- http://api6.ipify.org` fails).

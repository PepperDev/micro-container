#!/bin/sh
# Network namespace isolation (-N): a bridge created inside the sandbox netns
# (docker0, started by dockerd in a nested Docker-in-Docker run) must stay
# inside the sandbox and never appear in the container/host netns. This is a
# regression guard: the isolation held when the test was written.
#
# The scenario needs the sandbox to be able to run dockerd, so docker is
# installed through cage (`apk add docker`) and its state lives on a tmpfs
# volume mounted at /var/lib/docker. The overlay upper must stay on real tmpfs
# (overlay-on-overlay is rejected), which the harness appdir provides.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

# A fresh tmpfs for dockerd's data root, owned by the test user.
e2e_root sh -c '
  umount -l /tmp/docker-volume 2>/dev/null || :
  rm -rf /tmp/docker-volume
  mkdir -p /tmp/docker-volume
  mount -t tmpfs -o size=1G tmpfs /tmp/docker-volume
  chown 1000:1000 /tmp/docker-volume'

# The sandbox must be able to run dockerd, whose nested network needs the
# veth/iptables plumbing cage sets up for -N (cage-nat). The harness only
# installs socat/acl, so best-effort add nftables to the container first.
e2e_root apk add --no-cache nftables >/dev/null 2>&1 || :

# Run dockerd inside a -N -C sandbox and report the sandbox links. The inner
# script is fed to the sandbox shell on stdin, not as a file path: cage mounts
# a fresh tmpfs on the sandbox /tmp, so a container-side file would be hidden.
# The heredoc body is piped to the container (docker exec -i), inherited by
# `sh -c` and then by cage, ending at the sandbox `sh -s`.
#
# This docker daemon runs `docker exec -i` with a heredoc asynchronously: the
# exec returns immediately and the container command keeps running detached, so
# its output is not waitable. Poll the container-side log for the terminal
# marker instead of trusting the exec to have finished.
e2e_exec_i sh -c '
  : > /tmp/net-isolation.log
  cage -l /altroot -v /tmp/docker-volume:/var/lib/docker -N -C -n netiso -- \
    /bin/sh -s > /tmp/net-isolation.log 2>&1
  printf "cage_rc=%s\n" "$?" >> /tmp/net-isolation.log
' <<'INNER'
apk add -q --no-cache docker >/dev/null 2>&1 || { echo "APK-FAILED"; exit 1; }
nohup setsid dockerd --storage-driver overlay2 >/tmp/dockerd.log 2>&1 </dev/null &
i=0
while ! docker info >/dev/null 2>&1; do
  i=$(( i + 1 ))
  if [ "$i" -gt 300 ]; then
    echo "DOCKER-TIMEOUT"
    exit 1
  fi
  sleep 0.1
done
echo "DOCKER-UP"
echo "sandbox-links: $(ip -o link | cut -d: -f2 | tr -d ' ' | tr '\n' ' ')"
INNER

# Wait for the detached run to reach its terminal marker (cage_rc= appears only
# after the inner script finished and cage exited). Cap the wait so a stuck run
# fails rather than hangs the suite.
iso_i=0
while [ "$iso_i" -lt 900 ]; do
  if e2e_exec grep -q 'cage_rc=' /tmp/net-isolation.log 2>/dev/null; then
    break
  fi
  iso_i=$(( iso_i + 1 ))
  sleep 0.1
done

e2e_run e2e_exec cat /tmp/net-isolation.log
e2e_check_contains "sandbox dockerd reached DOCKER-UP" "$E2E_OUT" "DOCKER-UP"
e2e_check_contains "sandbox created docker0 bridge" "$E2E_OUT" "docker0"

# The isolation check: after a sandbox dockerd starts, the container netns must
# not see the sandbox's docker0 bridge. The sandbox docker0 lives only in the
# sandbox netns (dockerd's netns differs from the host's).
e2e_run e2e_root sh -c 'ip -o link | cut -d: -f2 | tr -d " " | tr "\n" " "'
e2e_check_not_contains "host netns does not see sandbox docker0" "$E2E_OUT" "docker0"

e2e_wait_session_gone netiso

e2e_end

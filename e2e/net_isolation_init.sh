#!/bin/sh
# Init-script isolation for the sandbox: an init script must run after cage
# sets up the sandbox netns (-N) and cgroup (-C), so network objects and
# processes it creates land in the sandbox, not the container/host. This test
# starts a bridge (`docker0`) and a background child from the init script, then
# asserts the host netns does not see the bridge and the child inherited the
# sandbox cgroup (`cage-parent`). Both checks fail until the init script runs
# after the netns unshare and cgroup enter.
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

# Baseline: no docker0 on the host before the run.
e2e_root sh -c 'ip link del docker0 2>/dev/null || :'

# Init script (inside the guest root) that creates the bridge and reports the
# netns it ran in plus the links it can see. It also reports its cgroup: with
# "-C", a process spawned by the init script must inherit the sandbox cgroup
# (`cage-parent`), not the host root cgroup.
# shellcheck disable=SC2016 # $() runs when the init script runs, not on the host
e2e_root sh -c '
  cat > /altroot/init_netns.sh <<"S"
#!/bin/sh
ip link add docker0 type bridge 2>&1 || echo INIT_BRIDGE_FAILED
printf "init_ns=%s\n" "$(readlink /proc/self/ns/net)"
printf "init_links=%s\n" "$(ip -o link | cut -d: -f2 | tr -d " " | tr "\n" " ")"
sleep 300 &
sleep 0.3
printf "init_cgroup=%s\n" "$(cat /proc/$!/cgroup)"
kill $! 2>/dev/null || :
S
  chmod 0755 /altroot/init_netns.sh'

e2e_run e2e_exec cage -l /altroot -N -C -n nets -i /init_netns.sh -u 0 -g 0 -- true
e2e_check_rc "init-script run rc" 0 "$E2E_RC"
e2e_check_contains "init script created docker0" "$E2E_OUT" "docker0"
e2e_check_contains "init-script child lands in the sandbox cgroup" "$E2E_OUT" "init_cgroup=0::/cage-parent"

# The isolation check: the bridge created by the init script must live in the
# sandbox netns, so the container/host netns must not see it.
e2e_run e2e_root sh -c 'ip -o link | cut -d: -f2 | tr -d " " | tr "\n" " "'
e2e_check_not_contains "host netns does not see init-script docker0" "$E2E_OUT" "docker0"

e2e_wait_session_gone nets

# Clean up the leaked bridge so the rest of the suite starts clean.
e2e_root sh -c 'ip link del docker0 2>/dev/null || :'

e2e_end

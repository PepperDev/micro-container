# CLI Arguments

```
cage [options...] [--] [command...]

Options:
  -a <appdir>    Application base directory
                 Default: /var/lib/cage/default (no app name)
                 Or: /var/lib/cage/app-${name} (with -n name)
                 Used only to compute upperdir and workdir.
                  If no custom -U is given and lowerdir is a parent directory
                  on the same filesystem, a 10G ext4 image is created at
                  parent(appdir)-loop.img and mounted at
                  parent(appdir)-loop (no suffix appended when the parent
                  already ends in -loop), and used as the
                  application base instead. The appdir's last element is kept
                  as the leaf: -a /other/dir/xpto → /other/dir-loop/xpto, and
                  -a /other/dir-loop/xpto keeps /other/dir-loop.img.
  -C             Unshare cgroup namespace.
                  Before unshare, creates /sys/fs/cgroup/cage-{pid},
                  moves self into it, and enables subtree_control so
                  each instance gets an isolated cgroup tree.
  -c <dir>       Working directory inside the container (default: /)
  -e <key=value> Add environment variable
  -G             Setup environment and volumes to support GUI applications.
                 On re-entry, inherit the running instance's GUI env (the
                 caller's DISPLAY/XDG_RUNTIME_DIR are ignored); -e overrides.
  -g <group>     Group name or GID (default: 0)
                 Colon-separated for multiple groups.
                 If -u is given, try to compute supplementary groups.
  -h             Print help and exit
  -i <file>      Init script to run as root inside the container before the command
  -k             Stop/kill the running instance identified by -p or -n
  -l <dir>       OverlayFS lower directory (default: /)
  -N             Unshare network namespace
  -n <name>      Application name
                 Used only to compute appdir and pidfile default paths.
  -p <file>      PID file path
                  Default: /run/cage/pid (no app name)
                  Or: /run/cage/${name}.pid (with -n name)
                  Re-entry is implicit: if a pidfile exists and its process
                  is running, cage joins its namespaces, forks, and runs
                  the command. When re-entering, only -e, -G, -u, -g, -c,
                  and command are applied; initscript, overlay paths, and
                  custom volumes are ignored.
  -U <dir>       OverlayFS upper directory (default: ${appdir}/upper)
                 Must not have lowerdir as a parent directory on the same
                 filesystem.
  -u <user>      User name or UID (default: 0)
                 Colon-separated groups follow the user.
  -V             Print version and exit
  -v <host:container>
                 Map a volume from the host into the container. If a colon
                 is present, the left side is the host path and the right
                 side is the container mount point.
  -w <dir>       OverlayFS work directory (default: ${appdir}/work)
                 Must be on the same filesystem as upperdir.
                 On the samefs loopback flow (see -a) a custom -w must be a
                 path under the derived loopdir, otherwise cage fails: the
                 workdir must accompany the relocated upper on the loop.
  --fix          Internal (not listed by -h). Invoked as `cage --fix` with no
                 other arguments; makes the binary setuid-root: chown it to
                 root and set the setuid bit (currently mode 04755).
                 Unprivileged callers reach it through the automatic sudo path.
                 A run with effective uid 0 against a binary that is not
                 root-owned or lacks the setuid bit performs the same repair
                 automatically before continuing (no sudo needed).
                 Escalation contract: the only requirements are owner==root
                 and the setuid bit set — exactly what validation checks.
                 Group/other mode bits are implementation-defined and
                 irrelevant to escalation.

  The command argument accepts two special forms:
    `-`    Run the user's shell instead of a named command.
           Shell resolution: `-e SHELL=...` → guest passwd shell → `/bin/sh`.
           Equivalent to running `$SHELL` as a login shell inside the container.
    `--`   When `--` is given with no following command, same as `-`.
           When `--` is given with a following command, the command is run
           as-is (standard `--` behavior).
           Without `--`, bare `cage` with no command still produces
           `cage: no command specified`.
 ```

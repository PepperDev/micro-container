# Domain Model

## Business Rules

1. **No daemon** — Cage operates as a single-shot binary. No long-running daemon process.
2. **OverlayFS isolation** — Cage uses OverlayFS to compose a writable root filesystem from lower, upper, and work directories.
3. **Zero external dependencies** — Static binary; no shared libraries or external binaries required at runtime.
4. **Standalone** — Single binary, fully self-contained. Creates ext4 images and sets up mounts internally.
5. **Application-based instances** — Named applications manage their own overlay layers and PID files.
6. **Volume mapping** — Host paths are bind-mounted into the container at user-specified locations.
7. **Init script support** — A script can run as root inside the container before the target command runs.

## Entities

| Entity          | Description                                                  |
|-----------------|--------------------------------------------------------------|
| `application`   | Named container instance identified by `-n name`. Drives appdir and pidfile paths |
| `appdir`        | Application base directory holding the upper and work overlay layers |
| `lowerdir`      | OverlayFS read-only base layer where the root filesystem is sourced |
| `upperdir`      | OverlayFS writable layer where runtime changes accumulate    |
| `workdir`       | OverlayFS internal work directory (required by kernel, must share filesystem with upperdir) |
| `pidfile`       | File tracking the PID of the running instance                |
| `volume`        | Host-to-container bind-mount mapping specified by `-v`       |
| `initscript`    | Script executed as root inside the container before the target command |

## Glossary

Domain entities (`application`, `appdir`, `lowerdir`, `upperdir`, `workdir`,
`pidfile`, `volume`, `initscript`) are defined in [Entities](#entities). This
table covers the remaining terminology.

| Term            | Definition |
|-----------------|------------|
| **cage**        | The project itself — an isolated environment for a process |
| **OverlayFS**   | Linux kernel filesystem that layers one directory over another |
| **host**        | The physical or virtual machine running cage |
| **target**      | The program executed inside the container |

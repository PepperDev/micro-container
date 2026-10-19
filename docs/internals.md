# Internals

## Concurrency Model

Cage uses a **single-threaded, event-driven** design. No thread pool is needed because:

- The core workflow is sequential: parse, mount, exec.
- Mount operations and init script execution happen on the control thread.
- `waitpid()` is synchronous.

Any I/O multiplexing uses a single-threaded `epoll` loop (edge-triggered,
non-blocking); cage never spawns threads.

## Dynamic Arrays (Grow / Shrink)

Used for: volume list, environment variable list, group list, PID list.

```
Internal: cap, len, elem_size

Grow:
  if len == cap:
    new_cap = cap + max(cap, 64)
    if ceiling is set and new_cap > ceiling:
      new_cap = ceiling
      if new_cap <= cap: fail
    ptr = realloc(ptr, new_cap * elem_size)
    if !ptr: fail (existing ptr still valid)
    cap = new_cap

Shrink:
  if len > 0 and len <= cap / 4:
    new_cap = max(cap / 2, floor)
    ptr = realloc(ptr, new_cap * elem_size)
    if ptr:
      cap = new_cap
    // shrink failure is non-fatal; keep old cap
```

- **Floor** = initial capacity (usually 8 or 16).
- **Ceiling** = optional (e.g. max mount count).

## Hash Maps (Bucket Grow / Shrink)

Used for: environment variable deduplication.

```
Initial capacity: 16
Load factor grow threshold: 0.70
Load factor shrink threshold: 0.25
Floor (minimum capacity): 16

Grow:
  if len > cap * 0.70:
    new_cap = cap + max(cap, 64)
    rehash all entries into new table
    free old table

Shrink:
  if len > 0 and len < cap * 0.25 and cap > floor:
    new_cap = max(cap / 2, floor)
    rehash into smaller table
    if success: free old
    // shrink failure non-fatal
```

- **Hash function**: `FNV-1a` 32-bit (`2166136261` base, `16777619` prime).
- **Collision resolution**: Open addressing with linear probing.
- **Empty slot marker**: separate `int used` array (FNV-1a does not reserve 0).
- **Build for execve**: iterate all slots linearly; key and value are joined with `=`.

## Modprobe Loading Strategy

Cage loads kernel modules lazily, at the latest possible point, via the
`modprobe_load()` wrapper which uses `init_module(2)` directly (no
`modprobe` binary — zero external dependencies).

**Modules and their load points:**

| Module    | Load point                          | Trigger                                   |
|-----------|-------------------------------------|-------------------------------------------|
| overlay   | `prepare_overlay()` in mount.c      | `sys_mount("overlay", ...)` returns ENODEV |
| loop      | `loop_open_control()` in loop.c     | `access("/dev/loop-control")` fails(ENOENT)|
| veth      | `setup_net_or_fail()` in cage.c     | Before any netlink operations              |
| fuse      | (not loaded by cage)                | Filesystem driver, not cage's concern      |

**Design:**

1. **Try first, load on failure** — For `overlay`: call `sys_mount` first.
   If `ENODEV`, the module is not loaded. Load it and retry once. This
   avoids unnecessary `init_module` calls when the module is already loaded.
2. **Check device node first** — For `loop`: try `access("/dev/loop-control")`.
   If the node exists, the module is likely loaded (or the kernel built it
   in). Skip `modprobe_load`. Only load on `ENOENT`.
3. **Modules.dep lazy cache** — `modprobe_load` resolves module dependencies
   via `modules.dep` and `modules.builtin`, cached lazily in internal hash
   maps. The files are read once, on first resolution, and cached for the
   process lifetime. See `src/pkg/modprobe.c`.
4. **No forced loading** — Cage never calls `modprobe_load` unconditionally.
   Every call is guarded by a check (failed syscall or missing device node).

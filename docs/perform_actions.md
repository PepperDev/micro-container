# Perform Actions

This document defines the "perform" trigger protocol and the recurring actions that can be executed multiple times when a request starts with `perform`.

When a request to the agent starts with `perform`, read this file first and follow the instructions in any section whose name matches the request. These sections describe actions that may be performed multiple times or repeated in the future.

## perform spec mode

When the prompt is `perform spec mode`, optionally followed by a new spec description
or instructions (for example `perform spec mode add a --json flag`), enter planning
mode and follow this strict workflow:

1. **Update specification/planning docs as needed** — docs/ (domain documentation), AGENTS.md,
   and other spec/planning files are the expected outputs of planning mode. Do NOT
   write docs/ or other spec updates to TODO.md — updating them during
   planning is the norm, not a separate task. Only add TODO.md entries when
   the user asks for code changes (bugfixes, features, refactors).
   Per CONTRIBUTING.md, doc-only changes skip the quality gate — no
   `make report` needed during planning. docs/ must reflect the
   **intended/designed behavior** (the spec), not what the current code
   happens to do — the code will be changed later to match.
   **Note:** System Plan mode is read-only — the user must switch to Build
   mode for the agent to write these files. Build mode during planning does
   NOT constitute approval to start coding or committing.
2. **Present the plan** — show it and wait for the user to speak. While waiting, you are in *planning mode* — only planning docs may be touched. Do NOT commit TODO.md, start coding, or modify any source/header (.c/.h) files until the user explicitly approves. End your presentation with **"Ready for review?"** — do not commit, do not proceed.
   - After editing planning docs, show `git diff --stat` so the user sees what changed.
   - Do not `git add` or `git commit` until the user says "commit", "approve", or equivalent.
3. **Wait for approval, then commit** — after the user approves, commit TODO.md (the plan) first.
4. **Proceed to code** — the user either names a single task to tackle (e.g. "tackle task 1"), or sends `perform loop` to tackle multiple tasks sequentially (see §perform loop).

Violations: committing code without approval, coding before the user says to, or skipping the TODO-first step are prohibited.

> **⚠️ Agent must not act on its own analysis**
> During planning mode the agent may be asked to investigate or audit. The
> agent must **never** edit TODO.md or any other file based on findings
> from that investigation unless the user explicitly instructs it to do so.
> Present the findings, wait for instructions — do not pre-emptively add
> tasks, update plans, or fix things the user hasn't asked for.
> "Only planning docs may be touched" means ONLY docs the user explicitly
> names or agrees to in that conversation turn.

> **⚠️ System Plan/Build mode vs AGENTS.md Planning Mode**
> The system's plan/build mode switching is independent of this workflow.
> In system Plan mode the agent's tools are read-only — no file of any kind
> can be written. The user must switch to system Build mode to enable the
> agent to edit spec/planning files (docs/, TODO.md, etc.) on their behalf.
>
> System build mode does **NOT** constitute user approval for coding or
> committing — the agent must remain in AGENTS.md planning mode (no code,
> only planning docs) until the user explicitly says "tackle", "proceed",
> sends `perform loop`, or otherwise signals approval. Only then may code be
> modified.
>
> Even doc-only commits (docs/, TODO.md, AGENTS.md) require the
> user's go-ahead — "commit the plan" or "proceed" at step 3 of the workflow
> below. Do not commit any file during planning mode without explicit
> permission.
>
> **⚠️ Incremental edits**
> When editing a document, check if there are already uncommitted changes before adding more. When the user asks for additional changes and the agent makes a mistake and wants to revert, NEVER run `git checkout` — it will lose the previous uncommitted state. Instead, compare the current state with what was there before and try to solve the problem. As a last resource, restore only the affected lines, not the entire document.

## perform loop

To enter loop mode the prompt is exactly `perform loop`, nothing more.

During context compaction, make sure to keep in the context if agent is currently on loop mode or not.

When entering loop mode follow the steps:
- Check for work-in-progress changes in the worktree and try to match the task they belong to; resume it.
- If none, tackle the next task that makes sense.
- File and function names need not strictly match those in TODO.md — agents are free to reorganize them.
- Add multi level source directories (only main at src/, remaining grouped in meaningful subdirectories, e.g. src/module/code.c), moving files to group related responsibilities (SOLID).
- Each iteration, try to reduce or avoid lizard exceptions by reorganising or refactoring the code.
- Apply the Definition of Done. Delete the finished task's entry from TODO.md in the same commit — completed tasks are removed, never ticked and left behind, so no separate TODO housekeeping pass is needed.
- Commit once per task (one task, one commit). Squash related changes into a single logical commit.
- Loop back until the user interrupts. For relevant missing critical technical details or blockers (e.g. a required system tool or permission), query the user. Do not query the user to decide the next task — pick the most relevant one and proceed.

## perform code audit

**Deliverable:** one self-contained `- [ ]` item per actionable finding in
TODO.md, plus a report at `.tmp/code-audit-$(date +%F).md`, and no other change.
This is audit-only: it edits no source, build, or spec file — not even to remove
dead code. A finding that needs a design decision the human must make is written
only in the report; wait for that decision rather than writing it as a task.

Review the whole repository against the tracks below. Each actionable finding
becomes one TODO.md task under a fitting section, carrying file and line
references, the exact finding, and the intended fix. Do not invent work: an audit
that finds nothing reports so.

- **Dead code** — unreachable branches, unused functions and statics, parameters
  kept only to be ignored, constants no longer read, flags for behaviour that no
  longer exists, and scaffolding from an earlier design. A test that cannot fail is
  a finding.
- **Duplication** — the same logic, constant, or parsing step written more than once
  across translation units; candidates for one shared home. A constant defined twice
  is a defect even when both copies agree today.
- **Generalization** — near-identical code that differs in a few places and should be
  one parameterized function (the complement of duplication, which is the same code
  repeated word-for-word). In C this is usually a function taking a small struct or a
  mode argument rather than a class hierarchy.
- **Performance** — allocation churn, avoidable recomputation, a linear scan inside
  a loop that could be a table lookup, repeated `strlen` in a condition, `snprintf`
  on a hot path, one syscall per item where one per batch would do, quadratic buffer
  growth, and a blocking call on a path that should be non-blocking. For a project
  whose goal is small and auditable, a premature cache or speculative optimization
  is also a finding (unnecessary complexity), not a win.
- **Complexity** — deep nesting, long functions doing several things, tangled
  control flow, high branching, and logic harder to read than the problem warrants.
  Candidates for splitting, flattening, or replacing a dispatch chain with a table.
  A lizard exception that refactoring could remove is a finding.
- **Single responsibility** — a translation unit or header with more than one job; a
  unit that both parses and performs I/O; a header that declares policy; a module
  that reaches into a neighbour's internals.
- **Open for extension** — a dispatcher or switch that grows a new branch every time
  a tool, argument, or event is added, where a table row would absorb the new variant.
- **Substitutability** — an ops struct or function-pointer table whose contract an
  implementation weakens: a shorter bound than advertised, a different ownership
  rule, a null return meaning something else, or a function that partially succeeds.
- **Interface width** — a struct of function pointers where callers use only a few
  entries, forcing stubs. Split it so each consumer depends only on what it calls.
- **Dependency direction** — a module reaching for a global instead of a parameter,
  a hidden runtime dependency, or an inner layer that includes an outer one. Keep the
  declared layer direction (e.g. `core` depending on `sandbox`, `ext4`, `user`, and
  `util`, never the reverse).
- **Coupling and leaky abstractions** — domain code reaching into process, mount, or
  socket machinery; a module calling a neighbour's internals; coupling through shared
  mutable globals. Where a less-coupled result is reachable, name the target shape
  (new interface, adapter at the boundary, redrawn layer), not just the local cleanup.
- **Testability** — code with no seam for a deterministic test, a test that needs
  root, a network, or real kernel state (see CONTRIBUTING.md §Testing), an untested
  boundary, and a test that asserts implementation detail rather than observable
  behaviour.
- **Memory ownership** — every allocation has one owner and a documented release
  path: no leak on an error branch, no double free, no use-after-free, no pointer
  used after the buffer it points into was reallocated, and no pointer outliving what
  it borrows.
- **Buffer bounds** — no read or write past a declared limit. Off-by-one at every
  index expression, every length-versus-capacity comparison, and every terminator.
  Check every `memcpy`, `memmove`, `strncpy`, and `snprintf` result, including
  truncation, which is silent data loss rather than cosmetic. No unbounded `strcat`
  or `sprintf`.
- **Integer conversions** — every narrowing conversion, every `size_t` to signed or
  narrower type, every multiplication that could wrap before a bounds check uses the
  wrapped value, and every subtraction that could go negative. Parse functions check
  `ERANGE` and reject trailing characters, so a malformed number is an error rather
  than a truncated one.
- **Uninitialized and unowned memory** — no read of an uninitialized struct field or
  padding, no partially written output parameter on any path including error paths,
  and no uninitialized struct handed to a caller.
- **Bounds and limits** — every operation that can grow without a known bound has a
  documented limit, and the limit is enforced before the allocation rather than
  after. A new unbounded path is a release blocker.
- **Filesystem and path handling** — `..` cannot escape the sandbox root, symlink
  traversal is detected rather than inferred from spelling, a check-then-open gap is
  closed or documented as a known race, and a failed or uncertain check fails closed
  rather than falling back permissively. Temporary files live beside their target and
  a failed write leaves the previous file intact.
- **Privilege and namespace safety** — privileges are dropped at the right point (no
  step after dropping needs root, and none before it runs with more privilege than
  needed); `chroot`/pivot, mount flags, and the setuid-root/`--fix` path are reviewed
  for an escalation window (symlink and TOCTOU attacks included); user and cgroup
  namespaces do not leak host access.
- **Signals and concurrency** — no non-async-signal-safe call, allocation, or locking
  from a signal handler; every child process is reaped, and terminated then reaped
  where needed. Cage is single-process and single-threaded by design: a finding that a
  thread or an extra process was introduced is a scope change, not a bug.
- **Untrusted input** — every CLI argument, config value, environment variable, file
  read from a volume or rootfs, and kernel or filesystem response is validated,
  bounded, and treated as data. No input reaches a format string, a path, a command
  argument, or a privilege decision without validation.
- **Secrets** — no credential in a source file, URL, log line, diagnostic, or
  generated asset.

## perform spec audit

**Deliverable:** one report at `.tmp/spec-audit-$(date +%F).md` (git-ignored,
never committed), plus — for likely code mistakes only — one `- [ ]` item per
finding in TODO.md. This is audit-only: it changes no source, build, or spec file — a
finding becomes a TODO.md item or a report entry, never a fix. Repairing a one-line
thing while reading is exactly how an audit turns into an unreviewable mixed change.
An audit that finds nothing still produces its deliverable; "nothing found" is a
result and is reported as one, never as silence.

Read every document in `docs/` and check each claim against the code it describes.
Record every disagreement in the report, in these sections:

- **Likely code mistakes** — the code does something the specification does not
  describe, or describes differently. The specification is the authority, so the code
  is the bug. Each of these also becomes one self-contained TODO.md task (file/line
  references, exact finding, intended fix) so any agent can pick it up without this
  pass's context.
- **Likely spec mistakes** — the specification requires something the code does not
  implement, and the code looks right. Wait for a human decision; do not edit the spec.
- **Unsure** — neither side is clearly authoritative. Wait for a human decision.
- **Missing in both** — a real behaviour neither the code nor the specification
  mentions, invisible to a two-way comparison. Wait for a human decision.
- **Overclaims** — any sentence implying a built target, listener, store, or test
  suite that does not exist. Wait for a human decision.

A finding where the spec is the bug, where neither side is authoritative, or that
needs a design decision is written in the report only; the agent edits neither the
spec nor the code.

Every finding carries file and line references for both sides, plus the smallest
reproduction where one exists. For a likely code mistake, the TODO.md task carries
the same references and the intended fix.

The report opens with the state of each document: one line per `docs/` file saying
what is implemented, what is design-only, and what has no code, so an unimplemented
specification is distinguishable from a wrong one without reading the findings.

Also check:

- **Single-sourcing** — every setting, limit, default, route, name, and grammar rule
  is defined in exactly one place and referenced elsewhere. Report a value stated
  twice, and a rule restated in a second document instead of linked.
- **Referenced targets exist** — resolve every relative link, every `file.md`
  section reference, and every named file, CLI flag, environment variable, and
  symbol. Report dangling names and renames that left stale references.

## perform spec consistency

**Deliverable:** the corrected specification itself. The deliverable of this pass is
the change to the spec, made in the same change that found the defect. This pass is
not audit-only — it edits what it reads, because a documentation defect and its
correction are the same edit. A defect that needs a code change to resolve becomes a
TODO.md task instead, and the specification is edited only as far as the agreed
behaviour goes.

Passes:

- **Dangling references** — every mention of a file, module, flag, symbol, or
  section that no longer exists, and every old name of anything renamed or
  redesigned. Each reference must point at what exists under its current name.
- **Define-by-negation clauses** — delete every qualifier that defines a component by
  what it is not or where it no longer lives ("no longer", "now", "previously",
  "unlike", "not under", "rather than X"). Mechanical test: delete the clause and
  reread the sentence; if it still states the current fact in full, keep the
  shortened positive version.
- **Transitory text** — anything describing a step the reader is not at, scaffolding,
  and notes that explain a change already made. Specs state behaviour as though the
  code already behaves that way; migration phases, staged steps, and "will be" / "not
  yet" wording belong in TODO.md, not in a specification.
- **Repeated claims** — text that repeats a claim for one item when a broader set
  already carries it. Consolidate into one home and reference it. Merging or
  splitting a `docs/` file is allowed when it removes repetition, but the file set
  must stay coherent and linked from [README.md](README.md).
- **Plain English** — short sentences, one idea each, active voice; drop weasel words
  (hedges, vague qualifiers, and intensifiers).
- **Honesty** — no document claims a build target, listener, store, or test suite
  that does not exist, and no document presents a proposed specification as
  implemented behaviour.

## perform manual test

End-to-end verification of the `cage` binary against a real Docker daemon. Use
this when asked to *perform manual test* (or "test the binary on Docker"). It
complements `make test` / `make report`: unit tests mock the kernel, this action
does not — it exercises real mounts, namespaces, cgroups and overlayfs.

The cases are executable scripts under `e2e/`, one file per case. The shared
harness `e2e/_common.sh` builds the static `bin/cage` with `make`, provisions a
privileged `cage-t` container, and installs the binary with `docker cp`; the
runner `e2e/_all.sh` runs every case, prints a summary and tears the container
down.

```sh
sh e2e/_all.sh          # run every case
sh e2e/gui_x11.sh       # run one case
```

Selection rules:

- the request names no case → run the whole suite (`sh e2e/_all.sh`);
- the request matches an existing case (`e2e/*.sh`) → run just that case;
- the request describes a case that does not exist yet → add a new `e2e/*.sh`
  that includes `e2e/_common.sh`, or extend the closest existing case.

A case includes the harness, then records checks; it creates the container if
needed but never removes it, so it can be run repeatedly:

```sh
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_run docker exec "$CAGE_CTR" cage -l /altroot -n demo -- id
e2e_check_contains "runs as root" "$E2E_OUT" "uid=0"
e2e_end
```

Assertions and helpers: `e2e_check`, `e2e_check_eq/ne/contains/not_contains/rc`,
`e2e_run` (captures `$E2E_OUT`/`$E2E_RC`), `e2e_socket_up/down` (reusable socat
unix-socket server), and `e2e_guest_install` (install a tool inside an instance's
overlay through cage, never into `/altroot`). Each check appends
`STATUS<TAB>description` to `$E2E_RESULTS/<case>.tsv`; `e2e/_all.sh` turns those
into a per-case pass/fail summary and exits non-zero on any failure.

Keep all scratch files **outside the repo** (e.g. `/tmp/opencode`). Manual testing
does not modify source; if it uncovers bugs, record them in TODO.md (the user must
ask before any code fix is started).

The Docker CLI and a reachable daemon are assumed (remote daemon: `export
DOCKER_HOST=unix://<path-to>/docker.sock`). The harness ships files with
`docker cp`, never by bind-mounting the workspace. If the Docker CLI is not
present, stop and report the blocker back to the user.

Host requirements: `make`, `gcc`, `wget`, `tar` and `docker`; the harness installs
`socat`/`acl` inside the container.

### Kernel module probes

The e2e container shares the daemon's kernel, where `veth` is already loaded and
`/lib/modules` does not exist, so the suite cannot verify cage's own module
loading. To probe module loading with the modules **unloaded**, boot an Alpine
netboot VM under QEMU/KVM inside a container — see
[manual-test-modules.md](manual-test-modules.md). Use it only for that purpose;
everything else belongs in `e2e/`.

### Host-side cgroup cleanup

Cage creates `/sys/fs/cgroup/cage-<pid>`. The container runs with
`--cgroupns=host`, so those directories are the host's and outlive
`docker rm`. `e2e/_all.sh` removes them inside the container before teardown;
remove any host-side orphans leaf-first (`rmdir`, never `rm -rf`):

```sh
for d in /sys/fs/cgroup/cage-*; do [ -d "$d" ] || continue; \
  for c in "$d"/*; do [ -d "$c" ] && rmdir "$c" 2>/dev/null; done; rmdir "$d" 2>/dev/null; done
```

### Gotchas

- **Remote daemon + bind mounts**: never mount a local workspace path into the
  daemon; ship files with `docker cp` (the harness does this).
- **Appdir must be tmpfs**: Docker's root fs is overlayfs and rejects overlay
  upperdirs (`... not supported as upperdir`); `e2e/same_fs.sh` exercises the
  ext4-loopback branch instead.
- **Detach background work**: a background process inheriting the `docker exec`
  stdout keeps the exec open. Use `setsid ... </dev/null >/dev/null 2>&1 &` or
  `docker exec -d`.
- **`pkill -f` self-match**: a `pkill -f PATTERN` whose own command line contains
  `PATTERN` kills its parent shell. Write the pattern as `[U]NIX-LISTEN:...` so it
  cannot match itself.
- **Async teardown vs re-entry**: `cage` tears a session down asynchronously, and a
  run with the same `-n` started before teardown finishes is treated as a *re-entry*,
  which by design skips GUI mounts and socket ACLs. Callers that expect a fresh first
  entry (e.g. after `e2e_guest_install`) must first wait for the session's
  `/run/cage/<name>.pid` to disappear (`e2e_wait_session_gone`).
- **Leaked cgroup dirs** cannot be removed with `rm -rf` (the kernel returns EPERM
  when unlinking cgroup files); remove leaf-first with `rmdir`. Because the
  container runs with `--cgroupns=host`, the `cage-*` cgroups are the host's and
  must also be removed on the host after `docker rm` (see §Host-side cgroup
  cleanup).
- **Non-root escalation** depends on the binary being setuid-root (`cage --fix`) or
  `sudo` being present. A run with effective uid 0 self-heals a binary that is
  not root-owned or not setuid (chown root + setuid), without sudo.
- **Expected spec behaviours**: env precedence/overrides, colon group parsing,
  implicit re-entry, `-k` teardown, GUI socket bind-mounts, volume mapping.

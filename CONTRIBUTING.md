# Contributing

## Getting Started

1. Fork the repository.
2. Create a feature branch from `main`.
3. Make changes following the project conventions.
4. Run the full quality gate locally.
5. Submit a pull request.

## Code Style

Use `make format` to apply code style.

Always wrap blocks with braces even for single line bodies (if/for/while/do).

## Coding Conventions

Avoid code duplication.
Apply SOLID principles — especially Single Responsibility and Dependency Inversion.
Follow the Transformation Priority Premise and Object Calisthenics.
To make unit tests easy, keep code decoupled and context-agnostic. Write independent functions that receive arguments, process, and return, without relying on global, static, or shared variables. Apply interface segregation and dependency inversion.

## Testing

Create unit tests without relying on third-party testing library/framework, build our own.

Never rely on operations that require root access or are machine-state dependent:
- For filesystem operations, use `memfd_create` (from `<sys/mman.h>`) when an open fd suffices; otherwise create a random temporary directory before the test and clean it up on exit regardless of success or failure.
- For socket operations, always bind to port 0 (random port).
- For operations that require root or are not suitable for temporary files or random socket binds, use stubs/mocks/spies to mock the stdlib or syscall layer — do not rely on real kernel resources.
- Reading from `/proc`, `/dev`, or `/sys` is considered machine-state dependent and must be avoided or mocked.

Tests that depend on the execution of external system tools (database, proxy, external service, external binary) are considered integration tests. They must not block unit tests — they are optional and serve only to help test functionality.

Use dummies, stubs, fakes, mocks, and spies where appropriate.
For documentation of the weak symbols mock infrastructure and mock control variable discipline, see [docs/qa.md §Domain-Specific Pitfalls — Test mock infrastructure](docs/qa.md#test-mock-infrastructure-weak-symbols).

### Mocking via weak symbols

All syscall and stdlib wrappers are defined as weak symbols in `src/util/syscall.c`.
Tests provide strong overrides in `tests/mock_syscall.c` — at link time, the
linker picks the strong definition, replacing the real implementation without
any `#ifdef` guards or separate compilation.

This means every production `.c` file is compiled *identically* for the test
binary and the production binary — only the linker differs. No `CAGE_TEST`
define, no `TEST_STATIC`, no test hook functions exposed with `cppcheck-suppress
unusedFunction`. Coverage data reflects the exact same code paths as production.

Since the wrappers are plain functions (not macros), stdlib calls like `open`,
`read`, `fopen`, `fclose`, `stat`, and `access` can also be mocked, enabling
full test isolation from the real filesystem and kernel state.

Add stdlib wrappers **only when a test concretely needs them** — no speculative
wrapping. The cost of adding is low (declare + weak impl + mock override), so
there is no reason to front-load. Extend on demand.

## Quality Gate

No commit may bypass any quality gate item. All thresholds are mandatory.

### Running checks

Always run `make format` before `make lint` or `make report` — formatting
changes line counts and affects lizard LOC calculations.

`make report` is the preferred single command: it runs build, test, lint,
and coverage in one step. If it fails, use `make lint` (lizard/cppcheck
details) or `make coverage` (gcov details) to inspect individual failures.

### Fixing violations

Do not circumvent build warnings, lint violations, or quality gate failures
with workarounds, suppressions, or half-baked fixes. Every violation must be
resolved at the root cause by refactoring the code.

Follow the action table below strictly. Do not circumvent metrics by using
ternary operators (`? :`) instead of `if`/`else`, merging unrelated statements
onto one line, or similar cosmetic workarounds — address the structural issue
directly.

| Issue | Required action |
|---|---|
| **File > 300 LOC** | Split into multiple files based on responsibility |
| **Function > 40 LOC** | Split into multiple smaller functions |
| **Cyclomatic complexity > 6** | Split into multiple functions (extract conditions, reduce nesting) |
| **Tokens per function > 150** | Split into multiple functions |
| **Nested control depth > 3** | Extract inner blocks into helper functions |
| **Parameters > 5** | Split into multiple functions; when splitting is not feasible (e.g. callback conforming to a fixed signature), pack parameters into a struct and pass the struct |
| **Unused parameter** | Remove it from the function signature and update all callers. Never suppress with `(void)param;`. The only exception is mock functions that must conform to a fixed weak-symbol interface; these must be documented as interface conformance, not hidden suppression |
| **Unused function** | Remove it entirely |
| **Unused local variable** | Remove the variable or its dead assignment. If the value is genuinely needed (e.g. reading from a device register or bit stream), use the value in a validation check instead of `(void)` suppression |
| **Coverage < 80%** | Add missing test cases to reach 80% line coverage; when multiple files are below threshold, prioritize files with the lowest coverage first |

Additional checks enforced by the quality gate:
- cppcheck `--enable=all --check-level=exhaustive` must succeed
- gcov — at least 80% line coverage
- cohesion & coupling — max 1 public abstraction per file
- lizard exceptions: avoid at all costs by refactoring. Only when impossible to meet a gate, add a justification to docs/qa.md §Code Quality Exceptions, before Past Incidents.

### Cppcheck suppression discipline

Global cppcheck suppression by adding `--suppress=*` to the Makefile is not
allowed. Must try to fix the root cause by refactoring first.

In-source suppression comments (e.g. `// cppcheck-suppress <rule-id>`) may be
used only as a last resort when the check is genuinely unfixable (e.g. a false
positive due to cross-file usage that cppcheck cannot see). Include a comment
explaining why the suppression is necessary.

### Dead code detection

Any snippet of production code (function, variable, branch) that is only
executed by tests and never in production — except for weak-symbol definitions
in `syscall.c` which are always overridden by strong test definitions — must
be removed from production sources.

If the code is still required as a test auxiliary helper (e.g. cleaning up
temp directories, constructing test fixtures), move it to `tests/` and make
it a shared test utility function. Production sources must not ship code
that is unreachable outside the test binary.

## Definition of Done

All items below are mandatory. A change is not complete until every item passes.

1. **Code builds with no warnings** — `make` must succeed
2. **All tests succeed** — `make test` must pass (see §Testing)
3. **Code style applied and reviewed** — `make format`
4. **All quality gate thresholds met** — `make report` must succeed (see §Quality Gate)
5. **Bug prevention reviewed** — see docs/qa.md §Bug Prevention; evaluate recurrence likelihood of every fixed bug
6. **Completed task removed from TODO.md** — the finished item is deleted in the same commit that completes it (see §TODO Housekeeping)
7. **Documents consistent with code** — docs/ is source of truth; code must match spec

## TODO Housekeeping

TODO.md holds outstanding work only. Organise it into meaningful sections with checkable bullet items. Completed tasks are removed — deleted in the same commit that completes them — never ticked and left behind, so no separate housekeeping pass is needed.

## Domain Knowledge

docs/ is the **source of truth** — it defines the intended/designed behavior (the spec). Naming in code may differ from naming in the spec, but behavior must match. If code disagrees with docs/, the code is wrong and must be changed to match, not the other way around.

Refer to [docs/README.md](docs/README.md) for business rules, CLI arguments, entities, flow/lifecycle, retry landscape, memory management strategies, threading architecture, and glossary.

## Task Lifecycle

A task is considered completed only when it fulfils all conditions in the Definition of Done.

## Pull Request Process

1. Ensure the complete Definition of Done is met (see §Definition of Done above).
2. Ensure all quality gate checks pass.
3. Update docs/ if architecture, CLI, or business rules change.
4. Remove completed items from TODO.md following the TODO housekeeping rules.
5. Follow the bug prevention process — see docs/qa.md §Bug Prevention.

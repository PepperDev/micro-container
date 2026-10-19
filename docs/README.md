# Domain

Domain documentation for cage. This directory is the **source of truth** — it
defines the intended/designed behaviour (the spec). Naming in code may differ
from naming in the spec, but behaviour must match. If code disagrees with these
documents, the code is wrong and must be changed to match.

| Document | Contents |
|----------|----------|
| [domain.md](domain.md) | Business rules, entities, terminology |
| [cli-arguments.md](cli-arguments.md) | CLI reference |
| [flow-conventions.md](flow-conventions.md) | Conventions used by the flow pseudo-code |
| [lifecycle.md](lifecycle.md) | Invocation, container, cgroup, network, GUI, samefs, re-entry flows |
| [ext4-image.md](ext4-image.md) | Self-contained ext4 image generation |
| [internals.md](internals.md) | Threading, data structures, kernel module loading |
| [syscall-discipline.md](syscall-discipline.md) | `access()` / `stat()` / `mkdir()` discipline |
| [qa.md](qa.md) | Quality assurance: pitfalls, exceptions, incident index |
| [past-incidents.md](past-incidents.md) | Recorded incidents (part 1) |
| [past-incidents-2.md](past-incidents-2.md) | Recorded incidents (part 2) |
| [manual-test-modules.md](manual-test-modules.md) | Manual test: probing kernel module loading in a netboot VM |

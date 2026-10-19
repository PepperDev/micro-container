# AGENTS.md

AI agent guidance for the cage project.

## Perform Trigger

If the user's request starts with `perform`, first read `docs/perform_actions.md` and follow the instructions for any section whose heading matches the request. Those sections describe recurring actions that may be performed multiple times or repeated in the future.

## Design Ethos

Minimalistic, compact, secure. Source code should be readable and maintainable by single human.

## Performance-First Design

When multiple technical solutions are possible, always prefer the one that performs better under heavy workload, such as using epoll instead of poll, using non-blocking operations, batch processing, lock-free queues, and smart concurrency design.

Worship lightweight resource usage. Handle extreme usage without reaching limits. Prefer stack memory, but if static allocation would limit performance, latency, throughput, or fail under stress, use smart dynamic allocation with bucket grows and shrinks.
Recommended grow: add max(cap, 64) up to a ceiling (if any). Recommended shrink: halve when utilisation drops below 25% down to a floor (usually initial value).
Use smart retry mechanisms with exponential backoff.

See [docs/README.md](docs/README.md) for the full retry landscape and memory management strategies.

## Domain Reference

For business rules, entities, CLI arguments, flows, retry landscape, memory management strategies, threading architecture, glossary, and code quality exceptions, refer to [docs/README.md](docs/README.md).

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) is an extension of this document — read it together with this section whenever you read AGENTS.md. The rules in CONTRIBUTING.md apply to agents as they apply to human contributors, with the exceptions noted below.

Agents work directly on the repository and commit without pull requests, so the Getting Started (forking) and Pull Request Process sections do not apply. All other sections — Definition of Done, Code Conventions, Bug Prevention, Quality Gate, Testing — are mandatory.

All items of the Definition of Done are mandatory before every commit, with one exception: a commit that contains only documentation changes (.md files) may skip the build, test, format, lint, and coverage steps. Every other commit must pass the full quality gate (test, format, lint, coverage ≥ 80%) and all other checks listed in CONTRIBUTING.md. Do not commit partial work — a commit that fails any quality gate item is not permitted.

> **⚠️ Agents are repeatedly failing to apply the DoD literally. In particular:**
> - **TODO tracking**: Completed tasks must be **deleted** from TODO.md in the same commit that completes them — never ticked with `- [x]` and left behind, never marked with strikethrough or (FIXED) markers.
> - **Bug prevention**: Per [docs/qa.md §Bug Prevention](docs/qa.md#bug-prevention), every fixed bug must be evaluated for recurrence likelihood. Medium-likelihood bugs require adding an entry to Domain-Specific Pitfalls. High-likelihood requires that PLUS a test case.
> - **Past Incidents**: Every fixed bug must be recorded in [docs/qa.md §Past Incidents](docs/qa.md#past-incidents) with bug description, root cause, and prevention action.
> - **Quality gate**: Every commit that changes code must pass the full quality gate (test, format, lint, coverage ≥ 80%); only documentation-only (.md) commits are exempt.
>
> When in doubt, re-read CONTRIBUTING.md §Definition of Done and follow each bullet literally. Do not skip steps or substitute your own judgement for the written rules.

## Definition of Done

Implied by reading [CONTRIBUTING.md](CONTRIBUTING.md) as stated in the Contributing section above.

## Constraints

An agent must never start a task or bugfix that is not written to TODO.md (for this scope, a "task" is any change that modifies code). When the user asks to do a task not yet in TODO.md, the agent must write it down in TODO.md before starting.

Run as an unprivileged user without sudo. When a required system tool is missing, ask the user to install it — do not attempt privilege escalation or circumvent this directive.

All items of the Definition of Done are mandatory before every commit, along with all other checks listed in CONTRIBUTING.md. A commit that fails any quality gate item is not permitted.

Do not generate .gcov files in the project root directory — they must live strictly inside `obj/` folder. Since `obj/` is already gitignored, no `.gitignore` entry for `.gcov` is needed.

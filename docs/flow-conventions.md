# Flow Pseudo-code Conventions

The flow pseudo-code in this document uses the following conventions:

- **Quotes**: File paths and variable references use double quotes (e.g., `"${host_volume}"`, `"${root}/etc"`).
- **Variables**: Prefixed with `$` and wrapped in braces (e.g., `${root}`, `${guest_volume}`, `${pid}`).
- **Syscalls**: Actual syscall names are used directly (e.g., `stat`, `lstat`, `unshare`, `mkdir`, `open`).
- **Shell commands**: Represented as shell equivalents where no single syscall exists (e.g., `mount --make-private /`, `mount -t tmpfs tmpfs /dev/shm`).
- **Assignments**: Attribution uses `:=` (e.g., `guest_HOME := -e HOME`, `cgroup_fs_type := 0`).
- **Descriptions**: Short descriptions preferred over function names. Names use spaces, not underscores (e.g., `setup container filesystem`, not `do_child_setup`).
- **Shell shorthand**: Common file operations are represented by their shell equivalents: `touch` for `touch(1)`, `recursive mkdir` for `mkdir -p`, `parent of something` for `dirname`.
- **Exist shorthand**: `exist` indicates an `access(F_OK)` call (e.g., `exist "${path}"?`).
- **C function calls**: Not used. Behaviour is described via syscalls, shell commands, and `call` to another routine.
- **No parentheses**: Arguments are not wrapped in parentheses (no `func(arg)` style).
- **Comments**: Explanatory notes prefixed with `#`.
- **Routine definitions**: A `define <name>:` line introduces a routine (e.g. `define setup resolv:`, `define container process:`). Its body is the indented lines that follow.
- **Routine calls**: Invoking another routine is written `call <name>` (e.g. `call container process`, `call cleanup cgroup`). An optional `#` comment summarizes what the call does.
- **Labelled blocks**: A plain line ending in `:` (e.g. `replace:`) introduces a labelled block reached via `goto <label>`.
- **Indentation**: 2 spaces per nesting level.
- **Control flow**: Conditions are written `if <cond>:`, with optional `elif` / `else:`. Loops are written `for ...:`. `return`, `exit`, and `_exit` end a routine or branch.
- **Sequential execution**: Statements run top to bottom; an indented block runs in place before the next statement at the same indent level. There is no implicit continuation from a nested node back to a parent node — the next step is always the next line at the same or an outer indentation.
- **Labels**: `goto <label>` transfers control to the named labelled block; used where the flow shares a common tail.
- **Processes**: A bracketed tag on a routine (e.g. `setup net [main]:`, `setup veth [container child]:`) names the process the steps run in.
- **References**: `see §Section Name` links to detailed sub-sections. Cross-file references use relative links to the sibling document (e.g. `[§Cgroup Lifecycle](lifecycle.md#cgroup-lifecycle)`). `→` indicates a conditional outcome.

See [CONTRIBUTING.md §Domain Knowledge](../CONTRIBUTING.md#domain-knowledge) for the guiding principle: the `docs/` directory is the **source of truth**. Naming in code may differ from naming in the spec, but behaviour must match. If code disagrees with `docs/`, the code is wrong and must change.

# Past Incidents (continued)

### Non-idempotent indent made the format check fail in both directions

- **Bug**: the CI format step (`make format && git diff --exit-code`) reported
  `src/user/user_parse.c:67` as unformatted, yet running the same step again
  locally flipped the line back — the check could never pass for either
  committed form.
- **Root cause**: GNU indent 2.2.13 is not idempotent for a comma immediately
  followed by a cast of a parenthesized expression. Every pass toggles
  `parse_one_token(&(*t)[*n], a, (size_t)(end - a));` between `a, (size_t)` and
  `a,(size_t)`. Whichever form was committed, one `make format` pass changed it
  and the following `git diff --exit-code` failed; a second pass restored the
  committed form, which looked like an inconsistent check. `append_tokens()`
  was the only place in `src/`/`tests/` carrying the pattern (verified with a
  per-file two-pass scan; `find -exec indent {} +` batching was not a factor).
- **Prevention**: the cast is now bound to a local
  (`size_t len = (size_t)(end - a);`), removing the pattern and making the whole
  tree idempotent (per-file two-pass scan: one oscillating file before, none
  after). `.github/scripts/build` formats a copy of every file twice and fails
  with the offending file name when the passes disagree. docs/qa.md
  §Domain-Specific Pitfalls records the rule.
- **Recurrence likelihood**: Medium — the failure presents as a flaky format
  check rather than a formatting error, so the real cause is easy to miss.

### Release cross-build broke on a newer UAPI constant and format truncation

- **Bug**: the `release` toolchains (`x86_64-linux-musl-gcc` and the other
  three) failed to build `cage`, first at `src/pkg/modload.c:163`
  (`'MODULE_INIT_COMPRESSED_FILE' undeclared`), then at
  `src/sandbox/gui_x11.c:22` (`'%s' directive output may be truncated`), while
  `make CC=gcc` on the host succeeded with no warnings.
- **Root cause**: the release toolchains bundle Linux 5.15 UAPI headers, but
  `MODULE_INIT_COMPRESSED_FILE` is defined only since Linux 5.17, so
  `#include <linux/module.h>` no longer provided it. Independently,
  `set_sock_acl()` formatted `/tmp/.X11-unix/` plus a directory entry name (up
  to 255 bytes) into a 64-byte buffer; the toolchains' GCC reported the
  potential truncation as an error (`-Werror`), whereas the host GCC did not.
  The build CI compiles with the distro `gcc` only, so neither failure appeared
  until the release was cut.
- **Prevention**: `finit_module`'s compressed-file flag now has a version-gated
  fallback (`#ifndef MODULE_INIT_COMPRESSED_FILE`); `set_sock_acl()` uses
  `char path[PATH_MAX]` and rejects a truncated result. The release toolchain
  build is the verification step for every future UAPI/`snprintf` change (all
  four arches were rebuilt clean). docs/qa.md §Domain-Specific Pitfalls records
  the rule.
- **Recurrence likelihood**: Medium — the host `gcc` accepts both patterns, so
  the failure surfaces only in the cross/release build.

### Release RPM cross-build failed for every non-native architecture

- **Bug**: the `release` packaging step aborted with
  `error: No compatible architectures found for build` immediately after the
  i686 link line, so it looked like `i686-linux-musl-gcc -static -flto` had
  failed to link `bin/cage`.
- **Root cause**: the generated spec set `BuildArch: ${RPM_ARCH}` (the *target*
  arch) while `rpmbuild` ran on an x86_64 runner. `rpmbuild` refuses to build a
  package whose `BuildArch` is not compatible with the build host — true for
  i686, armv7hl and aarch64 — so the first non-native arch aborted the release.
  The follow-up `cp` also read from
  `RPMS/$HOST_ARCH/cage-${VERSION}-1.${HOST_ARCH}.rpm` (the host dir and name),
  not the target, so it would have copied the wrong file even on success.
  Reproduced locally: `rpmbuild -ba` on x86_64 with `BuildArch: i686` prints the
  exact message.
- **Prevention**: the spec no longer sets `BuildArch`; `rpmbuild` is invoked
  with `--target "$RPM_ARCH"` to cross-build, and the RPM is copied from
  `RPMS/$RPM_ARCH/cage-${VERSION}-1.${RPM_ARCH}.rpm`. All four target arches now
  build on the x86_64 host (verified). docs/qa.md §Domain-Specific Pitfalls
  records the rule.
- **Recurrence likelihood**: Medium — the spec looks correct and the failure
  masquerades as a compiler/linker error at the tail of the build log.

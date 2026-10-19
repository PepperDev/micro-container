#!/bin/sh
#
# Run every e2e test in this directory (files not starting with "_"), then
# remove the container and print a summary. Individual tests create but do not
# remove the container; only this suite tears it down.

set -eu
E2E_DIR=$(
  cd "$(dirname "$0")" &&
    pwd
)
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$E2E_DIR/_common.sh"

e2e_require_docker
rm -rf "$E2E_RESULTS"
mkdir -p "$E2E_RESULTS"
e2e_ensure_container
e2e_sanitize

suite_rc=0
suite_file="$E2E_RESULTS/.suite.tsv"
tests_file="$E2E_RESULTS/.tests"
: > "$suite_file"

find "$E2E_DIR" -maxdepth 1 -name '*.sh' ! -name '_*' | sort > "$tests_file"
while IFS= read -r f; do
  b=$(basename "$f")
  log ""
  log "================================================================"
  log ">>> $b"
  log "================================================================"
  if sh "$f"; then
    rc=0
  else
    rc=$?
  fi
  printf '%s\t%s\n' "$b" "$rc" >> "$suite_file"
  if [ "$rc" -ne 0 ]; then
    suite_rc=1
  fi
done < "$tests_file"

log ""
log "================================================================"
log "SUMMARY"
log "================================================================"
log "$(printf '%-28s %8s %8s' 'test' 'pass' 'fail')"
log "----------------------------------------------------------------"

while IFS='	' read -r name rc; do
  rf="$E2E_RESULTS/${name%.sh}.tsv"
  if [ -f "$rf" ]; then
    pass=$(
      grep -c '^PASS	' "$rf" || :
    )
    fail=$(
      grep -c '^FAIL	' "$rf" || :
    )
  else
    pass=0
    fail=0
  fi
  log "$(printf '%-28s %8s %8s' "${name%.sh}" "$pass" "$fail")"
  if [ "$fail" -gt 0 ]; then
    grep '^FAIL	' "$rf" | cut -f2 | while IFS= read -r line; do
      log "    - $line"
    done
  elif [ "$rc" -ne 0 ]; then
    log "    - test aborted (rc=$rc)"
  fi
done < "$suite_file"

log "----------------------------------------------------------------"
if [ "$suite_rc" -eq 0 ]; then
  result=PASS
else
  result=FAIL
fi
log "result: $result"

e2e_teardown
exit "$suite_rc"

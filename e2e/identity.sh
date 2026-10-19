#!/bin/sh
# Identity and environment: root default, numeric uid/gid, unknown user,
# explicit groups, and -e handling (last wins, empty values kept).
set -eu
# shellcheck disable=SC1091 # resolved at runtime from the script's directory
. "$(dirname "$0")/_common.sh"
e2e_begin "$(basename "$0" .sh)"
e2e_sanitize

e2e_run e2e_exec cage -l /altroot -n i1 -- /bin/sh -c '
  id
  env'
e2e_check_rc "i1 rc" 0 "$E2E_RC"
e2e_check_contains "i1 runs as root" "$E2E_OUT" "uid=0(root)"
e2e_check_contains "i1 has PATH" "$E2E_OUT" "PATH="

# shellcheck disable=SC2016 # $HOME etc. expand in the guest, not the host
e2e_run e2e_exec cage -l /altroot -n i2 -u 1000 -g 1000 -- \
  /bin/sh -c '
    id
    printf "HOME=%s\n" "$HOME"
    printf "USER=%s\n" "$USER"
    printf "SHELL=%s\n" "$SHELL"'
e2e_check_contains "i2 numeric uid" "$E2E_OUT" "uid=1000"
e2e_check_contains "i2 HOME from passwd" "$E2E_OUT" "HOME=/home/dev"
e2e_check_contains "i2 USER from passwd" "$E2E_OUT" "USER=dev"
e2e_check_contains "i2 SHELL from passwd" "$E2E_OUT" "SHELL=/bin/sh"

# shellcheck disable=SC2016 # $HOME etc. expand in the guest, not the host
e2e_run e2e_exec cage -l /altroot -n i3 -u nobody -- \
  /bin/sh -c '
    id
    printf "HOME=%s\n" "$HOME"
    printf "USER=%s\n" "$USER"'
e2e_check_rc "i3 unknown-to-root fallback rc" 0 "$E2E_RC"
e2e_check_contains "i3 falls back to nobody" "$E2E_OUT" "uid=65534"

e2e_run e2e_exec cage -l /altroot -n i4 -u 1000:100:200 -g 300 -- id
e2e_check_contains "i4 uid" "$E2E_OUT" "uid=1000"
e2e_check_contains "i4 primary gid" "$E2E_OUT" "gid=300"
e2e_run e2e_exec cage -l /altroot -n i4b -u 1000:100:200 -g 300 -- id -G
gids=$(printf ' %s ' "$E2E_OUT")
e2e_check_contains "i4 supplementary group 100" "$gids" " 100 "
e2e_check_contains "i4 supplementary group 200" "$gids" " 200 "

e2e_run e2e_exec cage -l /altroot -n i5 -e FOO=a -e FOO=b -- \
  /bin/sh -c 'env | grep "^FOO="'
e2e_check_eq "i5 duplicate -e last wins" "FOO=b" "$E2E_OUT"
e2e_run e2e_exec cage -l /altroot -n i5b -e EMPTY= -- \
  /bin/sh -c 'env | grep "^EMPTY="'
e2e_check_eq "i5 empty -e kept" "EMPTY=" "$E2E_OUT"

# --- named-user resolution (guest passwd/group) -----------------------

# A named user's primary gid comes from its passwd entry, and its complementary
# groups are the /etc/group entries that list the user. daemon is gid 2 and a
# member of bin(1) and adm(4).
e2e_run e2e_exec cage -l /altroot -n n1 -u daemon -- id
e2e_check_rc "n1 named user rc" 0 "$E2E_RC"
e2e_check_contains "n1 primary gid from passwd" "$E2E_OUT" "gid=2(daemon)"
e2e_check_contains "n1 complementary group bin" "$E2E_OUT" "1(bin)"
e2e_check_contains "n1 complementary group adm" "$E2E_OUT" "4(adm)"

# An unknown name falls back to the guest's nobody entry.
e2e_run e2e_exec cage -l /altroot -n n2 -u absent-user -- id
e2e_check_rc "n2 unknown name rc" 0 "$E2E_RC"
e2e_check_contains "n2 unknown name falls back to nobody" "$E2E_OUT" "uid=65534(nobody)"

# A user created in the overlay by a prior run with the same name is visible to
# a later run: the overlay is persistent for a given -n.
e2e_run e2e_exec cage -l /altroot -n ov -- \
  sh -c 'addgroup -g 4200 alice && adduser -D -u 4200 -G alice alice'
e2e_check_rc "ov user setup rc" 0 "$E2E_RC"
e2e_wait_session_gone ov
e2e_run e2e_exec cage -l /altroot -n ov -u alice -- id
e2e_check_rc "ov overlay user rc" 0 "$E2E_RC"
e2e_check_contains "ov overlay user resolved" "$E2E_OUT" "uid=4200(alice)"

# A user and its complementary groups created by the init script are resolved
# for the command that follows it: passwd/group are re-read after --initscript.
docker exec -i "$CAGE_CTR" sh -c 'cat > /altroot/identity-init.sh' <<'EOF'
addgroup -g 4300 carol
adduser -D -u 4300 -G carol carol
addgroup -g 4301 cg1
addgroup -g 4302 cg2
addgroup carol cg1
addgroup carol cg2
EOF
e2e_run e2e_exec cage -l /altroot -n n4 -i /identity-init.sh -u carol -- id
e2e_check_rc "n4 init-created user rc" 0 "$E2E_RC"
e2e_check_contains "n4 init-created uid" "$E2E_OUT" "uid=4300(carol)"
e2e_check_contains "n4 complementary group cg1" "$E2E_OUT" "4301(cg1)"
e2e_check_contains "n4 complementary group cg2" "$E2E_OUT" "4302(cg2)"

# A named group created by the init script is resolved for the command that
# follows it, as a primary group (-g <name>) or a complementary group
# (-g <gid>:<name>); both use the group file re-read after --initscript.
docker exec -i "$CAGE_CTR" sh -c 'cat > /altroot/identity-init-g.sh' <<'EOF'
addgroup -g 4400 gonly
addgroup -g 4401 gcomp
EOF
e2e_run e2e_exec cage -l /altroot -n n5 -i /identity-init-g.sh -g gonly -- id
e2e_check_rc "n5 init-created named group rc" 0 "$E2E_RC"
e2e_check_contains "n5 named -g resolved as primary gid" "$E2E_OUT" "gid=4400(gonly)"

e2e_run e2e_exec cage -l /altroot -n n6 -i /identity-init-g.sh -g 4500:gcomp -- id
e2e_check_rc "n6 -g gid:name rc" 0 "$E2E_RC"
e2e_check_contains "n6 numeric primary gid" "$E2E_OUT" "gid=4500"
e2e_check_contains "n6 named complementary group resolved" "$E2E_OUT" "4401(gcomp)"

e2e_end

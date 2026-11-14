#!/bin/bash
# The yade binary against the Foam-Yade pin (#264 PR 4): built from the pin is
# OK, another commit with the same coupling API warns, another API fails, an
# unknown or missing commit warns; pgf exec --yade refuses the FAIL case.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
. "$PGF_ROOT/utilities/pgf/coupling.sh"
export XDG_CACHE_HOME="$TEST_TMP/cache"

g() { git -C "$TEST_TMP/fy" -c user.name=t -c user.email=t@t "$@"; }
c="$TEST_TMP/fy/pkg/openfoam/coupling/FoamYade"
mkdir -p "$c" && g init -q || die "git init failed"
echo "// api 1" >"$c/FoamYade.H" && g add -A && g commit -qm pin
pin="$(g rev-parse HEAD)"
echo "// not the API" >"$TEST_TMP/fy/README" && g add -A && g commit -qm docs
same="$(g rev-parse HEAD)"
echo "// api 2" >"$c/FoamYade.H" && g add -A && g commit -qm api
other="$(g rev-parse HEAD)"
g checkout -q "$pin"

# A scratch PGF tree whose index pins $pin.
tree="$TEST_TMP/pgf"
mkdir -p "$tree" && git -C "$tree" init -q &&
    git -C "$tree" update-index --add --cacheinfo "160000,$pin,submodules/Foam-Yade" || die "cannot pin"

# fake_yade COMMIT — a yade whose -v names COMMIT (none if empty).
fake_yade() {
    local f="$TEST_TMP/yade-${1:-none}"
    printf '#!/bin/sh\necho "Yade 2026.1%s"\n' "${1:+-git-${1:0:7}}" >"$f" && chmod +x "$f"
    printf '%s' "$f"
}
level() { PGF_ROOT="$tree" pgf_yade_check "$(fake_yade "$1")" "$TEST_TMP/fy" | cut -c1-4 | tr -d ' '; }

check "built from the pin: OK" [ "$(level "$pin")" = OK ]
check "another commit, same API: WARN" [ "$(level "$same")" = WARN ]
check "another API: FAIL" [ "$(level "$other")" = FAIL ]
check "a commit the trunk lacks: WARN" [ "$(level deadbeefdeadbeef)" = WARN ]
check "no commit reported: WARN" [ "$(level "")" = WARN ]
PGF_ROOT="$tree" pgf_yade_check "$(fake_yade "$other")" "$TEST_TMP/fy" >/dev/null
check "FAIL returns 1" [ $? -eq 1 ]

# The commit is cached per binary: a second call does not run yade.
y="$(fake_yade "$pin")"
first="$(pgf_yade_commit "$y")"
printf '#!/bin/sh\nexit 1\n' >"$TEST_TMP/broken" && chmod +x "$TEST_TMP/broken"
touch -r "$y" "$TEST_TMP/broken"
check "the commit is read from yade -v" [ "$first" = "${pin:0:7}" ]
check "and cached" [ "$(pgf_yade_commit "$y")" = "${pin:0:7}" ]

# pgf exec --yade refuses a yade with another coupling API.
prefix="$TEST_TMP/prefix"
mkdir -p "$prefix/bin" "$prefix/lib"
printf '#!/bin/sh\n' >"$prefix/bin/porousGasificationFoam" && chmod +x "$prefix/bin/porousGasificationFoam"
echo '{"variant": "yade1"}' >"$prefix/.pgf-build.json"
pgf_coupling_write_stamp "$prefix/lib" "$TEST_TMP/fy"
YADE_TRUNK="$TEST_TMP/fy" YADE_BIN="$(fake_yade "$other")" "$PGF" exec --prefix "$prefix" --yade -- true 2>"$TEST_TMP/err"
rc=$?
# This checkout's pin is not in the fake trunk, so the API comparison decides.
check "pgf exec --yade exits 2 on another coupling API (rc=$rc)" [ "$rc" -eq 2 ]
check "and says why" grep -q "coupling API differs" "$TEST_TMP/err"
YADE_TRUNK="$TEST_TMP/fy" YADE_BIN="$(fake_yade "$same")" "$PGF" exec --prefix "$prefix" --yade -- true 2>"$TEST_TMP/err"
check "pgf exec --yade runs with a warning on a commit-only difference" \
    bash -c '[ "$1" -eq 0 ] && grep -q "warning: yade .* same coupling API" "$2"' _ $? "$TEST_TMP/err"

# doctor --yade (#280): a trunk off the pin, from the environment, or the
# checkout's own submodule off its pin each warn.
. "$PGF_ROOT/utilities/pgf/commands.sh"
_r() { echo "$1 $2"; }
declare -A PGF_CONF_SRC=()
check "a trunk at the pin: no warning" [ -z "$(PGF_ROOT="$tree" pgf_trunk_pin_check "$TEST_TMP/fy")" ]
g checkout -q "$same"
check "a trunk off the pin warns with both commits" \
    bash -c 'grep -q "^WARN YADE_TRUNK is at ${2:0:10}, the submodule pin is ${3:0:10}" <<<"$1"' \
    _ "$(PGF_ROOT="$tree" pgf_trunk_pin_check "$TEST_TMP/fy")" "$same" "$pin"
PGF_CONF_SRC[YADE_TRUNK]=env
check "a trunk from the environment warns and says how to find the export" \
    bash -c 'grep -q "^WARN YADE_TRUNK=.* comes from the environment.*grep -n YADE_TRUNK" <<<"$1"' \
    _ "$(PGF_ROOT="$tree" pgf_doctor_trunk_source "$TEST_TMP/fy")"
PGF_CONF_SRC[YADE_TRUNK]=checkout
mkdir -p "$tree/submodules" && git -c advice.detachedHead=false clone -q "$TEST_TMP/fy" "$tree/submodules/Foam-Yade" &&
    git -C "$tree/submodules/Foam-Yade" -c advice.detachedHead=false checkout -q "$other" || die "cannot clone the submodule"
g checkout -q "$pin"
check "the checkout's own submodule off its pin warns, with the fix" \
    bash -c 'grep -q "^WARN submodules/Foam-Yade is at ${2:0:10}, off its pin.*git submodule update" <<<"$1"' \
    _ "$(PGF_ROOT="$tree" pgf_doctor_trunk_source "$TEST_TMP/fy")" "$other"
check "  also when it is the trunk itself" \
    bash -c 'grep -q "^WARN submodules/Foam-Yade is at" <<<"$1"' \
    _ "$(PGF_ROOT="$tree" pgf_trunk_pin_check "$tree/submodules/Foam-Yade")"

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; }
finish

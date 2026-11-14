#!/bin/bash
# pgf completion bash (#274 C): the script comes from the same table as --help,
# completes every subcommand and the dynamic values (builds, config keys and
# values, PGF_MPI, case dirs, tiers), and pgf setup installs it. Fake bashrc.

FAKE="$(mktemp -d "${TMPDIR:-/tmp}/pgf-fakefoam-XXXXXX")" || exit 2
: "${PGF_FOAM_BASHRC:=$FAKE/etc/bashrc}"
mkdir -p "$FAKE/etc" && : >"$FAKE/etc/bashrc"
. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
trap 'rm -rf "$TEST_TMP" "$FAKE"' EXIT

home="$TEST_TMP/home"
broot="$TEST_TMP/builds"
mkdir -p "$home" "$broot/abc123def456-yade0-v2406-gcc14" "$broot/half-built"
echo '{}' >"$broot/abc123def456-yade0-v2406-gcc14/.pgf-build.json"
export HOME="$home" PGF_BUILD_ROOT="$broot"
script="$TEST_TMP/pgf.bash"
"$PGF" completion bash >"$script" 2>"$TEST_TMP/err"
check "pgf completion bash exits 0" [ $? -eq 0 ]
check "  and is valid bash" bash -n "$script"

# comp LINE — the words _pgf offers for LINE (cursor at the end), one per line.
comp() {
    bash -c '. "$1"; COMP_LINE="$2"; COMP_POINT=${#2}; COMP_WORDS=("$3"); COMP_WORDBREAKS=" "
        _pgf; printf "%s\n" "${COMPREPLY[@]}"' _ "$script" "$1" "$PGF" 2>/dev/null
}
has() { grep -qx -- "$1" <<<"$2"; }

out="$(comp "pgf ")"
for c in $("$PGF" --help | sed -n 's/^  pgf \([a-z]*\).*/\1/p'); do
    check "every subcommand in --help completes: $c" has "$c" "$out"
done
check "the table drives --help: one usage line per subcommand" \
    [ "$("$PGF" --help | grep -c '^  pgf ')" -eq "$(grep -c '^pgf_cmd ' "$PGF_ROOT/utilities/pgf/cli.sh")" ]
check "a prefix narrows the subcommands" [ "$(comp "pgf bu")" = "$(printf 'build\nbuilds')" ]
check "exec options include the environment options" has --use "$(comp "pgf exec --")"
check "build options come from build.sh" has --libs-only "$(comp "pgf build --")"

out="$(comp "pgf use ")"
check "use completes complete builds" has abc123def456-yade0-v2406-gcc14 "$out"
check "  and auto" has auto "$out"
check "  and worktree names" has "$(basename "$PGF_ROOT")" "$out"
check "  but not a prefix without .pgf-build.json" bash -c '! grep -qx half-built <<<"$1"' _ "$out"
check "--use completes builds too" has abc123def456-yade0-v2406-gcc14 "$(comp "pgf run --use ab")"

check "config verbs" [ "$(comp "pgf config ")" = "$(printf 'get\nset\nunset')" ]
check "config get completes keys" has PGF_MPI "$(comp "pgf config get PGF_M")"
check "config set completes KEY=" has PGF_MPI= "$(comp "pgf config set PGF_M")"
check "config set PGF_MPI= completes the values" \
    [ "$(comp "pgf config set PGF_MPI=lo")" = PGF_MPI=loopback ]
check "PGF_YADE values" [ "$(comp "pgf exec --set PGF_YADE=" | sort | tr '\n' ' ')" = "PGF_YADE=0 PGF_YADE=1 " ]
check "--mpi values" [ "$(comp "pgf shell --mpi " | tr '\n' ' ')" = "auto loopback default " ]

mkdir -p "$TEST_TMP/cases/plain/system"
printf '#!/bin/sh\n' >"$TEST_TMP/cases/plain/Allrun.short" && chmod +x "$TEST_TMP/cases/plain/Allrun.short"
check "run completes case dirs" has "$TEST_TMP/cases/plain" "$(comp "pgf run $TEST_TMP/cases/pl")"
out="$(comp "pgf run --yade $TEST_TMP/cases/plain ")"
check "  then the case's scripts" bash -c 'grep -qx Allrun <<<"$1" && grep -qx Allrun.short <<<"$1"' _ "$out"

out="$(comp "pgf test ")"
check "test completes the tiers from tiers.conf" bash -c 'grep -qx fast <<<"$1" && grep -qx all <<<"$1"' _ "$out"
check "test options" has --if-stale "$(comp "pgf test --if")"
check "after exec -- a command" has bash "$(comp "pgf exec --yade -- bas")"
check "completion completes bash" [ "$(comp "pgf completion ")" = bash ]

# pgf setup offers it; --yes installs it, --no-completion does not
tree="$TEST_TMP/tree"
mkdir -p "$tree/utilities/pgf"
cp "$PGF" "$tree/" && cp "$PGF_ROOT"/utilities/pgf/* "$tree/utilities/pgf/" || die "cannot copy pgf"
env -i PATH=/usr/bin:/bin HOME="$home" TMPDIR="$TEST_TMP" "$tree/pgf" setup --checkout --yes --no-link \
    --foam-bashrc "$PGF_FOAM_BASHRC" --no-completion >/dev/null 2>&1
check "setup --no-completion installs nothing" [ ! -e "$home/.local/share/bash-completion/completions/pgf" ]
env -i PATH=/usr/bin:/bin HOME="$home" TMPDIR="$TEST_TMP" "$tree/pgf" setup --checkout --yes --no-link \
    --foam-bashrc "$PGF_FOAM_BASHRC" >"$TEST_TMP/err" 2>&1
check "setup --yes installs the completion script" \
    grep -q '^complete -F _pgf pgf' "$home/.local/share/bash-completion/completions/pgf"
check "  and says where once, in the summary" bash -c 'grep -q "bash completion: ~/.local/share/bash-completion/completions/pgf; takes effect in a new shell" "$1" &&
    ! grep -q "^pgf: bash completion in" "$1"' _ "$TEST_TMP/err"

[ "$TEST_FAILURES" -eq 0 ] || { echo "--- last stderr"; cat "$TEST_TMP/err"; }
finish

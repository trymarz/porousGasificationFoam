#!/bin/bash
# pgf dem run and its stall watchdog (#340), with a stub mpirun and no OpenFOAM
# or Yade: usage errors exit 2, a silent run is stopped with 124, a chatty one
# is not, --stall-timeout 0 turns the watchdog off, only the run's group is hit;
# the pgfDemRun shim forwards to it.

FAKE="$(mktemp -d "${TMPDIR:-/tmp}/pgf-fakefoam-XXXXXX")" || exit 2
: "${PGF_FOAM_BASHRC:=$FAKE/etc/bashrc}"
mkdir -p "$FAKE/etc" && : >"$FAKE/etc/bashrc"
. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
trap 'rm -rf "$TEST_TMP" "$FAKE"; [ -n "${sib:-}" ] && kill "$sib" 2>/dev/null' EXIT

bin="$TEST_TMP/bin"
case="$TEST_TMP/case"
mkdir -p "$bin" "$case/processor0"
touch "$case/x.py"
# The stub mpirun: STUB_MODE silent (one line, then a TERM-proof child sleeps),
# chatty (a line every 0.5 s for 4 s, then exit 7), quiet (silent 3 s, exit 0),
# args (print its arguments, exit 0). It writes its children's pids to $STUB_PIDS.
cat >"$bin/mpirun" <<'EOF'
#!/bin/bash
[ "$1" = --version ] && { echo "stub mpi"; exit 0; }
case "$STUB_MODE" in
silent)
    echo "stub step 1"
    bash -c 'trap "" TERM; sleep 120' &
    echo "$!" >"$STUB_PIDS"
    sleep 120 &
    echo "$!" >>"$STUB_PIDS"
    wait ;;
chatty) for i in 1 2 3 4 5 6 7 8; do echo "stub step $i"; sleep 0.5; done; exit 7 ;;
quiet) sleep 3; exit 0 ;;
args) printf '<%s>' "$@"; echo ;;
esac
EOF
chmod +x "$bin/mpirun"

# dem ARGS — pgf dem run in the case dir with the stub; S=2 s, grace 1 s by default.
dem() {
    (cd "$case" && PATH="$bin:$PATH" PGF_DEM_KILL_GRACE=1 STUB_PIDS="$TEST_TMP/pids" "$PGF" dem run "$@")
}

# -- usage and case errors exit 2 ---------------------------------------------
for args in "--bogus x.py" "" "nosuch.py" "-nfoam x x.py" "-nfoam 2 x.py" "-ndem 0 -nfoam 1 x.py" \
    "-nfoam" "--stall-timeout 1.5 -nfoam 1 x.py"; do
    # shellcheck disable=SC2086 # the words are the arguments
    STUB_MODE=args dem $args >/dev/null 2>"$TEST_TMP/err"
    rc=$?
    check "pgf dem run $args exits 2 (rc=$rc)" bash -c '[ "$1" -eq 2 ] && grep -q "^pgf dem run: " "$2"' _ "$rc" "$TEST_TMP/err"
done
(cd "$case" && "$PGF" dem nosuch) >/dev/null 2>&1
check "an unknown dem command exits 2" [ $? -eq 2 ]
check "pgf dem --help is pgf help dem" cmp -s <("$PGF" dem --help 2>&1) <("$PGF" help dem 2>&1)
check "  and so is pgf dem run -h" cmp -s <(cd "$case" && "$PGF" dem run -nfoam 1 -h 2>&1) <("$PGF" help dem 2>&1)

# -- arguments reach mpirun and the ranks -------------------------------------
out="$(STUB_MODE=args PGF_MPIRUN_FLAGS="--foo bar" dem -ndem 3 -nfoam 1 -solver mySolver x.py a "b c" 2>&1)"
check "mpirun gets the flags, -np N+M, the role switch and the yade args" \
    grep -qF "<--foo><bar><-np><4><$PGF_ROOT/utilities/pgf/dem.sh><--role><3><mySolver><yade><x.py><a><b c>" <<<"$out"
shim="$PGF_ROOT/tutorials/cases/DEM_common/pgfDemRun"
out="$(cd "$case" && env -u PGF_ROOT PATH="$bin:$PATH" STUB_MODE=args "$shim" -nfoam 1 x.py "b c" 2>"$TEST_TMP/err")"
check "the pgfDemRun shim forwards its arguments to pgf dem run" \
    grep -qF "<-np><3><$PGF_ROOT/utilities/pgf/dem.sh><--role><2><porousGasificationFoam><yade><x.py><b c>" <<<"$out"
check "  and says it is deprecated" grep -q "^pgfDemRun: deprecated; use: pgf dem run" "$TEST_TMP/err"
printf '#!/bin/sh\necho "yade $*"\n' >"$bin/yade"
printf '#!/bin/sh\necho "solver $*"\n' >"$bin/mySolver"
chmod +x "$bin/yade" "$bin/mySolver"
role=("$PGF_ROOT/utilities/pgf/dem.sh" --role 3 mySolver yade x.py a)
check "a rank below -ndem runs yade with the script" \
    [ "$(PATH="$bin:$PATH" OMPI_COMM_WORLD_RANK=2 "${role[@]}")" = "yade x.py a" ]
check "  the others the solver, -parallel" \
    [ "$(PATH="$bin:$PATH" PMIX_RANK=3 "${role[@]}")" = "solver -parallel -mpi-split-by-appnum -mpi-threads" ]

# -- a silent run is stopped; nothing outside its group is touched ------------
sleep 120 &
sib=$!
t0=$(date +%s)
STUB_MODE=silent dem -nfoam 1 --stall-timeout 2 x.py >"$TEST_TMP/out" 2>"$TEST_TMP/err"
rc=$? took=$(($(date +%s) - t0))
check "a silent run exits 124 (rc=$rc)" [ "$rc" -eq 124 ]
check "  within S + grace + 2 s ($took s)" [ "$took" -le 5 ]
check "  and says why, with the last line" bash -c 'grep -qx "pgf dem run: no output for 2 s — run stalled, stopped" "$1" &&
    grep -qx "pgf dem run: last line: stub step 1" "$1"' _ "$TEST_TMP/err"
check "  its output was shown as it came" grep -qx "stub step 1" "$TEST_TMP/out"
check "  its children are gone, the TERM-proof one too (KILL after grace)" \
    bash -c 'for p in $(cat "$1"); do kill -0 "$p" 2>/dev/null && exit 1; done; exit 0' _ "$TEST_TMP/pids"
check "  a sibling outside its group survives" kill -0 "$sib"

# -- a run that keeps printing is not stopped ---------------------------------
STUB_MODE=chatty dem -nfoam 1 --stall-timeout 2 x.py >"$TEST_TMP/out" 2>"$TEST_TMP/err"
rc=$?
check "a run printing past S is not stopped; its status passes through (rc=$rc)" [ "$rc" -eq 7 ]
check "  all of its output arrives" grep -qx "stub step 8" "$TEST_TMP/out"
check "  and there is no stall message" bash -c '! grep -q "stalled" "$1"' _ "$TEST_TMP/err"

# -- --stall-timeout 0 / PGF_DEM_STALL_TIMEOUT=0 turn it off -------------------
STUB_MODE=quiet dem -nfoam 1 --stall-timeout 0 x.py >"$TEST_TMP/out" 2>&1
rc=$?
check "--stall-timeout 0 lets a run silent for longer than any S finish (rc=$rc)" [ "$rc" -eq 0 ]
check "  and the banner says the watchdog is off" grep -q "(stall timeout: off)" "$TEST_TMP/out"
STUB_MODE=quiet PGF_DEM_STALL_TIMEOUT=1 dem -nfoam 1 x.py >/dev/null 2>&1
rc=$?
check "PGF_DEM_STALL_TIMEOUT sets S (rc=$rc)" [ "$rc" -eq 124 ]
STUB_MODE=quiet PGF_DEM_STALL_TIMEOUT=0 dem -nfoam 1 x.py >/dev/null 2>&1
check "  and 0 there turns it off too" [ $? -eq 0 ]

# -- a TERM to the launcher reaches the run -----------------------------------
rm -f "$TEST_TMP/pids"
(cd "$case" && PATH="$bin:$PATH" PGF_DEM_KILL_GRACE=1 STUB_MODE=silent STUB_PIDS="$TEST_TMP/pids" \
    exec "$PGF" dem run -nfoam 1 --stall-timeout 60 x.py) >/dev/null 2>&1 &
pid=$!
for _ in $(seq 1 50); do [ -s "$TEST_TMP/pids" ] && [ "$(wc -l <"$TEST_TMP/pids")" -eq 2 ] && break; sleep 0.1; done
kill -TERM "$pid"
wait "$pid"
rc=$?
check "a TERM to pgf dem run ends it with 143 (rc=$rc)" [ "$rc" -eq 143 ]
check "  and its children with it" \
    bash -c 'for p in $(cat "$1"); do kill -0 "$p" 2>/dev/null && exit 1; done; exit 0' _ "$TEST_TMP/pids"
check "  the sibling still survives" kill -0 "$sib"

finish

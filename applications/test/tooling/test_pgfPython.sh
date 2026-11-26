#!/bin/bash
# pgf runs its own python3 (#362): a python3 first on the caller's PATH (conda,
# a venv, /usr/local/bin) is not used; PGF_PYTHON names another one, and must be
# an absolute path. pgf doctor reports the Python it uses.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

fake="$TEST_TMP/fakebin"
mkdir -p "$fake"
printf '#!/bin/sh\necho FAKE python3\nexit 7\n' >"$fake/python3"
chmod +x "$fake/python3"
probe='import sys; print(sys.executable)'

out="$(PATH="$fake:$PATH" "$PGF" shell --bare -- bash -c "command -v python3" 2>/dev/null)"
check "python3 in pgf's environment is pgf's (got: $out)" [ "$out" = "$PGF_ROOT/utilities/pgf/python/python3" ]
out="$(PATH="$fake:$PATH" "$PGF" shell --bare -- python3 -c "$probe" 2>/dev/null)"
check "it runs /usr/bin/python3 (got: $out)" [ "$out" = /usr/bin/python3 ]

# pgf test keeps the caller's environment; --status reads its records with python3.
state="$TEST_TMP/state"
mkdir -p "$state/pgf/runs/last"
printf '{"tier": "fast", "outcome": "PASS", "end": "2026-01-01T00:00:00Z"}\n' >"$state/pgf/runs/last/fast.json"
PATH="$fake:$PATH" XDG_STATE_HOME="$state" "$PGF" test --status >"$TEST_TMP/status.out" 2>&1
check "pgf test, in the caller's environment, does not run the caller's python3" \
    bash -c '! grep -q "FAKE" "$1"' _ "$TEST_TMP/status.out"

other="$(readlink -f /usr/bin/python3)"
out="$(PATH="$fake:$PATH" PGF_PYTHON="$other" "$PGF" shell --bare -- python3 -c "$probe" 2>/dev/null)"
check "PGF_PYTHON names the python3 (got: $out)" [ "$out" = "$other" ]

PGF_PYTHON=python3 "$PGF" shell --bare -- python3 -c 1 >/dev/null 2>"$TEST_TMP/rel.err"
rc=$?
check "a relative PGF_PYTHON is refused (rc=$rc)" [ "$rc" -eq 2 ]
check "the refusal names PGF_PYTHON" grep -q "PGF_PYTHON must be an absolute path" "$TEST_TMP/rel.err"

PATH="$fake:$PATH" "$PGF" doctor >"$TEST_TMP/doctor.out" 2>&1
check "pgf doctor reports pgf's python3" grep -q "OK   python3 is /usr/bin/python3 (" "$TEST_TMP/doctor.out"
finish

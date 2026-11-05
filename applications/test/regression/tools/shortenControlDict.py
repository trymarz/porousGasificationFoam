#!/usr/bin/env python3
"""Shorten a copied case's run to N solver steps for a DEM smoke run.

Sets endTime = startTime + N * deltaT and writeInterval to half the run, so the
run still writes two solver times. Edits through foamDictionary, so the
OpenFOAM environment must be loaded. Only for a run copy: runDEMCase.sh calls
it with --short-steps, after staging the case under --run-dir.

The Yade side follows on its own: the case's Yade script derives its step count
from the edited controlDict (tutorials/cases/DEM_common/yadeEndpoint.py).

Usage: shortenControlDict.py <controlDict> <N>
Prints one summary line; exits 1 with a message on stderr when it cannot.
"""

import subprocess
import sys


def foam_get(path, key, default=None):
    r = subprocess.run(
        ["foamDictionary", path, "-entry", key, "-value"],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        if default is not None:
            return default
        raise SystemExit(f"{path}: cannot read '{key}'")
    return r.stdout.strip()


def foam_set(path, key, value):
    r = subprocess.run(
        ["foamDictionary", path, "-entry", key, "-set", value],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        raise SystemExit(f"{path}: cannot set '{key}' to {value}: {r.stderr.strip()}")


def main(argv):
    if len(argv) != 3 or not argv[2].isdigit() or int(argv[2]) < 2:
        raise SystemExit("usage: shortenControlDict.py <controlDict> <N >= 2>")
    path, n = argv[1], int(argv[2])

    try:
        start = float(foam_get(path, "startTime", "0"))
        dt = float(foam_get(path, "deltaT"))
    except ValueError:
        raise SystemExit(f"{path}: startTime / deltaT are not plain numbers")
    control = foam_get(path, "writeControl", "timeStep")

    if control == "timeStep":
        interval = f"{n // 2}"
    elif control in ("runTime", "adjustableRunTime"):
        interval = f"{n // 2 * dt:.12g}"
    else:
        raise SystemExit(f"{path}: writeControl {control} cannot be shortened")

    end = f"{start + n * dt:.12g}"
    foam_set(path, "endTime", end)
    foam_set(path, "writeInterval", interval)
    print(f"endTime {end} ({n} steps of {dt:.9g}), writeInterval {interval} ({control})")


if __name__ == "__main__":
    try:
        main(sys.argv)
    except SystemExit as e:
        if isinstance(e.code, str):
            print(e.code, file=sys.stderr)
            sys.exit(1)
        raise

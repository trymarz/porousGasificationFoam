"""Derive a coupled Yade run's step count from the OpenFOAM case's controlDict.

The coupling pairs one exchange per OpenFOAM time step, not per unit of
physical time (Foam-Yade FoamCoupling::exchangeData). Yade exchanges at
iterations 0, k, 2k, ... with

    k = int(deltaT_foam / dt_yade)   if dt_yade < deltaT_foam, else 1

(C++ truncation of the double quotient), and the solver exchanges once per
time step. Neither side has an early-exit handshake, so both stop cleanly only
when ceil(NSTEPS / k) equals the solver's step count N_F; any other NSTEPS
leaves one side blocked in an exchange the other will never answer.

The solver's controlDict is authoritative: this module returns NSTEPS = N_F * k
and refuses (YadeEndpointError) when the solver's step count cannot be known in
advance. Every successful call prints one line starting with TAG, which the DEM
regression runner requires in the run log.

Usage, after O.dt is fixed and before mp.mpirun / O.run:

    sys.path.insert(0, os.path.join(os.getcwd(), '..', 'DEM_common'))
    import yadeEndpoint
    NSTEPS = yadeEndpoint.yadeSteps(O.dt, dynDt=O.dynDt)
"""

import math
import os
import re

TAG = "[yadeEndpoint]"

_TRUE = {"yes", "on", "true", "y", "t"}
_FALSE = {"no", "off", "false", "n", "f", "none"}


class YadeEndpointError(RuntimeError):
    """The solver's step count cannot be derived from its controlDict."""


def _strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return re.sub(r"(?m)^\s*#.*$", " ", text)


def readControlDict(path):
    """Return the top-level `key value;` entries of an OpenFOAM dictionary."""
    with open(path) as f:
        text = _strip_comments(f.read())
    entries, depth, start = {}, 0, 0
    for i, ch in enumerate(text):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            start = i + 1
        elif ch == ";" and depth == 0:
            words = text[start:i].split()
            if len(words) >= 2:
                entries[words[0]] = " ".join(words[1:])
            start = i + 1
    return entries


def _number(entries, key, path, default=None):
    if key not in entries:
        if default is None:
            raise YadeEndpointError(f"{path}: no '{key}' entry")
        return default
    try:
        return float(entries[key])
    except ValueError:
        raise YadeEndpointError(
            f"{path}: '{key} {entries[key]}' is not a plain number"
        ) from None


def foamSteps(caseDir="."):
    """Return (N_F, deltaT, note) for the solver run that controlDict describes.

    N_F counts Time::run's steps: n runs while start + (n-1) dt < end - dt/2.
    """
    path = os.path.join(caseDir, "system", "controlDict")
    d = readControlDict(path)

    adjust = d.get("adjustTimeStep", "no").lower()
    if adjust in _TRUE:
        raise YadeEndpointError(
            f"{path}: adjustTimeStep {adjust} makes the solver's step count "
            "unknown in advance; the coupled endpoint needs a fixed deltaT"
        )
    if adjust not in _FALSE:
        raise YadeEndpointError(f"{path}: cannot read adjustTimeStep '{adjust}'")
    stopAt = d.get("stopAt", "endTime")
    if stopAt != "endTime":
        raise YadeEndpointError(f"{path}: stopAt {stopAt}; only endTime is supported")
    startFrom = d.get("startFrom", "startTime")
    if startFrom != "startTime":
        raise YadeEndpointError(
            f"{path}: startFrom {startFrom}; only startTime is supported"
        )

    start = _number(d, "startTime", path, default=0.0)
    end = _number(d, "endTime", path)
    dt = _number(d, "deltaT", path)
    if dt <= 0.0 or end <= start:
        raise YadeEndpointError(f"{path}: needs deltaT > 0 and endTime > startTime")

    ratio = (end - start) / dt
    n = math.ceil(ratio + 0.5) - 1
    note = ""
    if abs(ratio - round(ratio)) > 1e-9 * max(1.0, ratio):
        note = f" (endTime - startTime = {ratio:.6g} deltaT, solver runs {n} steps)"
    return n, dt, note


def exchangeInterval(foamDt, yadeDt):
    """Yade iterations per exchange, as FoamCoupling computes it."""
    return int(foamDt / yadeDt) if yadeDt < foamDt else 1


def yadeSteps(yadeDt, dynDt=False, caseDir=".", log=print):
    """Return the NSTEPS that ends Yade on the solver's last exchange."""
    try:
        if dynDt:
            raise YadeEndpointError("O.dynDt is on; the endpoint needs a fixed O.dt")
        if not yadeDt > 0.0:
            raise YadeEndpointError(f"O.dt = {yadeDt}; set a positive O.dt first")
        nFoam, foamDt, note = foamSteps(caseDir)
    except YadeEndpointError as e:
        log(f"{TAG} ERROR: {e}")
        raise
    k = exchangeInterval(foamDt, yadeDt)
    nsteps = nFoam * k
    log(
        f"{TAG} solverSteps={nFoam} foamDeltaT={foamDt:.9g} yadeDt={yadeDt:.9g} "
        f"interval={k} NSTEPS={nsteps}{note}"
    )
    return nsteps

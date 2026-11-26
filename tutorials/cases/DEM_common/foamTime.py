"""The solver's time settings, read from the case's system/controlDict.

controlDict is the one place that sets a coupled run's time (#328). The Yade
script follows it: O.dt is deltaT divided by a whole number of DEM substeps,
Yade writes at the solver's writeInterval, and it sets no step count of its
own; the coupling ends Yade on the solver's last step (PgfCoupling::action).

Usage, before O.engines and mp.mpirun:

    sys.path.insert(0, os.path.join(os.getcwd(), '..', 'DEM_common'))
    import foamTime
    foam = foamTime.read()
    O.dt = foam.deltaT / DEM_SUBSTEPS
    writePeriod = foam.iterations(foam.writeInterval, O.dt)
    ...
    mp.mpirun(foamTime.UNTIL_SOLVER_ENDS)
"""

import os
import re
from collections import namedtuple

# Yade's step budget: far more than any run takes; OpenFOAM's end stops Yade.
UNTIL_SOLVER_ENDS = 10**12


class FoamTime(namedtuple("FoamTime", "startTime endTime deltaT writeInterval")):
    """Times in seconds; writeInterval is converted from timeStep writeControl."""

    @staticmethod
    def iterations(interval, dt):
        """Whole Yade iterations in `interval` seconds of step `dt` (at least 1)."""
        return max(1, int(round(interval / dt)))


def _strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return re.sub(r"(?m)^\s*#.*$", " ", text)


def _topLevelEntries(path):
    """The top-level `key value;` entries of an OpenFOAM dictionary."""
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


def read(caseDir="."):
    """Return the FoamTime of caseDir/system/controlDict."""
    path = os.path.join(caseDir, "system", "controlDict")
    d = _topLevelEntries(path)

    def number(key, default=None):
        if key not in d and default is not None:
            return default
        try:
            return float(d[key])
        except (KeyError, ValueError):
            raise ValueError(f"{path}: '{key}' must be a plain number") from None

    deltaT = number("deltaT")
    writeInterval = number("writeInterval")
    writeControl = d.get("writeControl", "timeStep")
    if writeControl == "timeStep":
        writeInterval *= deltaT
    elif writeControl not in ("runTime", "adjustableRunTime"):
        raise ValueError(f"{path}: writeControl {writeControl} has no fixed write time")
    return FoamTime(number("startTime", 0.0), number("endTime"), deltaT, writeInterval)

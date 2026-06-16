---
name: pgf-dem-monitor
description: Monitor a running PGF-DEM case (YADE-coupled porousGasificationFoam). Use when the user asks to check how a case is running, monitor progress, diagnose problems, or inspect DEM coupling state. Works from inside the container by reading log files and processor directories via /workspace bind-mount (the case runs on the host/KVM; all output is visible in the shared filesystem).
---

# PGF-DEM Case Monitor

Monitor a running `porousGasificationFoam` + YADE DEM case. Read the log,
check key physics quantities, and alert on anomalies.

## Quick health check (use first)

```bash
TAIL=$(tail -1 "log.yade" 2>/dev/null || tail -1 "log.porousGasificationFoam")
TIME=$(rg "^Time = " "log.yade" 2>/dev/null | tail -1)
COURANT=$(rg "Courant Number" "log.yade" 2>/dev/null | tail -1)
RHO=$(rg "rho max/min" "log.yade" 2>/dev/null | tail -1)
echo "LAST: $TAIL"
echo "TIME: $TIME"
echo "COURANT: $COURANT"
echo "RHO: $RHO"
```

Red flags:
- **rho min < 0** → crash imminent (chemistry blow-up or CFL too high)
- **cumulative continuity error growing unbounded** → divergence
- **Courant max > 1** → CFL violation
- **Final residual = initial residual (stalled)** → solver not converging
- **"Floating point exception" or "Segmentation fault"** → crashed

## Progress & ETA

```bash
# Current time and end time
rg "^Time = " log.yade | tail -1
rg "^endTime" system/controlDict

# Execution time and clock time
rg "ExecutionTime" log.yade | tail -1

# Calculate ETA from clock time vs time fraction
```

## Stability metrics

```bash
# Courant number (watch max; should stay < 0.5–1.0)
rg "Courant Number" log.yade | tail -3

# Current timestep (deltaT)
rg "^deltaT" log.yade | tail -3

# Density bounds (rho min MUST stay > 0)
rg "rho max/min" log.yade | tail -5

# Cumulative continuity error (should converge, not grow)
rg "cumulative =" log.yade | tail -5
```

A rising cumulative continuity error that never plateaus indicates mass conservation failure.

## Gas phase

```bash
# Gas temperature
rg "T gas min/max" log.yade | tail -3

# Gas species (with chemistry off, species should be static)
rg " gas " log.yade | tail -5
```

## Solid phase (porous media)

```bash
# Solid enthalpy (hs) — proxy for Ts. Wood Cp ~1500 J/(kg·K).
# Ts ≈ 300 + hs/Cp. At Ts > 500K shrinkage starts; > 800K max rate.
rg "hs min/max" log.yade | tail -3

# Solid species mass fractions (0–1). With chemistry off, Ywood=1, Ychar=0.
rg "Ywood.*min|Ychar.*min" log.yade | tail -3

# Solid chemistry source term (Sh_solid). 0 when chemistry is off.
rg "solidChemistrySh" log.yade | tail -3

# Porosity (initial ~0.26, max ~1.0 in gas-only cells)
rg "porosity min/max" log.yade | tail -3

# Interphase heat transfer (W/m3). Negative = gas heats solid.
rg "heatTransfer min" log.yade | tail -3
```

## DEM coupling (YADE spheres)

```bash
# Is YADE active?
rg "DEM coupling: active|YADE.*Created.*spheres" log.yade

# lambdaDot values on spheres (1.0 = no shrinkage, < 1 = shrinking)
# Written to processor*/<time>/ParticlesData.txt at each output time
LATEST_TIME=$(rg -r '$1' -o 'processor0/(\S+)/ParticlesData.txt' -l . 2>/dev/null | rg -o 'processor0/([^/]+)' -r '$1' | sort -V | tail -1)
if [ -n "$LATEST_TIME" ]; then
  echo "Latest output: $LATEST_TIME"
  rg "lambdaDot" -c "processor0/$LATEST_TIME/ParticlesData.txt" 2>/dev/null || true
  # Count unique lambdaDot values
  tail -n +2 "processor0/$LATEST_TIME/ParticlesData.txt" | awk '{print $3}' | sort -u
fi
```

```bash
# Sphere radii from VTP files (spheres/ directory)
# Check if radii are changing over time
for f in spheres/spheres-rank0-*.vtp; do
  echo -n "$(basename $f): "
  python3 -c "
import xml.etree.ElementTree as ET
tree = ET.parse('$f')
for elem in tree.iter():
    if 'PointData' in elem.tag:
        for child in elem:
            if child.get('Name') == 'radius':
                vals = child.text.strip().split()
                if vals: print(f'{float(vals[0]):.6f}  (n={len(vals)})')
                break
        break
" 2>&1
done
```

## Residuals convergence

```bash
# Velocity residuals (should converge to 1e-15 in 1–3 iterations)
rg "Solving for U[xyz]" log.yade | tail -6

# Pressure residual (first corrector)
rg "Solving for p, " log.yade | tail -3

# Enthalpy residual
rg "Solving for h," log.yade | tail -3

# Species residuals (N2, targas)
rg "Solving for (N2|targas)" log.yade | tail -4
```

PIMPLE iterating more than 2–3 pressure correctors per step indicates stiff coupling.

## Troubleshooting from the log

| Symptom | Check |
|---------|-------|
| FPE / SIGFPE crash | `rho max/min` → negative density; `solidChemistrySh` → chemistry blow-up |
| Spheres not shrinking | `lambdaDot` in ParticlesData.txt (should be < 1); `Ts` values in spheres cells |
| Ts not rising | `heatTransfer` values; `chemistry off` means no exothermic source |
| High continuity error | `Courant Number max`; `deltaT`; PIMPLE convergence |
| Species blowing up | `Ynorm` values; species source terms |
| YADE not responding | `DEM coupling: active` in log; check `yadeProperties` `active true` |

## Monitor script (quick copy-paste)

Use this as a one-shot snapshot:

```bash
#!/bin/bash
LOG="${1:-log.yade}"
echo "=== PROGRESS ==="
rg "^Time = " "$LOG" | tail -1
rg "ExecutionTime" "$LOG" | tail -1
rg "Courant Number" "$LOG" | tail -1
rg "^deltaT" "$LOG" | tail -1
echo "=== STABILITY ==="
rg "rho max/min" "$LOG" | tail -1
rg "cumulative =" "$LOG" | tail -1
echo "=== GAS ==="
rg "T gas" "$LOG" | tail -1
echo "=== SOLID ==="
rg "hs min/max" "$LOG" | tail -1
rg "Ywood.*min" "$LOG" | tail -1
rg "heatTransfer min" "$LOG" | tail -1
echo "=== RESIDUALS ==="
rg "Solving for U[xyz]" "$LOG" | tail -3
rg "Solving for p, " "$LOG" | tail -1
```

## Notes

- The case runs on the host/KVM guest; the container sees all output via the `/workspace` bind mount
- `foamcli` is NOT available inside the container — use `rg`/`tail`/`python3` on the log file instead
- For decomposed cases (multiple processors), output lands in `processor*/<time>/`
- Sphere VTP files are written to `spheres/` at the case root by the YADE coupling
- `ParticlesData.txt` in each processor time directory contains `(particleID cellID lambdaDot)` triplets — this is the per-sphere shrink signal sent from OpenFOAM to YADE
- `writeParticlesData()` only writes at output times (`writeInterval` in controlDict)
- `lambdaDotModel::update()` runs every timestep and sets `partPtr->lambdaDot` on every sphere — the YADE side must consume this to actually shrink radii

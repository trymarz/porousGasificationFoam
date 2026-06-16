#!/bin/bash
# monitor-poster.sh — Check posterDemo progress against the two coupling goals:
#   1. PGF → YADE: shrink spheres (Ts → lambdaDot → radius decay)
#   2. YADE → PGF: advect solid material (DEM sphere motion → Us → Ywood→Ychar)
#
# Usage: ./monitor-poster.sh          # quick status
#        ./monitor-poster.sh full     # detailed report with field values
#        watch -n 10 ./monitor-poster.sh  # auto-refresh every 10s

set -uo pipefail  # no -e: handle missing files/dirs gracefully
CASE_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG="$CASE_DIR/log.yade"

# ── ANSI ───────────────────────────────────────────────────────────
BOLD='\033[1m'; GREEN='\033[32m'; YELLOW='\033[33m'; RED='\033[31m'
CYAN='\033[36m'; DIM='\033[2m';   NC='\033[0m'
ok()  { echo -e "  ${GREEN}✓${NC} $*"; }
warn(){ echo -e "  ${YELLOW}⚠${NC} $*"; }
bad() { echo -e "  ${RED}✗${NC} $*"; }
hdr() { echo -e "\n${BOLD}${CYAN}── $* ──${NC}"; }

# ── Progress ───────────────────────────────────────────────────────
hdr "Progress"
if [[ -f "$LOG" ]]; then
    LAST_TIME=$(rg -o '^Time = ([0-9.e+-]+)' "$LOG" 2>/dev/null | tail -1 | awk '{print $NF}')
    N_STEPS=$(rg -c '^Time = ' "$LOG" 2>/dev/null)
    LAST_COURANT=$(rg 'Courant Number mean:' "$LOG" 2>/dev/null | tail -1 | grep -oP 'max: \K[\d.]+')
    LAST_RHO=$(rg 'rho max/min' "$LOG" 2>/dev/null | tail -1)
    ELAPSED=$(rg -oP 'ExecutionTime = [\d.]+ s  ClockTime = \K[\d]+' "$LOG" 2>/dev/null | tail -1)

    if [[ -n "$LAST_TIME" ]]; then
        PROGRESS=$(python3 -c "print(f'{float($LAST_TIME)/2.0*100:.1f}')" 2>/dev/null || echo "?")
        ok "Sim time: ${BOLD}t = $LAST_TIME / 2.0 s${NC} ($PROGRESS%)"
    fi
    [[ -n "$N_STEPS" ]] && echo "  Steps: $N_STEPS  |  Clock: ${ELAPSED:-?}s  |  Co max: ${LAST_COURANT:-?}"
    [[ -n "$LAST_RHO" ]] && echo "  $LAST_RHO"

    # Check for crashes
    if rg -q "Floating point exception\|Segmentation fault\|Signal:" "$LOG" 2>/dev/null; then
        bad "CRASH detected in log!"
        rg "Signal:|Floating point" "$LOG" | tail -1
    fi
else
    warn "No log.yade found — case may not be running yet"
fi

# ── Goal 1: PGF → YADE (sphere shrinkage) ──────────────────────────
hdr "Goal 1: PGF → YADE  (Ts heats spheres → spheres shrink)"

# Sphere count from YADE startup
SPHERE_COUNT=$(rg -oP 'Created \K\d+' "$LOG" 2>/dev/null | head -1)
if [[ -n "$SPHERE_COUNT" ]]; then
    if [[ "$SPHERE_COUNT" -ge 300 ]]; then
        ok "Sphere count: ${BOLD}$SPHERE_COUNT${NC} (target 300)"
    else
        warn "Sphere count: $SPHERE_COUNT (target 300)"
    fi
else
    echo "  Waiting for YADE startup..."
fi

# Sphere radii from latest VTK
RANK1_VTKS=($(ls -t "$CASE_DIR/spheres"/spheres-rank1-*.vtp 2>/dev/null || true))
if [[ ${#RANK1_VTKS[@]} -gt 0 ]]; then
    LATEST_VTK="${RANK1_VTKS[0]}"
    VTK_TIME=$(basename "$LATEST_VTK" | grep -oP '[\d.]+(?=\.vtp)')
    RADII=$(python3 -c "
import xml.etree.ElementTree as ET
tree = ET.parse('$LATEST_VTK')
root = tree.getroot()
for pd in root.iter('PointData'):
    for da in pd.findall('DataArray'):
        if da.get('Name') == 'radius':
            vals = [float(x) for x in da.text.split()]
            print(f'{len(vals)} {min(vals):.6f} {max(vals):.6f} {sum(vals)/len(vals):.6f}')
" 2>/dev/null)
    if [[ -n "$RADII" ]]; then
        read N R_MIN R_MAX R_MEAN <<< "$RADII"
        SHRINK_PCT=$(python3 -c "print(f'{(1-$R_MEAN/0.004)*100:.1f}')" 2>/dev/null)
        ok "Spheres at t=$VTK_TIME: r = ${BOLD}$R_MEAN${NC} (shrink ${SHRINK_PCT}%), range [$R_MIN, $R_MAX], count=$N"

        # Compare rank0 and rank1 (lambdaDot sync check)
        RANK0="${LATEST_VTK/rank1/rank0}"
        if [[ -f "$RANK0" ]]; then
            R0_MEAN=$(python3 -c "
import xml.etree.ElementTree as ET
tree = ET.parse('$RANK0')
root = tree.getroot()
for pd in root.iter('PointData'):
    for da in pd.findall('DataArray'):
        if da.get('Name') == 'radius':
            vals = [float(x) for x in da.text.split()]
            print(f'{sum(vals)/len(vals):.6f}')
" 2>/dev/null)
            DIFF=$(python3 -c "print(f'{abs($R_MEAN - $R0_MEAN):.2e}')" 2>/dev/null)
            if [[ "$DIFF" == "0.00e+00" ]]; then
                ok "LambdaDot sync: rank0/rank1 radii identical (master←worker sync working)"
            else
                warn "LambdaDot sync: rank0=$R0_MEAN vs rank1=$R_MEAN (diff=$DIFF)"
            fi
        fi
    fi
else
    echo "  Waiting for first VTK write (t=0.05)..."
fi

# LambdaDot field from latest processor time dir
LATEST_PDIR=$(ls -d "$CASE_DIR/processor0"/[0-9]*.[0-9]* 2>/dev/null | sort -V | tail -1)
if [[ -n "$LATEST_PDIR" ]]; then
    TDIR=$(basename "$LATEST_PDIR")
    LD_FILE="$LATEST_PDIR/lambdaDot"
    NP_FILE="$LATEST_PDIR/nParticles"
    if [[ -f "$LD_FILE" ]]; then
        LD_VALS=$(python3 -c "
import re
with open('$LD_FILE', 'rb') as f:
    data = f.read()
# Find internalField block and parse scalar values after the opening (
idx = data.find(b'internalField')
if idx >= 0:
    tail = data[idx:].decode('ascii', errors='ignore')
    m = re.search(r'\(', tail)
    if m:
        block = tail[m.start():]
        # Parse all float tokens, filter out boundary-condition noise
        vals = []
        for tok in re.findall(r'[\d.e+\-]+', block):
            try: vals.append(float(tok))
            except: pass
        # Only keep values in the [0.9, 1.0] range (lambdaDot domain)
        vals = [v for v in vals if 0.9 <= v <= 1.0]
        if vals:
            print(f'{min(vals):.6f} {max(vals):.6f} {sum(vals)/len(vals):.6f} {len(vals)}')
" 2>/dev/null)
        if [[ -n "$LD_VALS" ]]; then
            read LD_MIN LD_MAX LD_MEAN LD_N <<< "$LD_VALS"
            SHRINK_RATE=$(python3 -c "print(f'{(1-$LD_MEAN)*100:.3f}')" 2>/dev/null)
            echo "  lambdaDot @ t=$TDIR: mean=${BOLD}$LD_MEAN${NC} (shrink ${SHRINK_RATE}%/step), range [$LD_MIN, $LD_MAX], $LD_N cells"
        fi
    fi
fi

# Ts from probes to explain lambdaDot
PROBE_FILE="$CASE_DIR/postProcessing/centerlineProbes/0/Ts"
if [[ -f "$PROBE_FILE" ]]; then
    TS_LINE=$(tail -1 "$PROBE_FILE" 2>/dev/null)
    if [[ -n "$TS_LINE" ]]; then
        echo "  Ts (bed probes, latest): $TS_LINE" | head -c 120
        echo
    fi
fi

# ── Goal 2: YADE → PGF (solid advection via Us) ────────────────────
hdr "Goal 2: YADE → PGF  (DEM motion → Us → solid advection)"

# Us field magnitude (vector field in nonuniform List<vector> format)
if [[ -n "$LATEST_PDIR" ]]; then
    US_FILE="$LATEST_PDIR/Us"
    if [[ -f "$US_FILE" ]]; then
        US_VALS=$(python3 -c "
import re
with open('$US_FILE', 'rb') as f:
    data = f.read()
# Find List<vector> then parse all (x y z) vectors after the opening (
idx = data.find(b'List<vector>')
if idx >= 0:
    tail = data[idx:].decode('ascii', errors='ignore')
    m = re.search(r'\(', tail)
    if m:
        block = tail[m.start():]
        vecs = re.findall(r'\(([\d.e+\- ]+)\)', block)
        mags = []
        for v in vecs:
            parts = v.split()
            if len(parts) >= 3:
                try: mags.append((float(parts[0])**2+float(parts[1])**2+float(parts[2])**2)**0.5)
                except: pass
        if mags: print(f'{min(mags):.6f} {max(mags):.6f} {sum(mags)/len(mags):.6f} {len(mags)}')
" 2>/dev/null)
        if [[ -n "$US_VALS" ]]; then
            read US_MIN US_MAX US_MEAN US_N <<< "$US_VALS"
            ok "Us @ t=$TDIR: |U| mean=${BOLD}$US_MEAN${NC} m/s, max=$US_MAX, $US_N cells"
            if [[ "$US_MEAN" == "0.000000" ]]; then
                warn "Us is zero — DEM spheres may not be moving or coupling not pushing velocity"
            fi
        fi
    fi
fi

# Solid species — probe data (bed region: z=0.03, 0.06, 0.09)
for SPEC in Ywood Ychar; do
    PF="$CASE_DIR/postProcessing/centerlineProbes/0/$SPEC"
    if [[ -f "$PF" ]]; then
        VALS=$(tail -1 "$PF" 2>/dev/null | awk '{print $2, $3, $4}')
        if [[ -n "$VALS" ]]; then
            AVG=$(python3 -c "vals=[float(x) for x in '$VALS'.split() if x.replace('.','').replace('-','').isdigit()]; print(f'{sum(vals)/len(vals):.4f}')" 2>/dev/null)
            echo "  $SPEC (bed avg, latest): ${BOLD}$AVG${NC}"
        fi
    fi
done

# Integrated solid mass
SM_FILE="$CASE_DIR/postProcessing/solidMass/0/volFieldValue.dat"
if [[ -f "$SM_FILE" ]]; then
    SM_LINE=$(tail -1 "$SM_FILE" 2>/dev/null)
    if [[ -n "$SM_LINE" ]]; then
        echo "  Solid mass (latest): $SM_LINE"
    fi
fi

# Porosity trend
if [[ -n "$LATEST_PDIR" ]]; then
    PORO_FILE="$LATEST_PDIR/porosityF"
    if [[ -f "$PORO_FILE" ]]; then
        PORO_VALS=$(python3 -c "
import re
with open('$PORO_FILE', 'rb') as f:
    data = f.read()
idx = data.find(b'internalField')
if idx >= 0:
    tail = data[idx:].decode('ascii', errors='ignore')
    m = re.search(r'\(', tail)
    if m:
        block = tail[m.start():]
        vals = []
        for tok in re.findall(r'[\d.e+\-]+', block):
            try: vals.append(float(tok))
            except: pass
        if vals:
            bed = [v for v in vals if v < 0.999]
            if bed: print(f'{min(bed):.4f} {max(bed):.4f} {sum(bed)/len(bed):.4f}')
" 2>/dev/null)
        if [[ -n "$PORO_VALS" ]]; then
            read P_MIN P_MAX P_MEAN <<< "$PORO_VALS"
            echo "  Porosity (bed): ${BOLD}$P_MEAN${NC}  [initial=0.40, range $P_MIN–$P_MAX]"
        fi
    fi
fi

# ── Stability ──────────────────────────────────────────────────────
hdr "Stability"
ISSUES=0

# Targas
if [[ -f "$LOG" ]]; then
    TARGAS_COUNT=$(rg -c 'too much.*targas' "$LOG" 2>/dev/null || echo 0)
    if [[ "$TARGAS_COUNT" -gt 100 ]]; then
        warn "Targas warnings: $TARGAS_COUNT (chemistry may be stiff)"
        ((ISSUES++))
    elif [[ "$TARGAS_COUNT" -gt 0 ]]; then
        echo "  Targas warnings: $TARGAS_COUNT (acceptable)"
    else
        ok "No targas warnings"
    fi

    # Negative rho
    NEG_RHO=$(rg 'rho max/min.*-' "$LOG" 2>/dev/null | wc -l)
    if [[ "$NEG_RHO" -gt 0 ]]; then
        bad "Negative rho detected in $NEG_RHO timesteps!"
        ((ISSUES++))
    else
        ok "No negative rho"
    fi

    # Courant
    HIGH_CO=$(rg -oP 'Courant Number mean:.*max: \K[\d.]+' "$LOG" 2>/dev/null | awk '{if($1>5) print}' | wc -l)
    if [[ "$HIGH_CO" -gt 0 ]]; then
        warn "Courant > 5 in $HIGH_CO steps"
        ((ISSUES++))
    else
        ok "Courant stable"
    fi
fi

# ── Summary ────────────────────────────────────────────────────────
hdr "Verdict"
if [[ "$ISSUES" -eq 0 ]]; then
    echo -e "  ${GREEN}${BOLD}✓ Both coupling goals on track${NC}"
else
    echo -e "  ${YELLOW}${BOLD}$ISSUES stability issue(s) detected${NC}"
fi
echo

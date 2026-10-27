#!/bin/bash
#
# compareToolchains.sh: run test tiers on the same commit natively and inside the
# pgf-toolchain image, and report which cases fail the committed baselines on each.
#
#   container/compareToolchains.sh --image SIF [--tiers "fast convergence"]
#                                  [--work DIR] [--skip-native] [--skip-image]
#
# Each side gets its own git-archive copy of HEAD under DIR (default
# ${PGF_RUN_ROOT:-${TMPDIR:-/tmp}/pgf-runs}/toolchains-<time>-<pid>), so their
# in-tree objects never meet. Native: pgf-test with the caller's PGF_BUILD_ROOT
# (a cache hit when HEAD is already built). Image: pgf-test inside the image with
# its build and run roots under DIR. The report lists each case with both
# outcomes and ends with how many cases fail in the image (#120, PR 4).
#
# The native side needs PGF_FOAM_BASHRC or WM_PROJECT_DIR. Uncommitted changes
# are not tested: both sides run HEAD. Exit codes: 0 when no case fails in the
# image, 1 when some do, 2 on a usage or setup error.

set -u

PGF_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
IMAGE=""
TIERS="fast convergence"
WORK=""
DO_NATIVE=1
DO_IMAGE=1

die() {
  echo "compareToolchains: $*" >&2
  exit 2
}

while [ $# -gt 0 ]; do
  case "$1" in
    --image) IMAGE="${2:?--image needs a path}"; shift 2 ;;
    --tiers) TIERS="${2:?--tiers needs a list}"; shift 2 ;;
    --work) WORK="${2:?--work needs a path}"; shift 2 ;;
    --skip-native) DO_NATIVE=0; shift ;;
    --skip-image) DO_IMAGE=0; shift ;;
    -h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

if [ "$DO_IMAGE" = 1 ]; then
  [ -n "$IMAGE" ] || die "--image SIF is required (or --skip-image)"
  [ -r "$IMAGE" ] || die "cannot read image $IMAGE"
  IMAGE="$(cd -- "$(dirname -- "$IMAGE")" && pwd -P)/$(basename -- "$IMAGE")"
  command -v apptainer >/dev/null || die "apptainer not found"
fi
if [ "$DO_NATIVE" = 1 ] && [ -z "${PGF_FOAM_BASHRC:-}${WM_PROJECT_DIR:-}" ]; then
  die "native side: set PGF_FOAM_BASHRC or WM_PROJECT_DIR"
fi

WORK="${WORK:-${PGF_RUN_ROOT:-${TMPDIR:-/tmp}/pgf-runs}/toolchains-$(date +%Y%m%d-%H%M%S)-$$}"
mkdir -p "$WORK" || die "cannot create $WORK"
WORK="$(cd -- "$WORK" && pwd -P)"
HEAD="$(git -C "$PGF_ROOT" rev-parse HEAD)" || die "$PGF_ROOT is not a git checkout"
if [ -n "$(git -C "$PGF_ROOT" status --porcelain --untracked-files=no)" ]; then
  echo "compareToolchains: warning: uncommitted changes are not tested; both sides run HEAD $HEAD" >&2
fi
echo "$HEAD" > "$WORK/head"

# export_tree <dir> — a clean copy of HEAD, symlinks kept.
export_tree() {
  [ -d "$1" ] && return 0
  mkdir -p "$1" && git -C "$PGF_ROOT" archive --format=tar HEAD | tar -x -C "$1"
}

run_native() {
  local tier="$1"
  export_tree "$WORK/native/src" || die "cannot export HEAD for the native side"
  (cd "$WORK/native/src" && PGF_RUN_ROOT="$WORK/native/runs" ./pgf-test "$tier")
}

run_image() {
  local tier="$1"
  export_tree "$WORK/image/src" || die "cannot export HEAD for the image side"
  apptainer exec --cleanenv --bind "$WORK" --pwd "$WORK/image/src" \
    --env "PGF_BUILD_ROOT=$WORK/image/builds,PGF_RUN_ROOT=$WORK/image/runs" \
    "$IMAGE" ./pgf-test "$tier"
}

for tier in $TIERS; do
  if [ "$DO_NATIVE" = 1 ]; then
    echo "== native: $tier"
    run_native "$tier" 2>&1 | tee "$WORK/native-$tier.log"
  fi
  if [ "$DO_IMAGE" = 1 ]; then
    echo "== image: $tier"
    run_image "$tier" 2>&1 | tee "$WORK/image-$tier.log"
  fi
done

[ "$DO_IMAGE" = 1 ] && apptainer exec "$IMAGE" cat /opt/pgf-toolchain-manifest.txt > "$WORK/image-manifest.txt" 2>/dev/null

# Per-case table from both sides' last/<tier>.json -> result.json; exit 1 when
# a case does not PASS in the image.
python3 - "$WORK" "$HEAD" $TIERS <<'PY'
import json, os, sys
work, head, tiers = sys.argv[1], sys.argv[2], sys.argv[3:]

def side(name, tier):
    rec = os.path.join(work, name, "runs", "last", tier + ".json")
    if not os.path.exists(rec):
        return None, {}
    last = json.load(open(rec))
    cases = {}
    res = last.get("result_json") or ""
    if os.path.exists(res):
        for c in json.load(open(res)).get("cases", []):
            cases[c["case_id"]] = c
    return last, cases

def cell(c):
    if c is None:
        return "-"
    s = c.get("status", "?")
    if "compare_files_total" in c:
        s += " %d/%d" % (c.get("compare_files_passed", 0), c["compare_files_total"])
    return s

failed = []
print("\ntoolchain comparison, HEAD %s, work %s" % (head[:12], work))
for tier in tiers:
    n_last, n_cases = side("native", tier)
    i_last, i_cases = side("image", tier)
    print("\n[%s] native key %s, image key %s" % (
        tier, n_last["key"] if n_last else "-", i_last["key"] if i_last else "-"))
    for cid in sorted(set(n_cases) | set(i_cases)):
        n, i = n_cases.get(cid), i_cases.get(cid)
        print("  %-14s %-14s %s" % (cell(n), cell(i), cid))
        if i is not None and i.get("status") != "PASS":
            failed.append((tier, cid, i.get("message", "")))
    if i_last is None and os.path.isdir(os.path.join(work, "image")):
        failed.append((tier, "(no image record)", "see image-%s.log" % tier))

if not os.path.isdir(os.path.join(work, "image")):
    print("\nimage side not run")
    sys.exit(0)
print("\ncases failing in the image: %d" % len(failed))
for tier, cid, msg in failed:
    print("  [%s] %s: %s" % (tier, cid, msg))
sys.exit(1 if failed else 0)
PY

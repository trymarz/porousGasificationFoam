#!/bin/bash
#
# porousGasificationFoam build engine, run by `pgf build` (and CI) under
# `pgf shell --bare`. Users call pgf build, not this script.
#
# wmake runs per component: lnInclude/ and Make/$WM_OPTIONS/ land beside their
# sources (an in-tree cache), binaries in a prefix. --prefix DIR / --isolated
# point $FOAM_USER_LIBBIN / $FOAM_USER_APPBIN at DIR/{bin,lib} before wmake
# runs; one of them is required. --isolated derives DIR from the build key
# <fingerprint12>-<yade0|yade1>-<OpenFOAM version>-gcc<version> under
# $PGF_BUILD_ROOT; a prefix holding .pgf-build.json is complete and is reused
# without compiling. Make/options link with -L$(FOAM_USER_LIBBIN), so nothing
# else needs to change, and v2406 embeds no RPATH, so a prefix can be moved.

PGF_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
. "$PGF_ROOT/utilities/bash_utils/helpers.sh"
. "$PGF_ROOT/utilities/pgf/coupling.sh"

# ============================================================
# CONFIGURATION
# ============================================================

declare -a LIBRARY_TARGETS=(DEM fieldPorosityModel radiationModels thermophysicalModels pyrolysisModels)
declare -a APP_TARGETS=(porousGasificationFoam)
declare -a ALL_TARGETS=("${LIBRARY_TARGETS[@]}" "${APP_TARGETS[@]}")
declare -a ALL_TARGETS_FLAGS=("${ALL_TARGETS[@]/#/--}")
declare -a ALL_TARGETS_NO_FLAGS=("${ALL_TARGETS[@]/#/--no-}")

declare -A BUILD_TARGETS=( # default values
  [DEM]=0 # disabled
  [fieldPorosityModel]=1
  [radiationModels]=1
  [thermophysicalModels]=1
  [pyrolysisModels]=1
  [porousGasificationFoam]=1
)

# Set to 1 via --yade to compile the solver with Yade/DEM coupling support
WITH_YADE=0

# Targets whose Make/options carries an ifeq ($(WITH_YADE),1) block, so their
# compiled ABI differs between the two variants: the solver, and the pyrolysis
# library (volPyrolysis holds a WITH_YADE-only LambdaDotCalculationModel).
# They get WITH_YADE=1 on the wmake line and each carry a build-variant stamp.
declare -a YADE_SENSITIVE_TARGETS=(pyrolysisModels porousGasificationFoam)

yade_sensitive_target() {
  local t
  for t in "${YADE_SENSITIVE_TARGETS[@]}"; do
    [ "$t" = "$1" ] && return 0
  done
  return 1
}

# Built in order: LIBRARY_TARGETS then APP_TARGETS. DEM first, so a --yade
# build has liblambdaDotModel before the solver links.
declare -A TARGET_DIRS=(
  [DEM]="$PGF_ROOT/porousGasificationMedia/DEM"
  [fieldPorosityModel]="$PGF_ROOT/porousGasificationMedia/fieldPorosityModel"
  [radiationModels]="$PGF_ROOT/porousGasificationMedia/radiationModels"
  [thermophysicalModels]="$PGF_ROOT/porousGasificationMedia/thermophysicalModels"
  [pyrolysisModels]="$PGF_ROOT/porousGasificationMedia/pyrolysisModels"
  [porousGasificationFoam]="$PGF_ROOT/porousGasificationFoam"
)

declare -A BUILD_COMMANDS=(
  [DEM]="wmake -j libso"
  [fieldPorosityModel]="wmake -j libso"
  [radiationModels]="wmake -j libso"
  [thermophysicalModels]="./Allwmake"
  [pyrolysisModels]="wmake -j libso"
  [porousGasificationFoam]="wmake -j"
)

declare -A CLEAN_COMMANDS=(
  [DEM]="wclean libso"
  [fieldPorosityModel]="wclean libso"
  [radiationModels]="wclean libso"
  [thermophysicalModels]="./Allwclean"
  [pyrolysisModels]="wclean libso"
  [porousGasificationFoam]="wclean"
)

MODE="build"

# none | prefix | isolated. PREFIX is the resolved output directory.
PREFIX_MODE="none"
PREFIX=""
# Set by --print-key / --print-prefix: print and exit, build nothing.
QUERY=""
# Arguments as given, recorded in .pgf-build.json.
BUILD_ARGS=()
VERBOSE=0
BUILD_LOG=""

# ============================================================
# ARGUMENT PARSING
# ============================================================

set_targets() {
  local -n targets=$1
  for t in "${targets[@]}"; do
    BUILD_TARGETS[$t]=$2
  done
}

parse_arguments() {
  while [ $# -gt 0 ]; do
    case "$1" in
    clean) MODE="clean" ;;
    build) MODE="build" ;;
    # Exclusive flags
    --reset-all)
      set_targets ALL_TARGETS 0
      ;;
    --all)
      set_targets ALL_TARGETS 1
      ;;
    --libs-only)
      set_targets LIBRARY_TARGETS 1
      set_targets APP_TARGETS 0
      ;;
    --apps-only)
      set_targets LIBRARY_TARGETS 0
      set_targets APP_TARGETS 1
      ;;
    # Selective flags
    --DEM | --fieldPorosityModel | --radiationModels | --thermophysicalModels | --pyrolysisModels | --porousGasificationFoam)
      local t="${1#--}"
      BUILD_TARGETS[$t]=1
      ;;
    --no-DEM | --no-fieldPorosityModel | --no-radiationModels | --no-thermophysicalModels | --no-pyrolysisModels | --no-porousGasificationFoam)
      local t="${1#--no-}"
      BUILD_TARGETS[$t]=0
      ;;
    --yade)
      WITH_YADE=1
      BUILD_TARGETS[DEM]=1
      ;;
    --dry-run)
      DRY_RUN=1
      ;;
    --verbose | -v)
      VERBOSE=1
      ;;
    --fingerprint)
      source_fingerprint
      exit 0
      ;;
    --prefix)
      [ -n "${2:-}" ] || {
        clog ERROR "--prefix needs a directory"
        exit 1
      }
      PREFIX_MODE="prefix"
      PREFIX="$2"
      shift
      ;;
    --isolated)
      PREFIX_MODE="isolated"
      ;;
    --print-key | --print-prefix)
      QUERY="${1#--print-}"
      ;;
    --help)
      echo "Usage: $0 [build|clean] [OPTIONS]"
      echo "Options: --reset-all, --all, --libs-only, --apps-only, --yade"
      echo "Targets: ${ALL_TARGETS_FLAGS[*]} ${ALL_TARGETS_NO_FLAGS[*]}"
      echo ""
      echo "  --yade   Build the DEM library and solver with WITH_YADE=1"
      echo "           (required for Yade-coupled DEM simulations)"
      echo "  --fingerprint  Print the source content hash a build stamps into"
      echo "           <prefix>/bin, and exit. The regression runner"
      echo "           compares it to refuse a solver built from other sources."
      echo "  --prefix DIR   Install into DIR/bin and DIR/lib."
      echo "  --isolated     Install into \$PGF_BUILD_ROOT/<build key>; reuse it"
      echo "           without compiling when it is already complete."
      echo "           PGF_BUILD_ROOT defaults to \$WM_PROJECT_USER_DIR/pgf-builds."
      echo "  --verbose, -v  Stream wmake's output. Without it, a prefix build"
      echo "           shows one line per target and logs wmake to DIR/build.log."
      echo "  --print-key, --print-prefix  Print the build key or the --isolated"
      echo "           prefix for the checkout and variant (honours --yade), and exit."
      echo ""
      echo "pgf build runs this with --isolated. Objects are cached in the checkout"
      echo "$PGF_ROOT; switching --yade on or off cleans the affected targets' objects."
      echo "clean removes that in-tree cache (wclean) and nothing installed."
      exit 0
      ;;
    *)
      clog ERROR "Unknown option '$1'"
      exit 1
      ;;
    esac
    shift
  done
}

# ============================================================
# ENVIRONMENT VALIDATION
# ============================================================

# Every output path comes from the OpenFOAM environment; there are no
# fallbacks, so an unset variable is fatal.
check_foam_environment() {
  local missing=()
  local var
  for var in WM_PROJECT_DIR WM_OPTIONS WM_PROJECT_VERSION; do
    [ -n "${!var}" ] || missing+=("$var")
  done

  if [ ${#missing[@]} -gt 0 ]; then
    clog ERROR "OpenFOAM environment is not set: ${missing[*]} unset."
    clog ERROR "Source your OpenFOAM etc/bashrc first."
    return 1
  fi

  if ! command -v wmake >/dev/null 2>&1; then
    clog ERROR "'wmake' is not on PATH; the OpenFOAM environment is incomplete."
    return 1
  fi

  return 0
}

# --yade / --DEM only; the normal build never requires Foam-Yade.
check_yade_environment() {
  YADE_TRUNK="${YADE_TRUNK:-$PGF_ROOT/submodules/Foam-Yade}"
  if [ -z "$YADE_TRUNK" ]; then
    clog ERROR "YADE_TRUNK is not set, but a DEM/Yade build was requested."
    clog ERROR "Point it at the Foam-Yade source checkout, e.g."
    clog ERROR "  export YADE_TRUNK=/path/to/foam-yade"
    return 1
  fi

  local coupling="$YADE_TRUNK/pkg/openfoam/coupling/FoamYade"
  local missing=()
  if coupling_in_prefix; then
    [ -f "$coupling/FoamYade.H" ] || {
      clog ERROR "No Foam-Yade coupling sources under YADE_TRUNK=$YADE_TRUNK (missing $coupling/FoamYade.H)."
      clog ERROR "Run: git submodule update --init submodules/Foam-Yade, or set YADE_TRUNK."
      return 1
    }
    return 0
  fi
  [ -d "$coupling/lnInclude" ] || missing+=("$coupling/lnInclude")
  [ -d "$coupling/meshtree/lnInclude" ] || missing+=("$coupling/meshtree/lnInclude")

  if [ ${#missing[@]} -gt 0 ]; then
    clog ERROR "Foam-Yade coupling headers not found under YADE_TRUNK=$YADE_TRUNK:"
    local m
    for m in "${missing[@]}"; do clog ERROR "  missing: $m"; done
    clog ERROR "Build Foam-Yade (and its OpenFOAM coupling libraries) first."
    return 1
  fi

  check_coupling_stamp
}

# The coupling libs must come from the headers wmake is about to compile
# against (#257); a mismatch is refused before anything is built.
check_coupling_stamp() {
  local libdir="$PGF_YADE_LIBDIR" line rc=0
  coupling_in_prefix && return 0
  while IFS= read -r line; do
    case "$line" in
    OK*) clog INFO "${line#OK   }" ;;
    WARN*) clog WARNING "${line#WARN }" ;;
    FAIL*) clog ERROR "${line#FAIL }"; rc=1 ;;
    esac
  done < <(pgf_coupling_check "$libdir" "$YADE_TRUNK")
  return "$rc"
}

# A prefix compiles its own coupling libs into <prefix>/lib (#264), unless
# PGF_YADE_LIBDIR names prebuilt ones to copy instead.
coupling_in_prefix() {
  [ -z "${PGF_YADE_LIBDIR:-}" ]
}

# Compile the coupling from a copy of the trunk's sources in the prefix, so
# the trunk stays untouched; later wmake calls take headers from that copy.
build_coupling_in_prefix() {
  local src="$YADE_TRUNK/pkg/openfoam/coupling/FoamYade"
  local shadow="$PREFIX/foam-yade" dst="$PREFIX/foam-yade/pkg/openfoam/coupling/FoamYade" d
  rm -rf "$shadow" && mkdir -p "$dst" || return 1
  (cd "$src" && tar --exclude=lnInclude --exclude="Make/linux*" --exclude="Make/darwin*" -cf - .) |
    (cd "$dst" && tar -xf -) || { clog ERROR "Cannot copy the coupling sources to $dst"; return 1; }
  # libYadeFoam links libMeshTree, so meshtree goes first.
  for d in meshtree commYade .; do
    clog INFO "Building the Foam-Yade coupling: ${d/#./FoamYade}"
    if [ -n "$BUILD_LOG" ]; then
      (cd "$dst/$d" && wmake libso) >>"$BUILD_LOG" 2>&1
    else
      (cd "$dst/$d" && wmake libso)
    fi || { clog ERROR "Coupling build failed in $dst/$d${BUILD_LOG:+; log: $BUILD_LOG}"; return 1; }
  done
  pgf_coupling_write_stamp "$FOAM_USER_LIBBIN" "$YADE_TRUNK" || return 1
  YADE_SOURCE_TRUNK="$YADE_TRUNK"
  export YADE_TRUNK="$shadow"
  clog INFO "Coupling libs built into $FOAM_USER_LIBBIN from $YADE_SOURCE_TRUNK"
}

# Link-time deps of the DEM library and the WITH_YADE solver; libYadeComm
# is a runtime dep of libYadeFoam. A prefix starts empty, so it is built or seeded.
check_yade_libs() {
  if coupling_in_prefix; then
    build_coupling_in_prefix || return 1
  else
    seed_yade_libs || return 1
  fi
  local lib missing=()
  for lib in "${YADE_LIBS[@]}"; do
    [ -e "$FOAM_USER_LIBBIN/$lib.so" ] || missing+=("$lib.so")
  done
  if [ ${#missing[@]} -gt 0 ]; then
    clog ERROR "Foam-Yade libraries not found in $FOAM_USER_LIBBIN: ${missing[*]}"
    clog ERROR "Build the Foam-Yade coupling libraries (wmake libso in"
    clog ERROR "\$YADE_TRUNK/pkg/openfoam/coupling/FoamYade) first."
    return 1
  fi

  return 0
}

declare -a YADE_LIBS=(libMeshTree libYadeFoam libYadeComm)

# With PGF_YADE_LIBDIR set, prebuilt coupling libs are copied from there.
# Copies, not links: a later rebuild there must not change the prefix.
seed_yade_libs() {
  local src="$PGF_YADE_LIBDIR" lib
  for lib in "${YADE_LIBS[@]}"; do
    if [ ! -e "$src/$lib.so" ]; then
      clog ERROR "$lib.so not found in $src."
      clog ERROR "Set PGF_YADE_LIBDIR to the directory holding the Foam-Yade coupling libs."
      return 1
    fi
    cp -fL "$src/$lib.so" "$FOAM_USER_LIBBIN/$lib.so" || return 1
  done
  rm -f "$FOAM_USER_LIBBIN/$PGF_COUPLING_STAMP"
  [ ! -f "$src/$PGF_COUPLING_STAMP" ] || cp -f "$src/$PGF_COUPLING_STAMP" "$FOAM_USER_LIBBIN/" || return 1
  clog INFO "Seeded ${YADE_LIBS[*]} from $src"
}

# ============================================================
# BUILD PREFIX
# ============================================================

# The fingerprint covers the sources only, so the key adds what else changes
# the binaries: the WITH_YADE variant, the OpenFOAM version and the compiler.
# yade1 also folds in the Foam-Yade pin and coupling-header hash (#257).
build_key() {
  local fp cc_version
  fp="$(source_fingerprint)" || return 1
  if [ "$WITH_YADE" -eq 1 ]; then
    local trunk="${YADE_TRUNK:-$PGF_ROOT/submodules/Foam-Yade}"
    fp="$(printf '%s\n' "$fp" "$(pgf_coupling_pin)" "$(pgf_coupling_api_hash "$trunk")" | sha1sum | cut -c1-40)"
  fi
  cc_version="$(gcc -dumpversion 2>/dev/null)" || {
    clog ERROR "gcc -dumpversion failed; cannot form the build key."
    return 1
  }
  printf '%s-yade%s-%s-gcc%s\n' "${fp:0:12}" "$WITH_YADE" "$WM_PROJECT_VERSION" "$cc_version"
}

# PGF_DEFAULT_USER_DIR is what OpenFOAM's bashrc chose; pgf exports it
# because it overrides WM_PROJECT_USER_DIR afterwards.
resolve_build_root() {
  local user_dir="${PGF_DEFAULT_USER_DIR:-${WM_PROJECT_USER_DIR:-}}"
  PGF_BUILD_ROOT="${PGF_BUILD_ROOT:-${user_dir:+$user_dir/pgf-builds}}"
  [ -n "$PGF_BUILD_ROOT" ] || {
    clog ERROR "PGF_BUILD_ROOT is unset and there is no WM_PROJECT_USER_DIR to default it from."
    return 1
  }
}

resolve_prefix() {
  case "$PREFIX_MODE" in
  prefix)
    mkdir -p "$PREFIX" && PREFIX="$(cd "$PREFIX" && pwd -P)" || {
      clog ERROR "Cannot create prefix $PREFIX"
      return 1
    }
    ;;
  isolated)
    [ -n "${WM_PROJECT_VERSION:-}" ] || {
      clog ERROR "WM_PROJECT_VERSION is unset; source OpenFOAM's etc/bashrc (or use pgf shell --bare)."
      return 1
    }
    resolve_build_root || return 1
    BUILD_KEY="$(build_key)" || return 1
    PREFIX="$PGF_BUILD_ROOT/$BUILD_KEY"
    ;;
  esac
}

prefix_complete() {
  [ -f "$PREFIX/.pgf-build.json" ]
}

# A complete prefix built the requested targets if its metadata lists them.
prefix_has_targets() {
  local t wanted=()
  for t in "${ALL_TARGETS[@]}"; do
    [ "${BUILD_TARGETS[$t]:-0}" -eq 1 ] && wanted+=("$t")
  done
  python3 -c 'import json, sys
built = set(json.load(open(sys.argv[1])).get("targets", []))
sys.exit(0 if set(sys.argv[2:]) <= built else 1)' "$PREFIX/.pgf-build.json" "${wanted[@]}" 2>/dev/null
}

# report_cache_hit — what is reused (key, variant, parts), when and where it was
#   built, and why: the key hashes the sources, OpenFOAM version and compiler.
report_cache_hit() {
  local what built
  { read -r what; read -r built; } < <(python3 - "$PREFIX/.pgf-build.json" "$HOME" "$PGF_ROOT" 2>/dev/null <<'EOF'
import json, os, sys
m = json.load(open(sys.argv[1]))
parts = {"porousGasificationFoam": "solver"}
names = []
for t in m.get("targets", []):
    n = parts.get(t, "libraries")
    if n not in names:
        names.append(n)
src = (m.get("checkout") or {}).get("path") or m.get("source_tree") or "?"
other = os.path.realpath(src) != os.path.realpath(sys.argv[3])
gone = other and not os.path.isdir(src)
if src.startswith(sys.argv[2] + "/"):
    src = "~" + src[len(sys.argv[2]):]
when = (m.get("built_utc") or "?").replace("T", " ").replace("Z", " UTC")
kind = {"yade0": "non-DEM", "yade1": "DEM"}.get(m.get("variant"), m.get("variant", "?"))
print("%s: %s" % (kind, ", ".join(names) or "?"))
if gone:
    print("built %s in %s, a checkout since removed, from the same sources" % (when, src))
else:
    print("built %s in %s%s" % (when, src, ", from the same sources" if other else ""))
EOF
  )
  st OK "reused build ${BUILD_KEY%%-*} (${what:-?}): same sources, OpenFOAM and compiler, nothing to compile"
  st_more "${built:-built ?}; prefix $(pgf_tilde "$PREFIX")"
  register_workspace
}

# Point wmake's output at the prefix, under a lock so two builds of the same
# key cannot interleave.
activate_prefix() {
  if [ "$MODE" = "build" ] && [ "$PREFIX_MODE" = "isolated" ] &&
    prefix_complete && prefix_has_targets; then
    report_cache_hit
    exit 0
  fi

  local lock="$PREFIX/.pgf-build.lock"
  [ "$PREFIX_MODE" = "isolated" ] && lock="$PGF_BUILD_ROOT/$BUILD_KEY.lock"
  mkdir -p "${lock%/*}" || return 1
  exec {BUILD_LOCK_FD}>"$lock" || return 1
  if ! flock -n "$BUILD_LOCK_FD"; then
    clog INFO "Waiting for another build holding $lock ..."
    flock "$BUILD_LOCK_FD" || return 1
  fi

  if [ "$MODE" = "build" ] && [ "$PREFIX_MODE" = "isolated" ] &&
    prefix_complete && prefix_has_targets; then
    report_cache_hit
    exit 0
  fi

  mkdir -p "$PREFIX/bin" "$PREFIX/lib" || return 1
  export FOAM_USER_APPBIN="$PREFIX/bin"
  export FOAM_USER_LIBBIN="$PREFIX/lib"
  export LD_LIBRARY_PATH="$FOAM_USER_LIBBIN${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
}

# Removed after the prerequisite checks but before anything in the prefix
# changes: a refused build keeps a complete prefix, and an interrupted one
# leaves an incomplete prefix that is never reused.
invalidate_prefix() {
  rm -f "$PREFIX/.pgf-build.json"
}

# Written last: its presence is what marks the prefix complete.
write_build_metadata() {
  local targets=() t
  for t in "${ALL_TARGETS[@]}"; do
    [ "${BUILD_TARGETS[$t]:-0}" -eq 1 ] && targets+=("$t")
  done
  local trunk="${YADE_SOURCE_TRUNK:-${YADE_TRUNK:-$PGF_ROOT/submodules/Foam-Yade}}" dirty=""
  if git -C "$PGF_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    dirty=0
    [ -z "$(git -C "$PGF_ROOT" status --porcelain --untracked-files=no 2>/dev/null)" ] || dirty=1
  fi
  PGF_M_BRANCH="$(git -C "$PGF_ROOT" symbolic-ref -q --short HEAD 2>/dev/null)" PGF_M_DIRTY="$dirty" \
    PGF_M_PIN="$(pgf_coupling_pin)" PGF_M_API="$(pgf_coupling_api_hash "$trunk")" \
    PGF_M_TRUNK="$trunk" PGF_M_TRUNK_HEAD="$(pgf_coupling_trunk_head "$trunk")" \
  python3 - "$PREFIX/.pgf-build.json" "$(source_fingerprint)" "$WITH_YADE" \
    "$WM_PROJECT_VERSION" "$WM_OPTIONS" "$(gcc -dumpversion)" \
    "$(git -C "$PGF_ROOT" rev-parse HEAD 2>/dev/null)" "$PGF_ROOT" \
    "${BUILD_KEY:-}" "${#targets[@]}" "${targets[@]}" "${BUILD_ARGS[@]}" <<'PY' || return 1
import datetime, json, os, sys
path, fp, yade, foam, opts, cc, head, root, key, n = sys.argv[1:11]
n = int(n)
env = os.environ.get
meta = {
    "schema": 2,
    "key": key or None, "fingerprint": fp, "variant": "yade" + yade,
    "wm_project_version": foam, "wm_options": opts, "compiler": "gcc " + cc,
    "built_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "targets": sys.argv[11:11 + n], "build_args": sys.argv[11 + n:],
    "git_head": head or None, "source_tree": root,
    "checkout": {"path": root, "worktree": os.path.basename(root), "branch": env("PGF_M_BRANCH") or None,
                 "dirty": env("PGF_M_DIRTY") == "1" if env("PGF_M_DIRTY") else None},
}
if yade == "1":
    meta["foam_yade"] = {"pin": env("PGF_M_PIN") or None, "api_hash": env("PGF_M_API") or None,
                         "trunk": env("PGF_M_TRUNK") or None, "trunk_head": env("PGF_M_TRUNK_HEAD") or None}
with open(path + ".tmp", "w") as f:
    json.dump(meta, f, indent=2)
    f.write("\n")
os.replace(path + ".tmp", path)
PY
  clog INFO "Wrote $PREFIX/.pgf-build.json"
  register_workspace
}

# Record this checkout's latest build per variant (pgf ls, pgf use WORKTREE).
register_workspace() {
  [ -z "${BUILD_KEY:-}" ] || python3 "$PGF_ROOT/utilities/pgf/registry.py" register "$PGF_BUILD_ROOT" \
    "$PGF_ROOT" "yade$WITH_YADE" "$BUILD_KEY" "$(git -C "$PGF_ROOT" symbolic-ref -q --short HEAD 2>/dev/null)" ||
    clog WARN "Could not register $PGF_ROOT in $PGF_BUILD_ROOT/.pgf/workspaces"
}

# Builds go into a prefix only (#288); the shared user dir is never written.
require_prefix() {
  [ "$PREFIX_MODE" = "none" ] || return 0
  clog ERROR "build.sh is pgf's build engine and needs --isolated or --prefix DIR; run: pgf build${1:+ $1}"
  return 1
}

print_destination() {
  clog INFO "Destination (prefix ${BUILD_KEY:-$PREFIX}):"
  clog INFO "  bin: $(pgf_tilde "$FOAM_USER_APPBIN")"
  clog INFO "  lib: $(pgf_tilde "$FOAM_USER_LIBBIN")"
}

# ============================================================
# BUILD VARIANT BOOKKEEPING
# ============================================================

# wmake recompiles on timestamps and cannot see WITH_YADE change, so building
# the other mode over existing objects would mix both ABIs. The mode is stamped
# in each YADE-sensitive target's Make/$WM_OPTIONS/; a mismatch drops that dir.
variant_stamp_path() {
  printf '%s/Make/%s/.pgf-build-mode' "${TARGET_DIRS[$1]}" "$WM_OPTIONS"
}

check_build_variant() {
  [ "$MODE" = "build" ] || return 0

  local target stamp requested recorded cleaned=()
  requested="WITH_YADE=$WITH_YADE"

  for target in "${YADE_SENSITIVE_TARGETS[@]}"; do
    [ "${BUILD_TARGETS[$target]:-0}" -eq 1 ] || continue

    stamp="$(variant_stamp_path "$target")"

    if [ -f "$stamp" ]; then
      recorded="$(cat "$stamp")"
      if [ "$recorded" != "$requested" ]; then
        rm -rf "${stamp%/*}" || return 1
        cleaned+=("$target")
      fi
    fi

    mkdir -p "${stamp%/*}" && printf '%s\n' "$requested" >"$stamp"
  done
  [ ${#cleaned[@]} -eq 0 ] || clog INFO "switching to the $(variant_name "$WITH_YADE") variant:" \
    "cleaned the $(variant_name $((1 - WITH_YADE))) objects of ${cleaned[*]}; they recompile now"
}

variant_name() {
  [ "$1" -eq 1 ] && echo DEM || echo normal
}

# ============================================================
# BUILD OPERATIONS
# ============================================================

execute_target() {
  local target=$1
  local dir="${TARGET_DIRS[$target]}"
  local cmd

  [ -d "$dir" ] || {
    clog ERROR "Directory not found: $dir"
    return 1
  }

  if [ "$MODE" = "build" ]; then
    cmd="${BUILD_COMMANDS[$target]}"
    if yade_sensitive_target "$target" && [ "$WITH_YADE" -eq 1 ]; then
      cmd="WITH_YADE=1 ${cmd}"
      progress "${2:-}Building $target (WITH_YADE=1)..."
    else
      progress "${2:-}Building $target..."
    fi
  else
    cmd="${CLEAN_COMMANDS[$target]}"
    clog INFO "${2:-}Cleaning $target..."
  fi

  # Run from the target's own directory, so Make/options paths are relative.
  if [ -n "$BUILD_LOG" ]; then
    local from
    from=$(($(wc -l <"$BUILD_LOG") + 1))
    echo "== $target: $cmd" >>"$BUILD_LOG"
    if ! (cd "$dir" && eval "$cmd") >>"$BUILD_LOG" 2>&1; then
      progress_done
      tail -n "+$from" "$BUILD_LOG" | tail -n 30 | sed 's/^/    /'
      clog ERROR "full log: $BUILD_LOG"
      return 1
    fi
  else
    (cd "$dir" && eval "$cmd") || return 1
  fi

  return 0
}

build_all_targets() {
  local failed=() target n=0 total=0 t0 start=$SECONDS

  for target in "${LIBRARY_TARGETS[@]}" "${APP_TARGETS[@]}"; do
    [ "${BUILD_TARGETS[$target]:-0}" -eq 1 ] && total=$((total + 1))
  done
  for target in "${LIBRARY_TARGETS[@]}" "${APP_TARGETS[@]}"; do
    if [ "${BUILD_TARGETS[$target]:-0}" -eq 1 ]; then
      n=$((n + 1)) t0=$SECONDS
      if execute_target "$target" "[$n/$total] "; then
        progress_done
        clog SUCCESS "[$n/$total] $target done ($(fmt_seconds $((SECONDS - t0))))"
      else
        clog ERROR "Failed on $target"
        failed+=("$target")
      fi
    fi
  done

  if [ ${#failed[@]} -eq 0 ]; then
    [ "$MODE" = "build" ] && stamp_source_revision
    clog SUCCESS "$MODE complete in $(fmt_seconds $((SECONDS - start)))${BUILD_LOG:+; wmake log: $(pgf_tilde "$BUILD_LOG")}"
    return 0
  else
    clog ERROR "Failed: ${failed[*]}"
    return 1
  fi
}

# progress MSG — with a build log, a line that the next one overwrites on a
#   terminal (nothing otherwise); without one, an INFO line as before.
progress() {
  if [ -z "$BUILD_LOG" ]; then clog INFO "$1"
  elif [ -t 1 ]; then printf '%s' "$1"; PROGRESS_OPEN=1
  fi
}
progress_done() {
  [ "${PROGRESS_OPEN:-0}" -eq 1 ] && printf '\r\033[K'
  PROGRESS_OPEN=0
}

# fmt_seconds S — "42s" or "1m32s".
fmt_seconds() {
  if [ "$1" -ge 60 ]; then printf '%dm%02ds' $(($1 / 60)) $(($1 % 60)); else printf '%ds' "$1"; fi
}

# Content hash of every source file the targets compile. Not a git revision:
# the binary has to match the tree it was built from, committed or not, and a
# plain commit must not invalidate a build whose sources never changed.
# Paths are hashed repo-relative, so every checkout of the same sources gets
# the same key; Make/<WM_OPTIONS>/ is pruned (wmake generates `options` there).
source_fingerprint() {
  local dir
  for dir in "${TARGET_DIRS[@]}"; do
    [ -d "$dir" ] || continue
    (cd "$PGF_ROOT" && find "${dir#"$PGF_ROOT"/}" -path '*/Make/*/*' -prune -o \
      \( -name '*.C' -o -name '*.H' -o -name 'files' -o -name 'options' \) \
      -type f -print0)
  done | LC_ALL=C sort -z | (cd "$PGF_ROOT" && xargs -0 sha1sum 2>/dev/null) | sha1sum | cut -d' ' -f1
}

# Record which sources produced the prefix's binaries; runCase.sh refuses to
# run when this stamp does not match the suite's own tree.
stamp_source_revision() {
  local fp
  [ -n "${FOAM_USER_APPBIN:-}" ] || return 0
  fp="$(source_fingerprint)" || return 0
  mkdir -p "$FOAM_USER_APPBIN" || return 0
  printf '%s\n' "$fp" >"$FOAM_USER_APPBIN/.pgf-source-fingerprint"
  clog INFO "Stamped $FOAM_USER_APPBIN with source fingerprint ${fp:0:10}"
}

dry_run() {
  echo "════════════════════════════════════════════════════════════"
  echo "DRY RUN - MODE: $MODE (build|clean)"
  echo "════════════════════════════════════════════════════════════"
  echo ""

  echo "PATHS:"
  echo "────────────────────────────────────────────────────────────"
  printf "  checkout:          %s\n" "$PGF_ROOT"
  [ "$PREFIX_MODE" = "none" ] || printf "  prefix:            %s\n" "${PREFIX:-<unresolved>}"
  printf "  FOAM_USER_LIBBIN:  %s\n" "${FOAM_USER_LIBBIN:-<unset>}"
  printf "  FOAM_USER_APPBIN:  %s\n" "${FOAM_USER_APPBIN:-<unset>}"
  echo ""

  echo "OPTIONS:"
  echo "────────────────────────────────────────────────────────────"
  printf "  WITH_YADE=%s\n" "$WITH_YADE"
  echo ""

  echo "BUILD TARGETS STATUS:"
  echo "────────────────────────────────────────────────────────────"
  for t in "${ALL_TARGETS[@]}"; do
    local status="${BUILD_TARGETS[$t]:-0}"
    local symbol="✗"
    if [ "$status" -eq 1 ]; then
      symbol="✓"
    fi
    printf "  %s %-30s [%s]\n" "$symbol" "$t" "$status"
  done
  echo ""

  echo "TARGETS TO EXECUTE:"
  echo "────────────────────────────────────────────────────────────"
  local will_execute=0
  for target in "${LIBRARY_TARGETS[@]}" "${APP_TARGETS[@]}"; do
    if [ "${BUILD_TARGETS[$target]:-0}" -eq 1 ]; then
      will_execute=1
      local dir="${TARGET_DIRS[$target]}"
      local cmd
      if [ "$MODE" = "build" ]; then
        cmd="${BUILD_COMMANDS[$target]}"
        if yade_sensitive_target "$target" && [ "$WITH_YADE" -eq 1 ]; then
          cmd="WITH_YADE=1 ${cmd}"
        fi
      else
        cmd="${CLEAN_COMMANDS[$target]}"
      fi
      echo "  • $target"
      echo "    Directory: $dir"
      echo "    Command:   $cmd"
      echo ""
    fi
  done

  if [ $will_execute -eq 0 ]; then
    echo "  (No targets selected for execution)"
    echo ""
  fi

  echo "════════════════════════════════════════════════════════════"
  echo "Run without --dry-run to execute"
  echo "════════════════════════════════════════════════════════════"
}

# ============================================================
# MAIN & COMPLETION
# ============================================================

main() {
  DRY_RUN=0
  BUILD_ARGS=("$@")
  parse_arguments "$@"

  if [ -n "$QUERY" ]; then
    PREFIX_MODE="isolated"
    resolve_prefix || exit 1
    [ "$QUERY" = "key" ] && printf '%s\n' "$BUILD_KEY" || printf '%s\n' "$PREFIX"
    exit 0
  fi

  [ "$DRY_RUN" -eq 1 ] && {
    [ "$PREFIX_MODE" = "isolated" ] && { resolve_prefix || PREFIX=""; }
    dry_run
    exit 0
  }

  if [ "$MODE" = "build" ]; then
    require_prefix || exit 1
    resolve_prefix || exit 1
    activate_prefix || exit 1
  else
    require_prefix "clean" || exit 1
  fi

  check_foam_environment || exit 1
  [ "$MODE" = "clean" ] || print_destination

  local yade=0
  if [ "$MODE" = "build" ] &&
    { [ "$WITH_YADE" -eq 1 ] || [ "${BUILD_TARGETS[DEM]:-0}" -eq 1 ]; }; then
    yade=1
    check_yade_environment || exit 1
  fi

  check_build_variant || exit 1

  [ "$MODE" = "clean" ] || invalidate_prefix
  # A prefix build logs wmake to the prefix and shows one line per target.
  if [ "$MODE" = build ] && [ "$VERBOSE" -eq 0 ]; then
    BUILD_LOG="$PREFIX/build.log"
    : >"$BUILD_LOG" || exit 1
  fi
  [ "$yade" -eq 0 ] || check_yade_libs || exit 1

  build_all_targets || exit 1

  if [ "$MODE" = "build" ]; then
    write_build_metadata || exit 1
    printf 'prefix: %s\n' "$(pgf_tilde "$PREFIX")"
  fi
}

[ "${BASH_SOURCE[0]}" = "${0}" ] && main "$@"

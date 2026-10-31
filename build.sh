#!/bin/bash
#
# porousGasificationFoam build driver.
#
# wmake runs in place, per component: lnInclude/ and Make/$WM_OPTIONS/ land
# beside their sources, binaries in $FOAM_USER_LIBBIN / $FOAM_USER_APPBIN as
# each Make/files declares. Callable by absolute path from anywhere.
#
# --prefix DIR / --isolated redirect those two variables to DIR/{bin,lib}
# before wmake runs. --isolated derives DIR from the build key
# <fingerprint12>-<yade0|yade1>-<OpenFOAM version>-gcc<version> under
# $PGF_BUILD_ROOT; a prefix holding .pgf-build.json is complete and is reused
# without compiling. Make/options link with -L$(FOAM_USER_LIBBIN), so nothing
# else needs to change, and v2406 embeds no RPATH, so a prefix can be moved.

. "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/utilities/bash_utils/helpers.sh"

PGF_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

# ============================================================
# CONFIGURATION
# ============================================================

declare -a LIBRARY_TARGETS=(DEM fieldPorosityModel radiationModels thermophysicalModels pyrolysisModels)
declare -a APP_TARGETS=(porousGasificationFoam utilities)
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
  [utilities]=1
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

# --purge: on clean, also delete installed binaries. Off by default.
PURGE=0

# Built in order: LIBRARY_TARGETS then APP_TARGETS. DEM first, so a --yade
# build has liblambdaDotModel before the solver links.
declare -A TARGET_DIRS=(
  [DEM]="$PGF_ROOT/porousGasificationMedia/DEM"
  [fieldPorosityModel]="$PGF_ROOT/porousGasificationMedia/fieldPorosityModel"
  [radiationModels]="$PGF_ROOT/porousGasificationMedia/radiationModels"
  [thermophysicalModels]="$PGF_ROOT/porousGasificationMedia/thermophysicalModels"
  [pyrolysisModels]="$PGF_ROOT/porousGasificationMedia/pyrolysisModels"
  [porousGasificationFoam]="$PGF_ROOT/porousGasificationFoam"
  [utilities]="$PGF_ROOT/utilities"
)

declare -A BUILD_COMMANDS=(
  [DEM]="wmake -j libso"
  [fieldPorosityModel]="wmake -j libso"
  [radiationModels]="wmake -j libso"
  [thermophysicalModels]="./Allwmake"
  [pyrolysisModels]="wmake -j libso"
  [porousGasificationFoam]="wmake -j"
  [utilities]="./Allwmake"
)

declare -A CLEAN_COMMANDS=(
  [DEM]="wclean libso"
  [fieldPorosityModel]="wclean libso"
  [radiationModels]="wclean libso"
  [thermophysicalModels]="./Allwclean"
  [pyrolysisModels]="wclean libso"
  [porousGasificationFoam]="wclean"
  [utilities]="./Allwclean"
)

MODE="build"

# none | prefix | isolated. PREFIX is the resolved output directory.
PREFIX_MODE="none"
PREFIX=""
# Set by --print-key / --print-prefix: print and exit, build nothing.
QUERY=""
# Arguments as given, recorded in .pgf-build.json.
BUILD_ARGS=()

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
    --DEM | --fieldPorosityModel | --radiationModels | --thermophysicalModels | --pyrolysisModels | --porousGasificationFoam | --utilities)
      local t="${1#--}"
      BUILD_TARGETS[$t]=1
      ;;
    --no-DEM | --no-fieldPorosityModel | --no-radiationModels | --no-thermophysicalModels | --no-pyrolysisModels | --no-porousGasificationFoam | --no-utilities)
      local t="${1#--no-}"
      BUILD_TARGETS[$t]=0
      ;;
    --yade)
      WITH_YADE=1
      BUILD_TARGETS[DEM]=1
      ;;
    --purge)
      PURGE=1
      ;;
    --dry-run)
      DRY_RUN=1
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
      echo "Options: --reset-all, --all, --libs-only, --apps-only, --yade, --purge"
      echo "Targets: ${ALL_TARGETS_FLAGS[*]} ${ALL_TARGETS_NO_FLAGS[*]}"
      echo ""
      echo "  --yade   Build the DEM library and solver with WITH_YADE=1"
      echo "           (required for Yade-coupled DEM simulations)"
      echo "  --purge  On clean, also delete the libraries and executables the"
      echo "           cleaned targets declare in their own Make/files. Without"
      echo "           it, clean matches wclean and removes build state only."
      echo "  --fingerprint  Print the source content hash a build stamps into"
      echo "           \$FOAM_USER_APPBIN, and exit. The regression runner"
      echo "           compares it to refuse a solver built from other sources."
      echo "  --prefix DIR   Install into DIR/bin and DIR/lib instead of"
      echo "           \$FOAM_USER_APPBIN / \$FOAM_USER_LIBBIN."
      echo "  --isolated     Install into \$PGF_BUILD_ROOT/<build key>; reuse it"
      echo "           without compiling when it is already complete."
      echo "           PGF_BUILD_ROOT defaults to \$WM_PROJECT_USER_DIR/pgf-builds."
      echo "  --print-key, --print-prefix  Print the build key or the --isolated"
      echo "           prefix for this tree and variant (honours --yade), and exit."
      echo ""
      echo "Builds happen in this checkout ($PGF_ROOT); output goes to"
      echo "\$FOAM_USER_LIBBIN and \$FOAM_USER_APPBIN unless a prefix is given."
      echo "PGF_ISOLATED_ONLY=1 (set by pgf-env) refuses a build without one."
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
  for var in WM_PROJECT_DIR WM_OPTIONS WM_PROJECT_VERSION FOAM_USER_LIBBIN FOAM_USER_APPBIN; do
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
  [ -d "$coupling/lnInclude" ] || missing+=("$coupling/lnInclude")
  [ -d "$coupling/meshtree/lnInclude" ] || missing+=("$coupling/meshtree/lnInclude")

  if [ ${#missing[@]} -gt 0 ]; then
    clog ERROR "Foam-Yade coupling headers not found under YADE_TRUNK=$YADE_TRUNK:"
    local m
    for m in "${missing[@]}"; do clog ERROR "  missing: $m"; done
    clog ERROR "Build Foam-Yade (and its OpenFOAM coupling libraries) first."
    return 1
  fi

  return 0
}

# Link-time deps of the DEM library and the WITH_YADE solver; libYadeComm
# is a runtime dep of libYadeFoam. A prefix starts empty, so it is seeded.
check_yade_libs() {
  [ "$PREFIX_MODE" = "none" ] || seed_yade_libs || return 1
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

# The Foam-Yade coupling libs install into whichever $FOAM_USER_LIBBIN was
# active when they were built, so they are copied from PGF_YADE_LIBDIR or the
# default user lib dir. Copies, not links: a later rebuild there must not
# change a prefix underneath its metadata.
seed_yade_libs() {
  local src="${PGF_YADE_LIBDIR:-$DEFAULT_USER_LIBBIN}" lib
  for lib in "${YADE_LIBS[@]}"; do
    if [ ! -e "$src/$lib.so" ]; then
      clog ERROR "$lib.so not found in ${src:-<no default user lib dir>}."
      clog ERROR "Set PGF_YADE_LIBDIR to the directory holding the Foam-Yade coupling libs."
      return 1
    fi
    cp -fL "$src/$lib.so" "$FOAM_USER_LIBBIN/$lib.so" || return 1
  done
  clog INFO "Seeded ${YADE_LIBS[*]} from $src"
}

# ============================================================
# BUILD PREFIX
# ============================================================

# The fingerprint covers the sources only, so the key adds what else changes
# the binaries: the WITH_YADE variant, the OpenFOAM version and the compiler.
build_key() {
  local fp cc_version
  fp="$(source_fingerprint)" || return 1
  cc_version="$(gcc -dumpversion 2>/dev/null)" || {
    clog ERROR "gcc -dumpversion failed; cannot form the build key."
    return 1
  }
  printf '%s-yade%s-%s-gcc%s\n' "${fp:0:12}" "$WITH_YADE" "$WM_PROJECT_VERSION" "$cc_version"
}

# PGF_DEFAULT_USER_DIR is what OpenFOAM's bashrc chose; pgf-env exports it
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
      clog ERROR "WM_PROJECT_VERSION is unset; source OpenFOAM's etc/bashrc (or use pgf-env --bare)."
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

report_cache_hit() {
  clog SUCCESS "Cache hit: $PREFIX is complete for key $BUILD_KEY; nothing compiled."
  printf '%s\n' "$PREFIX"
}

# Point wmake's output at the prefix, under a lock so two builds of the same
# key cannot interleave.
activate_prefix() {
  DEFAULT_USER_LIBBIN="${PGF_DEFAULT_USER_DIR:+$PGF_DEFAULT_USER_DIR/platforms/$WM_OPTIONS/lib}"
  DEFAULT_USER_LIBBIN="${DEFAULT_USER_LIBBIN:-${FOAM_USER_LIBBIN:-}}"

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
  python3 - "$PREFIX/.pgf-build.json" "$(source_fingerprint)" "$WITH_YADE" \
    "$WM_PROJECT_VERSION" "$WM_OPTIONS" "$(gcc -dumpversion)" \
    "$(git -C "$PGF_ROOT" rev-parse HEAD 2>/dev/null)" "$PGF_ROOT" \
    "${BUILD_KEY:-}" "${#targets[@]}" "${targets[@]}" "${BUILD_ARGS[@]}" <<'PY' || return 1
import datetime, json, os, sys
path, fp, yade, foam, opts, cc, head, root, key, n = sys.argv[1:11]
n = int(n)
meta = {
    "key": key or None, "fingerprint": fp, "variant": "yade" + yade,
    "wm_project_version": foam, "wm_options": opts, "compiler": "gcc " + cc,
    "built_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "targets": sys.argv[11:11 + n], "build_args": sys.argv[11 + n:],
    "git_head": head or None, "source_tree": root,
}
with open(path + ".tmp", "w") as f:
    json.dump(meta, f, indent=2)
    f.write("\n")
os.replace(path + ".tmp", path)
PY
  clog INFO "Wrote $PREFIX/.pgf-build.json"
}

# The shared user dir is what automation must not overwrite by accident.
check_isolated_only() {
  [ "${PGF_ISOLATED_ONLY:-0}" = "1" ] || return 0
  [ "$PREFIX_MODE" = "none" ] || return 0
  [ "$MODE" = "build" ] || [ "$PURGE" -eq 1 ] || return 0
  clog ERROR "PGF_ISOLATED_ONLY=1: refusing to $MODE in the shared \$FOAM_USER_APPBIN / \$FOAM_USER_LIBBIN."
  clog ERROR "Use --isolated or --prefix DIR."
  return 1
}

print_destination() {
  local where="shared user dir"
  [ "$PREFIX_MODE" = "none" ] || where="prefix ${BUILD_KEY:-$PREFIX}"
  clog INFO "Destination ($where):"
  clog INFO "  bin: $FOAM_USER_APPBIN"
  clog INFO "  lib: $FOAM_USER_LIBBIN"
}

# ============================================================
# BUILD VARIANT BOOKKEEPING
# ============================================================

# wmake recompiles on timestamps and cannot see WITH_YADE change, so building
# the other mode over existing objects would mix both ABIs. The mode is stamped
# in each YADE-sensitive target's Make/$WM_OPTIONS/ (removed by `wclean`); a
# mismatch is refused.
variant_stamp_path() {
  printf '%s/Make/%s/.pgf-build-mode' "${TARGET_DIRS[$1]}" "$WM_OPTIONS"
}

check_build_variant() {
  [ "$MODE" = "build" ] || return 0

  local target stamp requested recorded
  requested="WITH_YADE=$WITH_YADE"

  for target in "${YADE_SENSITIVE_TARGETS[@]}"; do
    [ "${BUILD_TARGETS[$target]:-0}" -eq 1 ] || continue

    stamp="$(variant_stamp_path "$target")"

    if [ -f "$stamp" ]; then
      recorded="$(cat "$stamp")"
      if [ "$recorded" != "$requested" ]; then
        clog ERROR "Build variant mismatch for $target."
        clog ERROR "  already built: $recorded"
        clog ERROR "  requested:     $requested"
        clog ERROR "wmake keys recompilation on source timestamps, not on WITH_YADE,"
        clog ERROR "so an in-place rebuild would link objects from both variants."
        clog ERROR "Clean first:  $PGF_ROOT/build.sh clean --all --purge"
        clog ERROR "(--purge also drops the installed binary, so a failed rebuild"
        clog ERROR " cannot leave the other variant's solver on your PATH.)"
        return 1
      fi
    fi

    mkdir -p "${stamp%/*}" && printf '%s\n' "$requested" >"$stamp"
  done
}

# ============================================================
# ARTIFACT REMOVAL
# ============================================================

# Resolve a Make/files EXE/LIB declaration, e.g.
#   $(FOAM_USER_LIBBIN)/libHGSsolid  ->  /…/platforms/…/lib/libHGSsolid
# Only those two variables are substituted; anything else is refused, so no
# path outside the user output directories can be produced.
expand_make_path() {
  local raw="$1"
  raw="${raw//[[:space:]]/}"
  raw="${raw//\$(FOAM_USER_LIBBIN)/$FOAM_USER_LIBBIN}"
  raw="${raw//\$(FOAM_USER_APPBIN)/$FOAM_USER_APPBIN}"
  case "$raw" in
  '' | *'$('*) return 1 ;;
  esac
  printf '%s' "$raw"
}

# --purge only: $FOAM_USER_LIBBIN and $FOAM_USER_APPBIN are shared by every
# project in this $WM_PROJECT_USER_DIR, so only what the target's own
# Make/files declares is removed — never a neighbour's library.
remove_target_artifacts() {
  local dir=$1
  local makefiles decl path

  while IFS= read -r makefiles; do
    while IFS= read -r decl; do
      path="$(expand_make_path "${decl#*=}")" || {
        clog WARNING "  unresolved target path in ${makefiles}: ${decl}"
        continue
      }
      case "$decl" in
      *LIB*) path="$path.so" ;;
      esac
      case "$path" in
      "$FOAM_USER_LIBBIN"/* | "$FOAM_USER_APPBIN"/*) ;;
      *)
        clog WARNING "  refusing to remove $path (outside the user output directories)"
        continue
        ;;
      esac
      [ -e "$path" ] || continue
      rm -f "$path" && clog INFO "  removed $path"
    done < <(grep -E '^[[:space:]]*(EXE|LIB)[[:space:]]*=' "$makefiles")
  done < <(find "$dir" -type f -path '*/Make/files')
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
      clog INFO "Building $target (WITH_YADE=1)..."
    else
      clog INFO "Building $target..."
    fi
  else
    cmd="${CLEAN_COMMANDS[$target]}"
    clog INFO "Cleaning $target..."
  fi

  # Run from the target's own directory, so Make/options paths are relative.
  (cd "$dir" && eval "$cmd") || return 1

  [ "$MODE" = "clean" ] && [ "$PURGE" -eq 1 ] && remove_target_artifacts "$dir"

  return 0
}

build_all_targets() {
  local failed=()

  for target in "${LIBRARY_TARGETS[@]}" "${APP_TARGETS[@]}"; do
    if [ "${BUILD_TARGETS[$target]:-0}" -eq 1 ]; then
      if execute_target "$target"; then
        clog SUCCESS "$target done"
      else
        clog ERROR "Failed on $target"
        failed+=("$target")
      fi
    fi
  done

  if [ ${#failed[@]} -eq 0 ]; then
    [ "$MODE" = "build" ] && stamp_source_revision
    clog SUCCESS "$MODE complete!"
    return 0
  else
    clog ERROR "Failed: ${failed[*]}"
    return 1
  fi
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

# Record which sources produced the installed binaries. $FOAM_USER_APPBIN is
# shared by every worktree of every project, so the solver on $PATH need not be
# the one built from the sources beside it; runCase.sh refuses to run when this
# stamp does not match the suite's own tree.
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
  [ "$MODE" = "clean" ] && printf "  PURGE=%s\n" "$PURGE"
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

  check_isolated_only || exit 1

  if [ "$PREFIX_MODE" != "none" ]; then
    resolve_prefix || exit 1
    activate_prefix || exit 1
  fi

  check_foam_environment || exit 1
  print_destination

  local yade=0
  if [ "$MODE" = "build" ] &&
    { [ "$WITH_YADE" -eq 1 ] || [ "${BUILD_TARGETS[DEM]:-0}" -eq 1 ]; }; then
    yade=1
    check_yade_environment || exit 1
  fi

  check_build_variant || exit 1

  [ "$PREFIX_MODE" = "none" ] || invalidate_prefix
  [ "$yade" -eq 0 ] || check_yade_libs || exit 1

  build_all_targets || exit 1

  if [ "$PREFIX_MODE" != "none" ]; then
    if [ "$MODE" = "build" ]; then
      write_build_metadata || exit 1
      printf '%s\n' "$PREFIX"
    elif [ "$PURGE" -eq 1 ]; then
      rm -f "$PREFIX/.pgf-build.json"
    fi
  fi
}

_build_completion() {
  local opts="build clean --reset-all --all --libs-only --apps-only --yade --purge --prefix --isolated --print-key --print-prefix --fingerprint ${ALL_TARGETS_FLAGS[*]} ${ALL_TARGETS_NO_FLAGS[*]} --help --dry-run"
  COMPREPLY=($(compgen -W "$opts" -- "${COMP_WORDS[COMP_CWORD]}"))
}

complete -o bashdefault -o default -o nospace -F _build_completion ./build.sh

[ "${BASH_SOURCE[0]}" = "${0}" ] && main "$@"

#!/bin/bash
#
# porousGasificationFoam build driver.
#
# Native OpenFOAM user-build layout: wmake runs *in place*, in the component
# directories of this checkout. Generated state (lnInclude/, Make/$WM_OPTIONS/)
# lands beside the sources it was generated from; the final libraries and
# executables go to $FOAM_USER_LIBBIN / $FOAM_USER_APPBIN, as declared by each
# component's Make/files. No source tree is copied anywhere, so a deleted or
# renamed file cannot survive a branch switch inside a stale mirror.
#
# The script resolves its own location, so it may be invoked by absolute path
# from any working directory.

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

# Set to 1 via --purge to also delete installed libraries/executables on clean.
# Off by default: a plain clean matches `wclean` and touches build state only.
PURGE=0

# Every target is a directory of this checkout. Build order is the order of
# LIBRARY_TARGETS followed by APP_TARGETS; DEM is first so that a --yade build
# has liblambdaDotModel in place before the solver links.
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
      echo ""
      echo "Builds happen in this checkout ($PGF_ROOT); output goes to"
      echo "\$FOAM_USER_LIBBIN and \$FOAM_USER_APPBIN."
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

# The native layout has no fallbacks of its own: every path it writes to comes
# from the OpenFOAM environment. Fail loudly rather than silently building into
# a half-configured shell.
check_foam_environment() {
  local missing=()
  local var
  for var in WM_PROJECT_DIR WM_OPTIONS FOAM_USER_LIBBIN FOAM_USER_APPBIN; do
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

# Only reachable from a --yade / --DEM invocation: the normal build must never
# depend on Foam-Yade being present.
check_yade_environment() {
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

  # Link-time dependencies of the DEM library and of the WITH_YADE solver.
  # Missing libraries are reported here rather than as a bare linker error.
  local lib
  for lib in libMeshTree libYadeFoam; do
    [ -e "$FOAM_USER_LIBBIN/$lib.so" ] ||
      clog WARNING "$lib.so not found in \$FOAM_USER_LIBBIN; linking will fail unless it is elsewhere on the link path."
  done

  return 0
}

# ============================================================
# BUILD VARIANT BOOKKEEPING
# ============================================================

# wmake decides what to recompile from source timestamps; it does not know that
# WITH_YADE changed. Rebuilding the solver in place after a mode switch would
# therefore relink whatever objects happen to be there, mixing the two ABIs.
# We record the mode next to the objects it produced — inside
# Make/$WM_OPTIONS/, which `wclean` removes wholesale — and refuse to continue
# when it disagrees with the requested mode.
variant_stamp_path() {
  printf '%s/Make/%s/.pgf-build-mode' "${TARGET_DIRS[porousGasificationFoam]}" "$WM_OPTIONS"
}

check_build_variant() {
  [ "$MODE" = "build" ] || return 0
  [ "${BUILD_TARGETS[porousGasificationFoam]:-0}" -eq 1 ] || return 0

  local stamp requested recorded
  stamp="$(variant_stamp_path)"
  requested="WITH_YADE=$WITH_YADE"

  if [ -f "$stamp" ]; then
    recorded="$(cat "$stamp")"
    if [ "$recorded" != "$requested" ]; then
      clog ERROR "Build variant mismatch for porousGasificationFoam."
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
}

# ============================================================
# ARTIFACT REMOVAL
# ============================================================

# Resolve an EXE/LIB path as declared in a Make/files, e.g.
#   $(FOAM_USER_LIBBIN)/libHGSsolid  ->  /…/platforms/…/lib/libHGSsolid
# Only the two user output variables are substituted: anything else is refused
# rather than guessed at, and the caller additionally checks the prefix, so no
# path outside the user output directories can ever be produced here.
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

# --purge only. `wclean` deliberately leaves the installed library or executable
# alone — $FOAM_USER_LIBBIN and $FOAM_USER_APPBIN are shared by every project
# built into this $WM_PROJECT_USER_DIR, and OpenFOAM only ever sweeps them from
# `wclean empty`, which is guarded to $WM_PROJECT_DIR. So the default clean
# matches wclean, and this runs only when the caller asks for a guaranteed blank
# slate. It removes what the target's own Make/files declares and nothing else,
# which is why unrelated user libraries (Foam-Yade's, say) cannot be caught.
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
    if [ "$target" = "porousGasificationFoam" ] && [ "$WITH_YADE" -eq 1 ]; then
      cmd="WITH_YADE=1 ${cmd}"
      clog INFO "Building $target (WITH_YADE=1)..."
    else
      clog INFO "Building $target..."
    fi
  else
    cmd="${CLEAN_COMMANDS[$target]}"
    clog INFO "Cleaning $target..."
  fi

  # Each target is built from its own directory, so wmake generates lnInclude
  # and Make/$WM_OPTIONS in the checkout and Make/options can use paths
  # relative to the component.
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
    clog SUCCESS "$MODE complete!"
    return 0
  else
    clog ERROR "Failed: ${failed[*]}"
    return 1
  fi
}

dry_run() {
  echo "════════════════════════════════════════════════════════════"
  echo "DRY RUN - MODE: $MODE (build|clean)"
  echo "════════════════════════════════════════════════════════════"
  echo ""

  echo "PATHS:"
  echo "────────────────────────────────────────────────────────────"
  printf "  checkout:          %s\n" "$PGF_ROOT"
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
        if [ "$target" = "porousGasificationFoam" ] && [ "$WITH_YADE" -eq 1 ]; then
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
  parse_arguments "$@"

  [ "$DRY_RUN" -eq 1 ] && {
    dry_run
    exit 0
  }

  check_foam_environment || exit 1

  if [ "$MODE" = "build" ] &&
    { [ "$WITH_YADE" -eq 1 ] || [ "${BUILD_TARGETS[DEM]:-0}" -eq 1 ]; }; then
    check_yade_environment || exit 1
  fi

  check_build_variant || exit 1

  build_all_targets || exit 1
}

_build_completion() {
  local opts="build clean --reset-all --all --libs-only --apps-only --yade --purge ${ALL_TARGETS_FLAGS[*]} ${ALL_TARGETS_NO_FLAGS[*]} --help --dry-run"
  COMPREPLY=($(compgen -W "$opts" -- "${COMP_WORDS[COMP_CWORD]}"))
}

complete -o bashdefault -o default -o nospace -F _build_completion ./build.sh

[ "${BASH_SOURCE[0]}" = "${0}" ] && main "$@"

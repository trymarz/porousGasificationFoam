#!/bin/bash

. ./porousGasificationMediaDirectories
. ./utilities/bash_utils/helpers.sh

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

# Which OF-side YADE coupling backend the DEM library and the solver are
# compiled against (only relevant with --yade):
#   foamYade  legacy FoamYade coupling: drag/lift/torque feedback, meshTree
#             cell search, Gaussian interpolation (default)
#   pgfYade   minimal PgfToYadeMpiCoupler: particle kinematics in, lambdaDot
#             out, no force feedback
# Compile-time only: one solver binary carries exactly one backend. The YADE
# side of a case must instantiate the matching engine class (the DEM tutorial
# scripts read the same YADE_COUPLING_LIB variable).
YADE_COUPLING_LIB="${YADE_COUPLING_LIB:-foamYade}"

validate_coupling_lib() {
  case "$YADE_COUPLING_LIB" in
  foamYade | pgfYade) return 0 ;;
  *)
    clog ERROR "Invalid YADE_COUPLING_LIB '$YADE_COUPLING_LIB' (expected 'foamYade' or 'pgfYade')"
    return 1
    ;;
  esac
}

# The selected backend lives in the Foam-Yade source tree pointed at by
# $YADE_TRUNK; fail here rather than in a wmake include-path error. The
# backend's own header is the probe, not its directory: wmake leaves gitignored
# Make/ and lnInclude/ behind, so the directory can survive a branch switch
# that removed the sources.
check_coupling_backend_available() {
  [ "$WITH_YADE" -eq 1 ] || return 0

  local backend_header
  if [ "$YADE_COUPLING_LIB" = "pgfYade" ]; then
    backend_header="pkg/pgfToYadeCoupling/PgfToYadeMpiCoupler/PgfToYadeMpiCoupler.H"
  else
    backend_header="pkg/openfoam/coupling/FoamYade/FoamYade.H"
  fi

  if [ -z "${YADE_TRUNK:-}" ]; then
    clog ERROR "YADE_TRUNK is not set; it must point at the Foam-Yade source checkout"
    return 1
  fi
  if [ ! -f "$YADE_TRUNK/$backend_header" ]; then
    clog ERROR "YADE_COUPLING_LIB=$YADE_COUPLING_LIB needs \$YADE_TRUNK/$backend_header, which is missing"
    clog ERROR "  YADE_TRUNK=$YADE_TRUNK"
    clog ERROR "  Check out a Foam-Yade revision that provides it and run build-yade first"
    return 1
  fi
  return 0
}

declare -A TARGET_DIRS=(
  [DEM]="$FOAM_HGS/DEM"
  [fieldPorosityModel]="$FOAM_HGS/fieldPorosityModel"
  [radiationModels]="$FOAM_HGS/radiationModels"
  [thermophysicalModels]="$FOAM_HGS/thermophysicalModels"
  [pyrolysisModels]="$FOAM_HGS/pyrolysisModels"
  [porousGasificationFoam]="$WM_PROJECT_USER_DIR/applications/porousGasificationFoam"
  [utilities]="$WM_PROJECT_USER_DIR/applications/utilities"
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
    --dry-run)
      dry_run
      exit 0
      ;;
    --help)
      echo "Usage: $0 [build|clean] [OPTIONS]"
      echo "Options: --reset-all, --all, --libs-only, --apps-only, --yade"
      echo "Targets: ${ALL_TARGETS_FLAGS[*]} ${ALL_TARGETS_NO_FLAGS[*]}"
      echo ""
      echo "  --yade   Build the DEM library and solver with WITH_YADE=1"
      echo "           (required for Yade-coupled DEM simulations)"
      echo ""
      echo "Environment:"
      echo "  YADE_COUPLING_LIB  OF-side coupling backend, foamYade (default)"
      echo "                     or pgfYade; only used with --yade. Currently:"
      echo "                     $YADE_COUPLING_LIB"
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
# BUILD OPERATIONS
# ============================================================

setup_directories() {
  [ "$MODE" != "build" ] && return 0

  clog INFO "Setting up directories..."
  mkdir -p "$WM_PROJECT_USER_DIR/applications" "$FOAM_HGS" || return 1

  # Copy only selected targets
  [ "${BUILD_TARGETS[porousGasificationFoam]:-0}" -eq 1 ] && {
    clog INFO "  Copying porousGasificationFoam..."
    cp -r porousGasificationFoam "$WM_PROJECT_USER_DIR/applications/"
  }
  [ "${BUILD_TARGETS[utilities]:-0}" -eq 1 ] && {
    clog INFO "  Copying utilities..."
    cp -r utilities "$WM_PROJECT_USER_DIR/applications/"
  }
  [ "${BUILD_TARGETS[DEM]:-0}" -eq 1 ] && {
    clog INFO "  Copying DEM..."
    cp -r porousGasificationMedia/DEM "$FOAM_HGS/"
  }
  [ "${BUILD_TARGETS[fieldPorosityModel]:-0}" -eq 1 ] && {
    clog INFO "  Copying fieldPorosityModel..."
    cp -r porousGasificationMedia/fieldPorosityModel "$FOAM_HGS/"
  }
  [ "${BUILD_TARGETS[radiationModels]:-0}" -eq 1 ] && {
    clog INFO "  Copying radiationModels..."
    cp -r porousGasificationMedia/radiationModels "$FOAM_HGS/"
  }
  [ "${BUILD_TARGETS[thermophysicalModels]:-0}" -eq 1 ] && {
    clog INFO "  Copying thermophysicalModels..."
    cp -r porousGasificationMedia/thermophysicalModels "$FOAM_HGS/"
  }
  [ "${BUILD_TARGETS[pyrolysisModels]:-0}" -eq 1 ] && {
    clog INFO "  Copying pyrolysisModels..."
    cp -r porousGasificationMedia/pyrolysisModels "$FOAM_HGS/"
  }

  clog SUCCESS "Setup complete"
}

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
      cmd="WITH_YADE=1 YADE_COUPLING_LIB=$YADE_COUPLING_LIB ${cmd}"
      clog INFO "Building $target (WITH_YADE=1, YADE_COUPLING_LIB=$YADE_COUPLING_LIB)..."
    elif [ "$target" = "DEM" ]; then
      cmd="YADE_COUPLING_LIB=$YADE_COUPLING_LIB ${cmd}"
      clog INFO "Building $target (YADE_COUPLING_LIB=$YADE_COUPLING_LIB)..."
    else
      clog INFO "Building $target..."
    fi
  else
    cmd="${CLEAN_COMMANDS[$target]}"
    clog INFO "Cleaning $target..."
  fi

  cd "$dir" || return 1
  if eval "$cmd"; then
    clog SUCCESS "$target done"
    return 0
  else
    "✗ Failed on $target"
    return 1
  fi
}

build_all_targets() {
  local failed=()

  for target in "${LIBRARY_TARGETS[@]}" "${APP_TARGETS[@]}"; do
    if [ "${BUILD_TARGETS[$target]:-0}" -eq 1 ]; then
      if ! execute_target "$target"; then
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

  echo "OPTIONS:"
  echo "────────────────────────────────────────────────────────────"
  printf "  WITH_YADE=%s\n" "$WITH_YADE"
  printf "  YADE_COUPLING_LIB=%s\n" "$YADE_COUPLING_LIB"
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
          cmd="WITH_YADE=1 YADE_COUPLING_LIB=$YADE_COUPLING_LIB ${cmd}"
        elif [ "$target" = "DEM" ]; then
          cmd="YADE_COUPLING_LIB=$YADE_COUPLING_LIB ${cmd}"
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
  parse_arguments "$@"
  validate_coupling_lib || exit 1
  check_coupling_backend_available || exit 1
  setup_directories || {
    clog ERROR "Setup failed"
    exit 1
  }
  build_all_targets || exit 1
}

_build_completion() {
  local opts="build clean --reset-all --all --libs-only --apps-only --yade ${ALL_TARGETS_FLAGS[*]} ${ALL_TARGETS_NO_FLAGS[*]} --help --dry-run"
  COMPREPLY=($(compgen -W "$opts" -- "${COMP_WORDS[COMP_CWORD]}"))
}

complete -o bashdefault -o default -o nospace -F _build_completion ./build.sh

[ "${BASH_SOURCE[0]}" = "${0}" ] && main "$@"

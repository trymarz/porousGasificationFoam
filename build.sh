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
  for t in $targets; do
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
    --dry-run)
      dry_run
      exit 0
      ;;
    --help)
      clog "Usage: $0 [build|clean] [OPTIONS]"
      clog "Options: --reset-all, --all, --libs-only, --apps-only"
      clog "Targets: ${ALL_TARGETS_FLAGS[*]} ${ALL_TARGETS_NO_FLAGS[*]}"
      exit 0
      ;;
    *)
      clog error "Unknown option '$1'"
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

  clog info "Setting up directories..."
  mkdir -p "$WM_PROJECT_USER_DIR/applications" "$FOAM_HGS" || return 1

  # Copy only selected targets
  [ "${BUILD_TARGETS[porousGasificationFoam]:-0}" -eq 1 ] && {
    clog info "  Copying porousGasificationFoam..."
    cp -r porousGasificationFoam "$WM_PROJECT_USER_DIR/applications/"
  }
  [ "${BUILD_TARGETS[utilities]:-0}" -eq 1 ] && {
    clog info "  Copying utilities..."
    cp -r utilities "$WM_PROJECT_USER_DIR/applications/"
  }
  [ "${BUILD_TARGETS[DEM]:-0}" -eq 1 ] && {
    clog info "  Copying DEM..."
    cp -r porousGasificationMedia/DEM "$FOAM_HGS/"
  }
  [ "${BUILD_TARGETS[fieldPorosityModel]:-0}" -eq 1 ] && {
    clog info "  Copying fieldPorosityModel..."
    cp -r porousGasificationMedia/fieldPorosityModel "$FOAM_HGS/"
  }
  [ "${BUILD_TARGETS[radiationModels]:-0}" -eq 1 ] && {
    clog info "  Copying radiationModels..."
    cp -r porousGasificationMedia/radiationModels "$FOAM_HGS/"
  }
  [ "${BUILD_TARGETS[thermophysicalModels]:-0}" -eq 1 ] && {
    clog info "  Copying thermophysicalModels..."
    cp -r porousGasificationMedia/thermophysicalModels "$FOAM_HGS/"
  }
  [ "${BUILD_TARGETS[pyrolysisModels]:-0}" -eq 1 ] && {
    clog info "  Copying pyrolysisModels..."
    cp -r porousGasificationMedia/pyrolysisModels "$FOAM_HGS/"
  }

  clog success "Setup complete"
}

execute_target() {
  local target=$1
  local dir="${TARGET_DIRS[$target]}"
  local cmd

  [ -d "$dir" ] || {
    clog error "Directory not found: $dir"
    return 1
  }

  if [ "$MODE" = "build" ]; then
    cmd="${BUILD_COMMANDS[$target]}"
    clog info "Building $target..."
  else
    cmd="${CLEAN_COMMANDS[$target]}"
    clog info "Cleaning $target..."
  fi

  cd "$dir" || return 1
  if eval "$cmd"; then
    clog success "$target done"
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
    clog success "✓ $MODE complete!"
    return 0
  else
    clog error "Failed: ${failed[*]}"
    return 1
  fi
}

dry_run() {
  echo "════════════════════════════════════════════════════════════"
  echo "DRY RUN - MODE: $MODE (build|clean)"
  echo "════════════════════════════════════════════════════════════"
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
  setup_directories || {
    clog error "Setup failed"
    exit 1
  }
  build_all_targets || exit 1
}

_build_completion() {
  local opts="build clean --reset-all --all --libs-only --apps-only ${ALL_TARGETS_FLAGS[*]} ${ALL_TARGETS_NO_FLAGS[*]} --help --dry-run"
  COMPREPLY=($(compgen -W "$opts" -- "${COMP_WORDS[COMP_CWORD]}"))
}

complete -o bashdefault -o default -o nospace -F _build_completion ./build.sh

[ "${BASH_SOURCE[0]}" = "${0}" ] && main "$@"

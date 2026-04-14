#!/bin/bash

. ./porousGasificationMediaDirectories

# ============================================================
# CONFIGURATION
# ============================================================

declare -A BUILD_TARGETS=(
  [DEM]=1
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

declare -a LIBRARY_TARGETS=(DEM fieldPorosityModel radiationModels thermophysicalModels pyrolysisModels)
declare -a APP_TARGETS=(porousGasificationFoam utilities)

MODE="build"

# ============================================================
# ARGUMENT PARSING
# ============================================================

parse_arguments() {
  while [ $# -gt 0 ]; do
    case "$1" in
    clean | --clean) MODE="clean" ;;
    build | --build) MODE="build" ;;
    --all) : ;; # Default state
    --libs-only)
      BUILD_TARGETS[porousGasificationFoam]=0
      BUILD_TARGETS[utilities]=0
      ;;
    --apps-only)
      for t in "${LIBRARY_TARGETS[@]}"; do
        BUILD_TARGETS[$t]=0
      done
      ;;
    --dem | --porosity | --radiation | --thermophysical | --pyrolysis | --solver | --utilities)
      local t="${1#--}"
      [[ "$t" == "porosity" ]] && t="fieldPorosityModel"
      [[ "$t" == "solver" ]] && t="porousGasificationFoam"
      BUILD_TARGETS[$t]=1
      ;;
    --help)
      echo "Usage: $0 [build|clean] [OPTIONS]"
      echo "Options: --all, --libs-only, --apps-only"
      echo "Targets: --dem, --porosity, --radiation, --thermophysical, --pyrolysis, --solver, --utilities"
      exit 0
      ;;
    *)
      echo "Error: Unknown option '$1'"
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
    
    echo "Setting up directories..."
    mkdir -p "$WM_PROJECT_USER_DIR/applications" "$FOAM_HGS" || return 1
    
    # Copy only selected targets
    [ "${BUILD_TARGETS[porousGasificationFoam]:-0}" -eq 1 ] && { echo "  Copying porousGasificationFoam..."; cp -r porousGasificationFoam "$WM_PROJECT_USER_DIR/applications/"; }
    [ "${BUILD_TARGETS[utilities]:-0}" -eq 1 ] && { echo "  Copying utilities..."; cp -r utilities "$WM_PROJECT_USER_DIR/applications/"; }
    [ "${BUILD_TARGETS[DEM]:-0}" -eq 1 ] && { echo "  Copying DEM..."; cp -r porousGasificationMedia/DEM "$FOAM_HGS/"; }
    [ "${BUILD_TARGETS[fieldPorosityModel]:-0}" -eq 1 ] && { echo "  Copying fieldPorosityModel..."; cp -r porousGasificationMedia/fieldPorosityModel "$FOAM_HGS/"; }
    [ "${BUILD_TARGETS[radiationModels]:-0}" -eq 1 ] && { echo "  Copying radiationModels..."; cp -r porousGasificationMedia/radiationModels "$FOAM_HGS/"; }
    [ "${BUILD_TARGETS[thermophysicalModels]:-0}" -eq 1 ] && { echo "  Copying thermophysicalModels..."; cp -r porousGasificationMedia/thermophysicalModels "$FOAM_HGS/"; }
    [ "${BUILD_TARGETS[pyrolysisModels]:-0}" -eq 1 ] && { echo "  Copying pyrolysisModels..."; cp -r porousGasificationMedia/pyrolysisModels "$FOAM_HGS/"; }
    
    echo "✓ Setup complete"
}

execute_target() {
  local target=$1
  local dir="${TARGET_DIRS[$target]}"
  local cmd

  [ -d "$dir" ] || {
    echo "✗ Directory not found: $dir"
    return 1
  }

  if [ "$MODE" = "build" ]; then
    cmd="${BUILD_COMMANDS[$target]}"
    echo "Building $target..."
  else
    cmd="${CLEAN_COMMANDS[$target]}"
    echo "Cleaning $target..."
  fi

  cd "$dir" || return 1
  if eval "$cmd"; then
    echo "✓ $target done"
    return 0
  else
    echo "✗ Failed on $target"
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
    echo "✓ $MODE complete!"
    return 0
  else
    echo "✗ Failed: ${failed[*]}"
    return 1
  fi
}

# ============================================================
# MAIN & COMPLETION
# ============================================================

main() {
  parse_arguments "$@"
  setup_directories || {
    echo "✗ Setup failed"
    exit 1
  }
  build_all_targets || exit 1
}

_build_completion() {
  local opts="build clean --all --libs-only --apps-only --dem --porosity --radiation --thermophysical --pyrolysis --solver --utilities --help"
  COMPREPLY=($(compgen -W "$opts" -- "${COMP_WORDS[COMP_CWORD]}"))
}
complete -o bashdefault -o default -o nospace -F _build_completion ./build.sh

[ "${BASH_SOURCE[0]}" = "${0}" ] && main "$@"

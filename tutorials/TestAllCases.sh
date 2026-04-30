#!/bin/bash
# OpenFOAM Tutorial Case Runner
# Runs each case in tutorials directory, stops on errors

. ./../utilities/bash_utils/helpers.sh

trap 'echo ""; echo "Interrupted!"; kill $(jobs -p) 2>/dev/null; exit 130' INT TERM

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASES_DIR="$SCRIPT_DIR/cases"
LOG_DIR="$SCRIPT_DIR/simulation_logs"
TIMEOUT_SECONDS=20

CRASHED_CASES=()
mkdir -p "$LOG_DIR"

MOCK_YADE=false

# Parse input arguments
while [[ $# -gt 0 ]]; do
    case $1 in
    --no-yade)
        MOCK_YADE=true
        ;;
    --time=*)
        TIMEOUT_SECONDS="${1#--time=}"
        ;;
    *)
        clog WARNING "Unknown option: $1"
        ;;
    esac
    shift
done

if [[ "$MOCK_YADE" == true ]]; then
    MOCK_BIN_DIR="$SCRIPT_DIR/.mock_bin"
    mkdir -p "$MOCK_BIN_DIR"

    cat >"$MOCK_BIN_DIR/yade" <<'EOF'
#!/bin/bash
echo "[YADE MOCK] yade command is not available (mocked)"
exit 1
EOF
    chmod +x "$MOCK_BIN_DIR/yade"

    export PATH="$MOCK_BIN_DIR:$PATH"
    clog INFO "yade command is now mocked"
fi

echo "Starting OpenFOAM case runner..."
echo "========================================"

# Loop through each case directory
for case_dir in "$CASES_DIR"/*/; do
    case_name=$(basename "$case_dir")
    log_file="$LOG_DIR/${case_name}.log"

    clog "Processing case: $case_name"
    clog "Log file: $log_file"

    (
        [[ ! -f "$case_dir"/Allrun ]] && {
            clog SKIP "Allrun not present in $case_dir"
            exit 0
        }

        # Enter case directory (in subshell)
        cd "$case_dir" || {
            clog ERROR "Cannot enter directory $case_dir"
            exit 1
        }

        # Check for old results
        if [ -d "processor0" ]; then
            clog ERROR "Case contains processor0. Clean case before run!"
            exit 1
        fi

        # Run the case with timeout
        clog INFO "Running simulation for $TIMEOUT_SECONDS seconds..."

        if timeout -k 5 "$TIMEOUT_SECONDS" ./Allrun >"$log_file" 2>&1; then
            clog SUCESS "Case $case_name completed"
            exit 0
        else
            exit_code=$?

            if [ $exit_code -eq 124 ]; then
                # Timeout occurred (normal, expected behavior)
                clog SUCESS "Case $case_name ran for allocated time and stopped (timeout)"
                exit 0
            else
                # Actual error occurred
                clog ERROR "**SIMULATION CRASHED: $case_name**"
                echo ""
                echo "Last 20 lines of error log:"
                echo "========================================"
                tail -20 "$log_file" | sed 's/^/  /'
                echo "========================================"

                exit 1
            fi
        fi
    ) || CRASHED_CASES+=("$case_name")

done

echo ""
echo "========================================"
if [[ ${#CRASHED_CASES[@]} -eq 0 ]]; then
    clog SUCCESS "All cases processed successfully!"
else
    clog ERROR "Crashed simulations:"
    for case in "${CRASHED_CASES[@]}"; do
        echo "  - $case (log: $LOG_DIR/${case}.log)"
    done
    exit 1
fi

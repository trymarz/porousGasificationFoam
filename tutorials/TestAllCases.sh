#!/bin/bash
# OpenFOAM Tutorial Case Runner
# Runs each case in tutorials directory, stops on errors

TUTORIALS_DIR="cases"
TIMEOUT_SECONDS=50 # Run each case for X seconds
LOG_DIR="simulation_logs"
FAILED_CASE=""

# Create log directory
mkdir -p "$LOG_DIR"

echo "Starting OpenFOAM case runner..."
echo "========================================"

# Loop through each case directory
for case_dir in "$TUTORIALS_DIR"/*/; do
    case_name=$(basename "$case_dir")
    log_file="$LOG_DIR/${case_name}.log"

    echo ""
    echo "Processing case: $case_name"
    echo "Log file: $log_file"

    # Check if case directory exists and has necessary files
    if [[ ! -f "$case_dir/0.orig/p" ]]; then
        echo "⚠️  Skipping $case_name (missing initial conditions)"
        continue
    fi

    # Navigate to case directory
    cd "$case_dir" || {
        echo "❌ ERROR: Cannot enter directory $case_dir"
        FAILED_CASE="$case_name"
        break
    }

    # Reconstruct case if needed (remove old results)
    if [[ -d "processor0" ]]; then
        echo "❌ ERROR: Case $case_dir contains processor0. Clean case befor run!"
        FAILED_CASE="$case_name"
        break
    fi

    # Run the case with timeout
    echo "Running simulation for $TIMEOUT_SECONDS seconds..."

    if timeout "$TIMEOUT_SECONDS" ./Allrun >"$LOG_DIR/${case_name}.log" 2>&1; then
        echo "✅ Case $case_name completed successfully"
    else
        exit_code=$?

        if [[ $exit_code -eq 124 ]]; then
            # Timeout occurred (normal, expected behavior)
            echo "✅ Case $case_name ran for allocated time and stopped (timeout)"
        else
            # Actual error occurred
            echo "❌ **SIMULATION CRASHED: $case_name**"
            echo ""
            echo "Last 20 lines of error log:"
            echo "========================================"
            tail -20 "$log_file" | sed 's/^/  /'
            echo "========================================"
            FAILED_CASE="$case_name"
            break
        fi
    fi

    # Return to parent directory
    cd - >/dev/null || exit
done

echo ""
echo "========================================"
if [[ -z "$FAILED_CASE" ]]; then
    echo "✅ All cases processed successfully!"
else
    echo "❌ **STOPPED: Case '$FAILED_CASE' failed**"
    echo "Check log file: $LOG_DIR/${FAILED_CASE}.log"
    exit 1
fi

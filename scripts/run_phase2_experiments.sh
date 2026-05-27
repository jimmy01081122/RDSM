#!/bin/bash
# run_phase2_experiments.sh - Orchestrate Phase 2 experiments

set -e

# Configuration
BENCHMARK_BIN="./build/phase2_dsm_benchmark"
RESULTS_DIR="./results/phase2"
DURATION_SEC=30
MAX_RETRIES=100

# Ensure results directory exists
mkdir -p "$RESULTS_DIR"

# Experiment matrix (minimum viable)
ALGORITHMS=("baseline_occ" "backoff_occ" "hybrid_arbitration_occ")
WORKLOADS=(
    "high_contention_single_hot_product"
    "high_contention_4_hot_products"
    "zipfian_theta_0.99"
)
THREAD_COUNTS=(1 4 8)
WRITE_RATIOS=(0.5 1.0)
REPETITIONS=3

# Helper function to map workload to config
get_workload_config() {
    local workload=$1
    case $workload in
        "high_contention_single_hot_product")
            echo "--products 16 --users 100 --access-pattern uniform"
            ;;
        "high_contention_4_hot_products")
            echo "--products 16 --users 100 --access-pattern uniform"
            ;;
        "zipfian_theta_0.99")
            echo "--products 16 --users 100 --access-pattern zipfian_0.99"
            ;;
        *)
            echo "--products 16 --users 100 --access-pattern uniform"
            ;;
    esac
}

# Helper function to get algorithm-specific config
get_algorithm_config() {
    local algo=$1
    case $algo in
        "baseline_occ")
            echo "--algorithm baseline_occ --backoff-policy NO_BACKOFF"
            ;;
        "backoff_occ")
            echo "--algorithm baseline_occ --backoff-policy CONTENTION_AWARE_BACKOFF --backoff-base-us 10 --backoff-max-us 1000"
            ;;
        "hybrid_arbitration_occ")
            echo "--algorithm baseline_occ --hybrid-enabled true"
            ;;
        *)
            echo "--algorithm baseline_occ"
            ;;
    esac
}

# Run experiments
run_count=0
for algo in "${ALGORITHMS[@]}"; do
    for workload in "${WORKLOADS[@]}"; do
        for threads in "${THREAD_COUNTS[@]}"; do
            for write_ratio in "${WRITE_RATIOS[@]}"; do
                for rep in $(seq 1 $REPETITIONS); do
                    run_count=$((run_count + 1))
                    run_id=$(printf "%03d_%s_%s_%d_%d_rep%d" $run_count "$algo" "$workload" "$threads" "$((write_ratio * 100))" "$rep")
                    run_dir="$RESULTS_DIR/$run_id"
                    mkdir -p "$run_dir"

                    echo "Running experiment $run_count: $algo / $workload / $threads threads / $write_ratio write ratio (rep $rep)"

                    # Get configuration
                    workload_config=$(get_workload_config "$workload")
                    algo_config=$(get_algorithm_config "$algo")

                    # Run benchmark with OS overhead collection
                    cmd="perf stat -e context-switches,cpu-migrations,page-faults -- $BENCHMARK_BIN $workload_config $algo_config --threads $threads --write-ratio $write_ratio --duration-sec $DURATION_SEC --max-retries $MAX_RETRIES"

                    # Execute
                    stdout_file="$run_dir/benchmark.stdout.txt"
                    stderr_file="$run_dir/benchmark.stderr.txt"
                    perf_file="$run_dir/perf.stderr.txt"

                    eval $cmd > "$stdout_file" 2> "$perf_file"
                    mv "$stdout_file" "$run_dir/app_metrics.json" 2>/dev/null || true

                    # Create manifest
                    cat > "$run_dir/manifest.json" <<EOF
{
  "run_id": "$run_id",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "algorithm": "$algo",
  "workload": "$workload",
  "thread_count": $threads,
  "write_ratio": $write_ratio,
  "object_count": 16,
  "hot_object_count": 1,
  "zipf_theta": 0.99,
  "max_retries": $MAX_RETRIES,
  "backoff_policy": "CONTENTION_AWARE_BACKOFF",
  "hybrid_enabled": false,
  "environment": "VirtualBox + Ubuntu 22.04 + Soft-RoCE"
}
EOF

                    echo "  Completed: $run_dir"
                done
            done
        done
    done
done

echo "Experiment matrix completed: $run_count runs"
echo "Results saved to: $RESULTS_DIR"

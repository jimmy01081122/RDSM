#!/usr/bin/env bash
# Focused Phase 2 matrix for algorithm trade-off analysis.

set -euo pipefail

BENCHMARK_BIN="${BENCHMARK_BIN:-./build/phase2_dsm_benchmark}"
RESULTS_DIR="${RESULTS_DIR:-./results/phase2}"
DURATION_SEC="${DURATION_SEC:-1}"
MAX_RETRIES="${MAX_RETRIES:-100}"
REPETITIONS="${REPETITIONS:-2}"
START_INDEX="${START_INDEX:-200}"

mkdir -p "$RESULTS_DIR"

ALGORITHMS=(
  "baseline_occ"
  "backoff_occ"
  "hot_detection_occ"
  "hybrid_arbitration_occ"
)

THREAD_COUNTS=(1 8 16)

SCENARIOS=(
  "low_uniform_read95|--application-case flash_sale --products 16 --users 100 --hot-products 0 --hot-access-prob 0.00 --access-pattern uniform|0.05"
  "mixed_uniform_write20|--application-case flash_sale --products 16 --users 100 --hot-products 0 --hot-access-prob 0.00 --access-pattern uniform|0.20"
  "mixed_hot4_write50|--application-case flash_sale --products 16 --users 100 --hot-products 4 --hot-access-prob 0.75 --access-pattern uniform|0.50"
  "high_hot1_write100|--application-case flash_sale --products 16 --users 100 --hot-products 1 --hot-access-prob 0.90 --access-pattern uniform|1.00"
  "high_hot16_write100|--application-case flash_sale --products 16 --users 100 --hot-products 16 --hot-access-prob 1.00 --access-pattern uniform|1.00"
  "zipf99_write100|--application-case flash_sale --products 16 --users 100 --hot-products 0 --hot-access-prob 0.00 --access-pattern zipfian_0.99|1.00"
)

get_algorithm_config() {
  local algo="$1"
  case "$algo" in
    baseline_occ)
      echo "--algorithm baseline_occ --backoff-policy NO_BACKOFF --hot-detection-enabled false --hybrid-enabled false"
      ;;
    backoff_occ)
      echo "--algorithm backoff_occ --backoff-policy CONTENTION_AWARE_BACKOFF --backoff-base-us 10 --backoff-max-us 1000"
      ;;
    hot_detection_occ)
      echo "--algorithm hot_detection_occ --hot-detection-enabled true --hot-threshold 0.10 --hot-min-access 10 --hot-refresh-interval 64 --hybrid-enabled false"
      ;;
    hybrid_arbitration_occ)
      echo "--algorithm hybrid_arbitration_occ --hot-detection-enabled true --hybrid-enabled true --hot-threshold 0.10 --hot-min-access 10 --hot-refresh-interval 64 --server-worker-threads 1"
      ;;
  esac
}

run_count="$START_INDEX"
for scenario in "${SCENARIOS[@]}"; do
  IFS='|' read -r workload workload_config write_ratio <<< "$scenario"
  for algo in "${ALGORITHMS[@]}"; do
    for threads in "${THREAD_COUNTS[@]}"; do
      for rep in $(seq 1 "$REPETITIONS"); do
        run_count=$((run_count + 1))
        ratio_label="${write_ratio/./p}"
        run_id=$(printf "%03d_focused_%s_%s_t%d_w%s_rep%d" "$run_count" "$algo" "$workload" "$threads" "$ratio_label" "$rep")
        run_dir="$RESULTS_DIR/$run_id"
        mkdir -p "$run_dir"

        echo "Running $run_id"
        algo_config=$(get_algorithm_config "$algo")
        read -r -a workload_args <<< "$workload_config"
        read -r -a algo_args <<< "$algo_config"

        app_json="$run_dir/app_metrics.json"
        stdout_file="$run_dir/benchmark.stdout.txt"
        os_file="$run_dir/os_time.txt"
        perf_file="$run_dir/perf.stderr.txt"

        bench_cmd=("$BENCHMARK_BIN")
        bench_cmd+=("${workload_args[@]}" "${algo_args[@]}")
        bench_cmd+=(--threads "$threads" --write-ratio "$write_ratio" --duration-sec "$DURATION_SEC" --max-retries "$MAX_RETRIES" --output-file "$app_json")

        if command -v perf >/dev/null 2>&1; then
          /usr/bin/time -v perf stat -e context-switches,cpu-migrations,page-faults -- "${bench_cmd[@]}" > "$stdout_file" 2> "$perf_file" || true
          if [[ ! -s "$app_json" ]]; then
            /usr/bin/time -v "${bench_cmd[@]}" > "$stdout_file" 2> "$os_file"
          fi
        else
          /usr/bin/time -v "${bench_cmd[@]}" > "$stdout_file" 2> "$os_file"
        fi

        cat > "$run_dir/manifest.json" <<EOF
{
  "run_id": "$run_id",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "algorithm": "$algo",
  "workload": "$workload",
  "thread_count": $threads,
  "write_ratio": $write_ratio,
  "duration_sec": $DURATION_SEC,
  "max_retries": $MAX_RETRIES,
  "matrix": "focused_tradeoff",
  "environment": "VirtualBox + Ubuntu 22.04 + Soft-RoCE",
  "scope_note": "Protocol-level DSM/OCC evidence only; not hardware RNIC performance."
}
EOF
      done
    done
  done
done

echo "Focused matrix completed through run index $run_count"

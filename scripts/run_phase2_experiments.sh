#!/usr/bin/env bash
# Orchestrate Phase 2 DSM/OCC experiments and preserve raw JSON/OS-level data.

set -euo pipefail

BENCHMARK_BIN="${BENCHMARK_BIN:-./build/phase2_dsm_benchmark}"
RESULTS_DIR="${RESULTS_DIR:-./results/phase2}"
DURATION_SEC="${DURATION_SEC:-1}"
MAX_RETRIES="${MAX_RETRIES:-100}"
REPETITIONS="${REPETITIONS:-1}"

mkdir -p "$RESULTS_DIR"

ALGORITHMS=(
  "baseline_occ"
  "backoff_occ"
  "hot_detection_occ"
  "hybrid_arbitration_occ"
)

WORKLOADS=(
  "hot_products_1"
  "hot_products_4"
  "zipfian_theta_0.99"
)

THREAD_COUNTS=(1 4 8 16)
WRITE_RATIOS=(1.0 0.5)

get_workload_config() {
  local workload="$1"
  case "$workload" in
    hot_products_1)
      echo "--application-case flash_sale --products 16 --users 100 --hot-products 1 --hot-access-prob 0.90 --access-pattern uniform"
      ;;
    hot_products_4)
      echo "--application-case flash_sale --products 16 --users 100 --hot-products 4 --hot-access-prob 0.75 --access-pattern uniform"
      ;;
    zipfian_theta_0.99)
      echo "--application-case flash_sale --products 16 --users 100 --hot-products 0 --hot-access-prob 0.00 --access-pattern zipfian_0.99"
      ;;
    *)
      echo "--products 16 --users 100 --access-pattern uniform"
      ;;
  esac
}

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
      echo "--algorithm hot_detection_occ --hot-detection-enabled true --hot-threshold 0.10 --hot-min-access 10 --hot-refresh-interval 64"
      ;;
    hybrid_arbitration_occ)
      echo "--algorithm hybrid_arbitration_occ --hot-detection-enabled true --hybrid-enabled true --hot-threshold 0.10 --hot-min-access 10 --hot-refresh-interval 64 --server-worker-threads 1"
      ;;
    *)
      echo "--algorithm baseline_occ"
      ;;
  esac
}

run_count=0
for algo in "${ALGORITHMS[@]}"; do
  for workload in "${WORKLOADS[@]}"; do
    for threads in "${THREAD_COUNTS[@]}"; do
      for write_ratio in "${WRITE_RATIOS[@]}"; do
        for rep in $(seq 1 "$REPETITIONS"); do
          run_count=$((run_count + 1))
          ratio_label="${write_ratio/./p}"
          run_id=$(printf "%03d_%s_%s_t%d_w%s_rep%d" "$run_count" "$algo" "$workload" "$threads" "$ratio_label" "$rep")
          run_dir="$RESULTS_DIR/$run_id"
          mkdir -p "$run_dir"

          echo "Running $run_id"
          workload_config=$(get_workload_config "$workload")
          algo_config=$(get_algorithm_config "$algo")

          app_json="$run_dir/app_metrics.json"
          stdout_file="$run_dir/benchmark.stdout.txt"
          os_file="$run_dir/os_time.txt"
          perf_file="$run_dir/perf.stderr.txt"

          bench_cmd=("$BENCHMARK_BIN")
          read -r -a workload_args <<< "$workload_config"
          read -r -a algo_args <<< "$algo_config"
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
  "environment": "VirtualBox + Ubuntu 22.04 + Soft-RoCE",
  "scope_note": "Protocol-level DSM/OCC evidence only; not hardware RNIC performance."
}
EOF
        done
      done
    done
  done
done

echo "Experiment matrix completed: $run_count runs"
echo "Results saved to: $RESULTS_DIR"

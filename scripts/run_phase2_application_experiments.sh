#!/usr/bin/env bash
# Application-oriented Phase 2 scenarios beyond the high-contention matrix.

set -euo pipefail

BENCHMARK_BIN="${BENCHMARK_BIN:-./build/phase2_dsm_benchmark}"
RESULTS_DIR="${RESULTS_DIR:-./results/phase2}"
DURATION_SEC="${DURATION_SEC:-1}"
MAX_RETRIES="${MAX_RETRIES:-100}"
REPETITIONS="${REPETITIONS:-1}"
START_INDEX="${START_INDEX:-400}"

mkdir -p "$RESULTS_DIR"

ALGORITHMS=(
  "baseline_occ"
  "backoff_occ"
  "hot_detection_occ"
  "hybrid_arbitration_occ"
)

SCENARIOS=(
  "flash_sale_spike|--application-case flash_sale --products 16 --users 100 --hot-products 1 --hot-access-prob 0.95 --access-pattern uniform --threads 16 --write-ratio 1.00"
  "ticket_booking_hot_event|--application-case ticket_booking --products 64 --users 200 --hot-products 2 --hot-access-prob 0.85 --access-pattern uniform --threads 16 --write-ratio 0.80"
  "ticket_booking_many_events|--application-case ticket_booking --products 128 --users 300 --hot-products 0 --hot-access-prob 0.00 --access-pattern uniform --threads 8 --write-ratio 0.50"
  "ad_budget_hot_campaign|--application-case ad_budget --products 32 --users 500 --hot-products 4 --hot-access-prob 0.80 --access-pattern uniform --threads 16 --write-ratio 0.20"
  "ad_budget_read_heavy_dashboard|--application-case ad_budget --products 64 --users 500 --hot-products 0 --hot-access-prob 0.00 --access-pattern uniform --threads 8 --write-ratio 0.05"
  "warehouse_restock_uniform|--application-case warehouse_restock --products 128 --users 100 --hot-products 0 --hot-access-prob 0.00 --access-pattern uniform --threads 8 --write-ratio 0.30"
  "long_tail_marketplace_zipf|--application-case flash_sale --products 128 --users 300 --hot-products 0 --hot-access-prob 0.00 --access-pattern zipfian_0.99 --threads 16 --write-ratio 0.70"
  "mixed_hot_catalog|--application-case flash_sale --products 64 --users 200 --hot-products 8 --hot-access-prob 0.65 --access-pattern zipfian_0.8 --threads 16 --write-ratio 0.50"
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
  IFS='|' read -r workload workload_config <<< "$scenario"
  for algo in "${ALGORITHMS[@]}"; do
    for rep in $(seq 1 "$REPETITIONS"); do
      run_count=$((run_count + 1))
      run_id=$(printf "%03d_app_%s_%s_rep%d" "$run_count" "$algo" "$workload" "$rep")
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
      bench_cmd+=(--duration-sec "$DURATION_SEC" --max-retries "$MAX_RETRIES" --output-file "$app_json")

      if command -v perf >/dev/null 2>&1; then
        /usr/bin/time -v perf stat -e context-switches,cpu-migrations,page-faults -- "${bench_cmd[@]}" > "$stdout_file" 2> "$perf_file" || true
        if [[ ! -s "$app_json" ]]; then
          /usr/bin/time -v "${bench_cmd[@]}" > "$stdout_file" 2> "$os_file"
        fi
      else
        /usr/bin/time -v "${bench_cmd[@]}" > "$stdout_file" 2> "$os_file"
      fi

      thread_count=""
      write_ratio=""
      application_case=""
      for ((i = 0; i < ${#workload_args[@]}; i++)); do
        case "${workload_args[$i]}" in
          --threads) thread_count="${workload_args[$((i + 1))]}" ;;
          --write-ratio) write_ratio="${workload_args[$((i + 1))]}" ;;
          --application-case) application_case="${workload_args[$((i + 1))]}" ;;
        esac
      done

      cat > "$run_dir/manifest.json" <<EOF
{
  "run_id": "$run_id",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "algorithm": "$algo",
  "workload": "$workload",
  "application_case": "$application_case",
  "thread_count": ${thread_count:-0},
  "write_ratio": ${write_ratio:-0},
  "duration_sec": $DURATION_SEC,
  "max_retries": $MAX_RETRIES,
  "matrix": "application_scenarios",
  "environment": "VirtualBox + Ubuntu 22.04 + Soft-RoCE",
  "scope_note": "Protocol-level DSM/OCC evidence only; not hardware RNIC performance."
}
EOF
    done
  done
done

echo "Application scenario matrix completed through run index $run_count"

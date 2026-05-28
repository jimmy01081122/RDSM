#!/usr/bin/env bash
# Phase 4b cleanup/isolation matrix. This is not a full benchmark expansion.

set -euo pipefail

BENCHMARK_BIN="${BENCHMARK_BIN:-./build/phase2_dsm_benchmark}"
RESULTS_DIR="${RESULTS_DIR:-./results/phase4b_cleanup}"
DURATION_SEC="${DURATION_SEC:-1}"
MAX_RETRIES="${MAX_RETRIES:-100}"
REPETITIONS="${REPETITIONS:-1}"
START_INDEX="${START_INDEX:-700}"

mkdir -p "$RESULTS_DIR"

THREAD_COUNTS=(4 8)
ARBITRATION_MODES=("global:1" "per_object:1" "per_shard:8")
SOLD_COUNTER_MODES=("global" "per_product")
SCENARIOS=(
  "mixed_hot4_write50|--application-case flash_sale --products 64 --users 200 --hot-products 4 --hot-access-prob 0.75 --access-pattern uniform|0.50"
  "high_hot16_write100|--application-case flash_sale --products 64 --users 200 --hot-products 16 --hot-access-prob 1.00 --access-pattern uniform|1.00"
)

run_count="$START_INDEX"
for scenario in "${SCENARIOS[@]}"; do
  IFS='|' read -r workload workload_config write_ratio <<< "$scenario"
  for sold_counter_mode in "${SOLD_COUNTER_MODES[@]}"; do
    for mode_spec in "${ARBITRATION_MODES[@]}"; do
      IFS=':' read -r arbitration_mode hot_shards <<< "$mode_spec"
      for threads in "${THREAD_COUNTS[@]}"; do
        for rep in $(seq 1 "$REPETITIONS"); do
          run_count=$((run_count + 1))
          ratio_label="${write_ratio/./p}"
          run_id=$(printf "%03d_phase4b_%s_sold_%s_s%s_%s_t%d_w%s_rep%d" \
            "$run_count" "$sold_counter_mode" "$arbitration_mode" "$hot_shards" "$workload" "$threads" "$ratio_label" "$rep")
          run_dir="$RESULTS_DIR/$run_id"
          mkdir -p "$run_dir"

          appendix_only=false
          appendix_reason=""
          if [[ "$threads" -gt 4 ]]; then
            appendix_only=true
            appendix_reason="oversubscription_threads_exceed_exposed_cores"
          fi

          echo "Running $run_id"
          read -r -a workload_args <<< "$workload_config"
          app_json="$run_dir/app_metrics.json"
          stdout_file="$run_dir/benchmark.stdout.txt"
          os_file="$run_dir/os_time.txt"
          perf_file="$run_dir/perf.stderr.txt"

          bench_cmd=("$BENCHMARK_BIN")
          bench_cmd+=("${workload_args[@]}")
          bench_cmd+=(--algorithm hybrid_arbitration_occ --hot-detection-enabled true --hybrid-enabled true)
          bench_cmd+=(--hot-threshold 0.10 --hot-min-access 10 --hot-refresh-interval 64)
          bench_cmd+=(--arbitration-mode "$arbitration_mode" --hot-shards "$hot_shards")
          bench_cmd+=(--sold-counter-mode "$sold_counter_mode")
          bench_cmd+=(--threads "$threads" --write-ratio "$write_ratio" --duration-sec "$DURATION_SEC" --max-retries "$MAX_RETRIES")
          bench_cmd+=(--output-file "$app_json")

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
  "algorithm": "hybrid_arbitration_occ",
  "workload": "$workload",
  "thread_count": $threads,
  "write_ratio": $write_ratio,
  "duration_sec": $DURATION_SEC,
  "max_retries": $MAX_RETRIES,
  "matrix": "phase4b_cleanup",
  "appendix_only": $appendix_only,
  "appendix_reason": "$appendix_reason",
  "arbitration_mode": "$arbitration_mode",
  "hot_shards": $hot_shards,
  "sold_counter_mode": "$sold_counter_mode",
  "environment": "virtualized Linux + Ubuntu 22.04 + Soft-RoCE/rdma_rxe",
  "scope_note": "Cleanup/isolation validation only; not final performance claim."
}
EOF
        done
      done
    done
  done
done

echo "Phase 4b cleanup matrix completed through run index $run_count"

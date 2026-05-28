#!/usr/bin/env bash
# Reduced final focused matrix. This is intentionally not a full factorial sweep.

set -euo pipefail

BENCHMARK_BIN="${BENCHMARK_BIN:-./build/phase2_dsm_benchmark}"
RESULTS_DIR="${RESULTS_DIR:-./results/final_focused_matrix}"
DURATION_SEC="${DURATION_SEC:-10}"
REPETITIONS="${REPETITIONS:-3}"
LATENCY_SAMPLE_SIZE="${LATENCY_SAMPLE_SIZE:-10000}"
ROUTING_MARGIN_US="${ROUTING_MARGIN_US:-5}"
COST_WINDOW_MS="${COST_WINDOW_MS:-500}"
MAX_RETRIES="${MAX_RETRIES:-100}"

THREAD_COUNTS=(1 2 4)
ALGORITHMS=(
  "baseline_occ"
  "backoff_occ"
  "hybrid_static_arbitration_occ_global"
  "hybrid_static_arbitration_occ_per_object"
  "hybrid_static_arbitration_occ_per_shard_8"
  "hybrid_adaptive_arbitration_occ_per_shard_8"
)
WORKLOADS=(
  "low_uniform_read95"
  "mixed_uniform_write20"
  "mixed_hot4_write50"
  "high_hot1_write100"
  "high_hot16_write100"
  "zipf99_write100"
  "flash_sale_spike"
  "ticket_booking_hot_event"
  "ad_budget_read_heavy_dashboard"
  "long_tail_marketplace_zipf"
)

mkdir -p "$RESULTS_DIR"

workload_args() {
  case "$1" in
    low_uniform_read95)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 0 --hot-access-prob 0.00 --access-pattern uniform --write-ratio 0.05"
      ;;
    mixed_uniform_write20)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 0 --hot-access-prob 0.00 --access-pattern uniform --write-ratio 0.20"
      ;;
    mixed_hot4_write50)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 4 --hot-access-prob 0.85 --access-pattern uniform --write-ratio 0.50"
      ;;
    high_hot1_write100)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 1 --hot-access-prob 0.95 --access-pattern uniform --write-ratio 1.00"
      ;;
    high_hot16_write100)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 16 --hot-access-prob 1.00 --access-pattern uniform --write-ratio 1.00"
      ;;
    zipf99_write100)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 0 --hot-access-prob 0.00 --access-pattern zipfian_0.99 --write-ratio 1.00"
      ;;
    flash_sale_spike)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 4 --hot-access-prob 0.95 --access-pattern uniform --write-ratio 1.00"
      ;;
    ticket_booking_hot_event)
      echo "--application-case ticket_booking --products 64 --users 200 --hot-products 8 --hot-access-prob 0.90 --access-pattern uniform --write-ratio 0.80"
      ;;
    ad_budget_read_heavy_dashboard)
      echo "--application-case ad_budget --products 64 --users 200 --hot-products 4 --hot-access-prob 0.70 --access-pattern uniform --write-ratio 0.10"
      ;;
    long_tail_marketplace_zipf)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 0 --hot-access-prob 0.00 --access-pattern zipfian_0.99 --write-ratio 0.50"
      ;;
    *)
      echo "unknown workload: $1" >&2
      return 1
      ;;
  esac
}

algorithm_args() {
  case "$1" in
    baseline_occ)
      echo "--algorithm baseline_occ --hybrid-enabled false --hot-detection-enabled false --arbitration-mode global --hot-shards 1"
      ;;
    backoff_occ)
      echo "--algorithm backoff_occ --backoff-policy CONTENTION_AWARE_BACKOFF --hybrid-enabled false --hot-detection-enabled false --arbitration-mode global --hot-shards 1"
      ;;
    hybrid_static_arbitration_occ_global)
      echo "--algorithm hybrid_arbitration_occ --hot-detection-enabled true --hybrid-enabled true --arbitration-mode global --hot-shards 1"
      ;;
    hybrid_static_arbitration_occ_per_object)
      echo "--algorithm hybrid_arbitration_occ --hot-detection-enabled true --hybrid-enabled true --arbitration-mode per_object --hot-shards 1"
      ;;
    hybrid_static_arbitration_occ_per_shard_8)
      echo "--algorithm hybrid_arbitration_occ --hot-detection-enabled true --hybrid-enabled true --arbitration-mode per_shard --hot-shards 8"
      ;;
    hybrid_adaptive_arbitration_occ_per_shard_8)
      echo "--algorithm hybrid_adaptive_arbitration_occ --hot-detection-enabled true --hybrid-enabled true --adaptive-routing on --routing-margin-us $ROUTING_MARGIN_US --cost-window-ms $COST_WINDOW_MS --min-samples-before-adapt 100 --adaptive-object-scope shard --arbitration-mode per_shard --hot-shards 8"
      ;;
    *)
      echo "unknown algorithm: $1" >&2
      return 1
      ;;
  esac
}

run_index=0
for workload in "${WORKLOADS[@]}"; do
  read -r -a wargs <<< "$(workload_args "$workload")"
  for algorithm_label in "${ALGORITHMS[@]}"; do
    read -r -a aargs <<< "$(algorithm_args "$algorithm_label")"
    for threads in "${THREAD_COUNTS[@]}"; do
      for rep in $(seq 1 "$REPETITIONS"); do
        run_index=$((run_index + 1))
        run_id=$(printf "%03d_final_%s_%s_t%d_rep%d" "$run_index" "$algorithm_label" "$workload" "$threads" "$rep")
        run_dir="$RESULTS_DIR/$run_id"
        mkdir -p "$run_dir"

        app_json="$run_dir/app_metrics.json"
        stdout_file="$run_dir/benchmark.stdout.txt"
        os_file="$run_dir/os_time.txt"

        echo "Running $run_id"
        /usr/bin/time -v "$BENCHMARK_BIN" \
          "${wargs[@]}" \
          "${aargs[@]}" \
          --workload-name "$workload" \
          --threads "$threads" \
          --duration-sec "$DURATION_SEC" \
          --max-retries "$MAX_RETRIES" \
          --sold-counter-mode per_product \
          --latency-sampling reservoir \
          --latency-sample-size "$LATENCY_SAMPLE_SIZE" \
          --output-file "$app_json" \
          > "$stdout_file" 2> "$os_file"

        cat > "$run_dir/manifest.json" <<EOF
{
  "run_id": "$run_id",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "algorithm": "$algorithm_label",
  "workload": "$workload",
  "thread_count": $threads,
  "duration_sec": $DURATION_SEC,
  "matrix": "final_focused_matrix",
  "appendix_only": false,
  "appendix_reason": "",
  "sold_counter_mode": "per_product",
  "latency_sampling_mode": "reservoir",
  "latency_sample_size": $LATENCY_SAMPLE_SIZE,
  "routing_margin_us": $ROUTING_MARGIN_US,
  "cost_window_ms": $COST_WINDOW_MS,
  "environment": "virtualized Linux + Ubuntu 22.04 + Soft-RoCE/rdma_rxe",
  "scope_note": "Reduced focused final matrix; synthetic and application-like workloads must be reported separately."
}
EOF
      done
    done
  done
done

RESULTS_DIR="$RESULTS_DIR" python3 scripts/parse_phase2_results.py

python3 - "$RESULTS_DIR" "$DURATION_SEC" "$REPETITIONS" "$LATENCY_SAMPLE_SIZE" <<'PY'
import csv
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

results_dir = Path(sys.argv[1])
duration_sec = int(sys.argv[2])
repetitions = int(sys.argv[3])
sample_size = int(sys.argv[4])
rows = list(csv.DictReader((results_dir / "summary.csv").open()))

def f(row, key):
    try:
        return float(row.get(key, 0) or 0)
    except ValueError:
        return 0.0

groups = defaultdict(list)
for row in rows:
    groups[(row["algorithm"], row["workload"], row["thread_count"])].append(row)

stat_path = results_dir / "statistical_report.md"
lines = [
    "# Final Focused Matrix Statistical Report",
    "",
    f"- Duration per run: {duration_sec} sec",
    f"- Target repetitions: {repetitions}",
    f"- Latency sampling mode: reservoir",
    f"- Latency sample size: {sample_size}",
    "- Appendix-only filtering rule: final matrix contains only 1/2/4-thread main-body rows; rows with threads > 4 must be appendix-only in any future extension.",
    "",
    "| Algorithm | Workload | Threads | Runs | tx/sec mean | tx/sec stddev | tx/sec 95% CI | p99 latency mean us | Clean |",
    "|---|---|---:|---:|---:|---:|---:|---:|---|",
]
for (algo, workload, threads), group in sorted(groups.items()):
    vals = [f(r, "committed_tx_per_sec") for r in group]
    p99 = [f(r, "tx_latency_us_p99") for r in group]
    mean = statistics.mean(vals) if vals else 0.0
    sd = statistics.stdev(vals) if len(vals) > 1 else 0.0
    ci = 1.96 * sd / math.sqrt(len(vals)) if vals else 0.0
    clean = all(f(r, "invariant_violation_count") == 0 and f(r, "duplicate_commit_count") == 0 for r in group)
    lines.append(f"| {algo} | {workload} | {threads} | {len(group)} | {mean:.2f} | {sd:.2f} | {ci:.2f} | {statistics.mean(p99) if p99 else 0.0:.2f} | {str(clean).lower()} |")
stat_path.write_text("\n".join(lines) + "\n")

final_path = results_dir / "final_summary.md"
total_inv = sum(f(r, "invariant_violation_count") for r in rows)
total_dup = sum(f(r, "duplicate_commit_count") for r in rows)
final_path.write_text(
    "# Final Focused Matrix Summary\n\n"
    "This reduced final matrix reports synthetic and application-like workloads separately in the paper. "
    "It must not be turned into one universal ranking table.\n\n"
    f"- Rows: {len(rows)}\n"
    f"- Invariant violations: {total_inv:.0f}\n"
    f"- Duplicate commits: {total_dup:.0f}\n"
    f"- Duration per run: {duration_sec} sec\n"
    f"- Repetitions: {repetitions}\n"
    f"- Latency sampling: reservoir, sample size {sample_size}\n"
)

metadata = {
    "duration_sec": duration_sec,
    "repetitions": repetitions,
    "threads": [1, 2, 4],
    "latency_sampling_mode": "reservoir",
    "latency_sample_size": sample_size,
    "appendix_only_filtering_rule": "threads > 4 are appendix-only; this matrix uses only 1/2/4",
    "environment": "virtualized Linux + Ubuntu 22.04 + Soft-RoCE/rdma_rxe",
}
(results_dir / "run_metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
print(final_path)
print(stat_path)
PY

echo "Final focused matrix completed through run index $run_index"

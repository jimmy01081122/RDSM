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
FINAL_MATRIX_SMOKE="${FINAL_MATRIX_SMOKE:-0}"
RUN_TIMEOUT_SEC="${RUN_TIMEOUT_SEC:-120}"
MIN_DISK_FREE_KB="${MIN_DISK_FREE_KB:-1048576}"

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

if [[ "$FINAL_MATRIX_SMOKE" == "1" || "$FINAL_MATRIX_SMOKE" == "true" ]]; then
  THREAD_COUNTS=(1)
  ALGORITHMS=(
    "baseline_occ"
    "hybrid_adaptive_arbitration_occ_per_shard_8"
  )
  WORKLOADS=(
    "mixed_hot4_write50"
  )
  REPETITIONS=1
  DURATION_SEC="${SMOKE_DURATION_SEC:-1}"
fi

mkdir -p "$RESULTS_DIR"

check_disk() {
  local free_kb
  free_kb=$(df -Pk "$RESULTS_DIR" | awk 'NR==2 {print $4}')
  if [[ "$free_kb" -lt "$MIN_DISK_FREE_KB" ]]; then
    echo "ERROR: disk free ${free_kb}KB is below ${MIN_DISK_FREE_KB}KB" >&2
    exit 1
  fi
}

validate_app_metrics() {
  local app_json="$1"
  local algorithm_label="$2"
  python3 - "$app_json" "$algorithm_label" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
algorithm_label = sys.argv[2]
if not path.exists():
    raise SystemExit(f"missing app metrics: {path}")
metrics = json.loads(path.read_text())

def num(key):
    value = metrics.get(key, 0)
    return float(value) if value not in ("", None) else 0.0

if num("invariant_violation_count") > 0:
    raise SystemExit(f"invariant_violation_count > 0 in {path}")
if num("duplicate_commit_count") > 0:
    raise SystemExit(f"duplicate_commit_count > 0 in {path}")

required_latency = [
    "latency_sample_count",
    "latency_sampling_mode",
    "latency_sample_size",
    "tx_latency_us_p50",
    "tx_latency_us_p95",
    "tx_latency_us_p99",
    "committed_tx_latency_us_p50",
    "committed_tx_latency_us_p95",
    "committed_tx_latency_us_p99",
]
missing = [key for key in required_latency if key not in metrics]
if missing:
    raise SystemExit(f"missing latency metrics in {path}: {missing}")
if metrics.get("latency_sampling_mode") != "reservoir":
    raise SystemExit(f"unexpected latency_sampling_mode in {path}: {metrics.get('latency_sampling_mode')}")
if num("latency_sample_count") <= 0:
    raise SystemExit(f"latency_sample_count <= 0 in {path}")

if algorithm_label == "hybrid_adaptive_arbitration_occ_per_shard_8":
    required_adaptive = [
        "adaptive_route_to_occ_count",
        "adaptive_route_to_arbitration_count",
        "adaptive_route_to_occ_ratio",
        "adaptive_route_to_arbitration_ratio",
        "adaptive_insufficient_samples_count",
        "adaptive_bad_route_proxy_count",
        "routing_decision_latency_us_p50",
        "routing_decision_latency_us_p95",
        "routing_decision_latency_us_p99",
        "estimated_occ_cost_us_p50",
        "estimated_occ_cost_us_p95",
        "estimated_occ_cost_us_p99",
        "estimated_arbitration_cost_us_p50",
        "estimated_arbitration_cost_us_p95",
        "estimated_arbitration_cost_us_p99",
        "oscillation_count",
    ]
    missing = [key for key in required_adaptive if key not in metrics]
    if missing:
        raise SystemExit(f"missing adaptive metrics in {path}: {missing}")
PY
}

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
        check_disk
        run_index=$((run_index + 1))
        run_id=$(printf "%03d_final_%s_%s_t%d_rep%d" "$run_index" "$algorithm_label" "$workload" "$threads" "$rep")
        run_dir="$RESULTS_DIR/$run_id"
        mkdir -p "$run_dir"

        app_json="$run_dir/app_metrics.json"
        stdout_file="$run_dir/benchmark.stdout.txt"
        os_file="$run_dir/os_time.txt"

        echo "Running $run_id"
        timeout "$RUN_TIMEOUT_SEC" /usr/bin/time -v "$BENCHMARK_BIN" \
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

        validate_app_metrics "$app_json" "$algorithm_label"

        cat > "$run_dir/manifest.json" <<EOF
{
  "run_id": "$run_id",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "algorithm": "$algorithm_label",
  "algorithm_label": "$algorithm_label",
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

test -s "$RESULTS_DIR/summary.csv"
test -s "$RESULTS_DIR/summary_by_config.csv"

python3 - "$RESULTS_DIR" "$DURATION_SEC" "$REPETITIONS" "$LATENCY_SAMPLE_SIZE" "$ROUTING_MARGIN_US" "$COST_WINDOW_MS" <<'PY'
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
routing_margin_us = float(sys.argv[5])
cost_window_ms = int(float(sys.argv[6]))
rows = list(csv.DictReader((results_dir / "summary.csv").open()))
if not rows:
    raise SystemExit("summary.csv has no rows")

def f(row, key):
    try:
        return float(row.get(key, 0) or 0)
    except ValueError:
        return 0.0

groups = defaultdict(list)
for row in rows:
    required = ["tx_latency_us_p50", "tx_latency_us_p95", "tx_latency_us_p99", "latency_sample_count"]
    missing = [key for key in required if key not in row or row.get(key, "") == ""]
    if missing:
        raise SystemExit(f"missing summary metrics for {row.get('run_id')}: {missing}")
    if f(row, "invariant_violation_count") > 0 or f(row, "duplicate_commit_count") > 0:
        raise SystemExit(f"correctness failure for {row.get('run_id')}")
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
synthetic = {
    "low_uniform_read95",
    "mixed_uniform_write20",
    "mixed_hot4_write50",
    "high_hot1_write100",
    "high_hot16_write100",
    "zipf99_write100",
}
application = {
    "flash_sale_spike",
    "ticket_booking_hot_event",
    "ad_budget_read_heavy_dashboard",
    "long_tail_marketplace_zipf",
}

def section(title, names):
    section_rows = [r for r in rows if r.get("workload") in names]
    lines = [
        f"## {title}",
        "",
        f"- Rows: {len(section_rows)}",
        "| Workload | Algorithms | Threads | Runs | Correctness-clean |",
        "|---|---:|---|---:|---|",
    ]
    by_workload = defaultdict(list)
    for row in section_rows:
        by_workload[row["workload"]].append(row)
    for workload, group in sorted(by_workload.items()):
        algorithms = sorted(set(r["algorithm"] for r in group))
        threads = sorted(set(r["thread_count"] for r in group), key=lambda x: int(float(x)))
        clean = all(f(r, "invariant_violation_count") == 0 and f(r, "duplicate_commit_count") == 0 for r in group)
        lines.append(f"| {workload} | {len(algorithms)} | {','.join(threads)} | {len(group)} | {str(clean).lower()} |")
    return lines

summary_lines = [
    "# Final Focused Matrix Summary",
    "",
    "This is a reduced focused final matrix, not a publication-grade full evaluation.",
    "",
    "Synthetic and application-like workloads are reported separately. This file intentionally avoids a universal ranking table across unrelated workload families.",
    "",
    f"- Rows: {len(rows)}",
    f"- Invariant violations: {total_inv:.0f}",
    f"- Duplicate commits: {total_dup:.0f}",
    f"- Duration per run: {duration_sec} sec",
    f"- Repetitions: {repetitions}",
    f"- Latency sampling: reservoir, sample size {sample_size}",
    "",
]
summary_lines += section("Synthetic Workloads", synthetic)
summary_lines += [""] + section("Application-like Workloads", application)
summary_lines += [
    "",
    "## Interpretation Boundary",
    "",
    "- These rows are prototype-relative local DSM/OCC results.",
    "- They are not hardware RDMA latency, throughput, RNIC offload, PCIe, switch, or bare-metal scalability evidence.",
    "- Adaptive routing should be interpreted through calibration and focused workload behavior, not as a mature production policy.",
]
final_path.write_text("\n".join(summary_lines) + "\n")

metadata = {
    "duration_sec": duration_sec,
    "repetitions": repetitions,
    "threads": [1, 2, 4],
    "latency_sampling_mode": "reservoir",
    "latency_sample_size": sample_size,
    "routing_margin_us": routing_margin_us,
    "cost_window_ms": cost_window_ms,
    "matrix_scope": "reduced focused final matrix; not publication-grade full evaluation",
    "appendix_only_filtering_rule": "threads > 4 are appendix-only; this matrix uses only 1/2/4",
    "environment": "virtualized Linux + Ubuntu 22.04 + Soft-RoCE/rdma_rxe",
}
(results_dir / "run_metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
print(final_path)
print(stat_path)
PY

echo "Final focused matrix completed through run index $run_index"

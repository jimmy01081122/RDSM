#!/usr/bin/env bash
# Controlled global-vs-per-product sold-counter comparison.
# This isolates whether a shared application metadata object masks arbitration gains.

set -euo pipefail

BENCHMARK_BIN="${BENCHMARK_BIN:-./build/phase2_dsm_benchmark}"
RESULTS_DIR="${RESULTS_DIR:-./results/final_sold_counter_comparison}"
DURATION_SEC="${DURATION_SEC:-10}"
REPETITIONS="${REPETITIONS:-3}"
LATENCY_SAMPLE_SIZE="${LATENCY_SAMPLE_SIZE:-10000}"
ROUTING_MARGIN_US="${ROUTING_MARGIN_US:-5}"
COST_WINDOW_MS="${COST_WINDOW_MS:-500}"
MAX_RETRIES="${MAX_RETRIES:-100}"
RUN_TIMEOUT_SEC="${RUN_TIMEOUT_SEC:-120}"
MIN_DISK_FREE_KB="${MIN_DISK_FREE_KB:-1048576}"
HOT_SHARDS="${HOT_SHARDS:-8}"

WORKLOADS=(mixed_hot4_write50 high_hot16_write100)
SOLD_COUNTER_MODES=(global per_product)
ALGORITHMS=(
  hybrid_static_arbitration_occ_per_shard_8
  hybrid_adaptive_arbitration_occ_per_shard_8
)
THREAD_COUNTS=(2 4)

mkdir -p "$RESULTS_DIR"

check_disk() {
  local free_kb
  free_kb=$(df -Pk "$RESULTS_DIR" | awk 'NR==2 {print $4}')
  if [[ "$free_kb" -lt "$MIN_DISK_FREE_KB" ]]; then
    echo "ERROR: disk free ${free_kb}KB is below ${MIN_DISK_FREE_KB}KB" >&2
    exit 1
  fi
}

workload_args() {
  case "$1" in
    mixed_hot4_write50)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 4 --hot-access-prob 0.85 --access-pattern uniform --write-ratio 0.50"
      ;;
    high_hot16_write100)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 16 --hot-access-prob 1.00 --access-pattern uniform --write-ratio 1.00"
      ;;
    *)
      echo "unknown workload: $1" >&2
      return 1
      ;;
  esac
}

algorithm_args() {
  case "$1" in
    hybrid_static_arbitration_occ_per_shard_8)
      echo "--algorithm hybrid_arbitration_occ --hot-detection-enabled true --hybrid-enabled true --arbitration-mode per_shard --hot-shards $HOT_SHARDS"
      ;;
    hybrid_adaptive_arbitration_occ_per_shard_8)
      echo "--algorithm hybrid_adaptive_arbitration_occ --hot-detection-enabled true --hybrid-enabled true --adaptive-routing on --routing-margin-us $ROUTING_MARGIN_US --cost-window-ms $COST_WINDOW_MS --min-samples-before-adapt 100 --adaptive-object-scope shard --arbitration-mode per_shard --hot-shards $HOT_SHARDS"
      ;;
    *)
      echo "unknown algorithm: $1" >&2
      return 1
      ;;
  esac
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
if metrics.get("latency_sampling_mode") != "reservoir":
    raise SystemExit(f"unexpected latency_sampling_mode in {path}: {metrics.get('latency_sampling_mode')}")
if num("latency_sample_count") <= 0:
    raise SystemExit(f"latency_sample_count <= 0 in {path}")
for key in ("tx_latency_us_p50", "tx_latency_us_p95", "tx_latency_us_p99"):
    if key not in metrics:
        raise SystemExit(f"missing latency field {key} in {path}")

if algorithm_label == "hybrid_adaptive_arbitration_occ_per_shard_8":
    for key in (
        "adaptive_route_to_occ_count",
        "adaptive_route_to_arbitration_count",
        "routing_decision_latency_us_p50",
        "estimated_occ_cost_us_p50",
        "estimated_arbitration_cost_us_p50",
        "oscillation_count",
    ):
        if key not in metrics:
            raise SystemExit(f"missing adaptive field {key} in {path}")
PY
}

run_index=0
for workload in "${WORKLOADS[@]}"; do
  read -r -a wargs <<< "$(workload_args "$workload")"
  for sold_counter_mode in "${SOLD_COUNTER_MODES[@]}"; do
    for algorithm_label in "${ALGORITHMS[@]}"; do
      read -r -a aargs <<< "$(algorithm_args "$algorithm_label")"
      for threads in "${THREAD_COUNTS[@]}"; do
        for rep in $(seq 1 "$REPETITIONS"); do
          check_disk
          run_index=$((run_index + 1))
          run_id=$(printf "%03d_sold_%s_%s_%s_t%d_rep%d" \
            "$run_index" "$sold_counter_mode" "$algorithm_label" "$workload" "$threads" "$rep")
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
            --sold-counter-mode "$sold_counter_mode" \
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
  "matrix": "final_sold_counter_comparison",
  "appendix_only": false,
  "appendix_reason": "",
  "sold_counter_mode": "$sold_counter_mode",
  "latency_sampling_mode": "reservoir",
  "latency_sample_size": $LATENCY_SAMPLE_SIZE,
  "routing_margin_us": $ROUTING_MARGIN_US,
  "cost_window_ms": $COST_WINDOW_MS,
  "hot_shards": $HOT_SHARDS,
  "environment": "virtualized Linux + Ubuntu 22.04 + Soft-RoCE/rdma_rxe",
  "scope_note": "Controlled sold-counter bottleneck comparison; not a universal algorithm ranking."
}
EOF
        done
      done
    done
  done
done

RESULTS_DIR="$RESULTS_DIR" python3 scripts/parse_phase2_results.py
test -s "$RESULTS_DIR/summary.csv"
test -s "$RESULTS_DIR/summary_by_config.csv"

python3 - "$RESULTS_DIR" "$DURATION_SEC" "$REPETITIONS" "$LATENCY_SAMPLE_SIZE" "$ROUTING_MARGIN_US" "$COST_WINDOW_MS" "$HOT_SHARDS" <<'PY'
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
hot_shards = int(sys.argv[7])
rows = list(csv.DictReader((results_dir / "summary.csv").open()))
if not rows:
    raise SystemExit("summary.csv has no rows")

def f(row, key):
    try:
        return float(row.get(key, 0) or 0)
    except ValueError:
        return 0.0

if any(f(r, "invariant_violation_count") > 0 or f(r, "duplicate_commit_count") > 0 for r in rows):
    raise SystemExit("correctness failure in sold-counter comparison")

groups = defaultdict(list)
for row in rows:
    groups[(row["workload"], row["sold_counter_mode"], row["algorithm"], row["thread_count"])].append(row)

summary_rows = []
for (workload, sold_mode, algorithm, threads), group in sorted(groups.items()):
    txps = [f(r, "committed_tx_per_sec") for r in group]
    p99 = [f(r, "tx_latency_us_p99") for r in group]
    q99 = [f(r, "server_queue_wait_us_p99") for r in group]
    locks = [f(r, "lock_fail_count") for r in group]
    validations = [f(r, "validation_fail_count") for r in group]
    summary_rows.append({
        "workload": workload,
        "sold_counter_mode": sold_mode,
        "algorithm": algorithm,
        "threads": threads,
        "runs": len(group),
        "txps_mean": statistics.mean(txps),
        "txps_stddev": statistics.stdev(txps) if len(txps) > 1 else 0.0,
        "tx_latency_p99_mean_us": statistics.mean(p99),
        "queue_wait_p99_mean_us": statistics.mean(q99),
        "lock_fail_mean": statistics.mean(locks),
        "validation_fail_mean": statistics.mean(validations),
    })

with (results_dir / "summary_by_config.csv").open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=[
        "workload",
        "sold_counter_mode",
        "algorithm",
        "thread_count",
        "runs",
        "committed_tx_per_sec_mean",
        "committed_tx_per_sec_stddev",
        "tx_latency_us_p99_mean",
        "server_queue_wait_us_p99_mean",
        "lock_fail_count_mean",
        "validation_fail_count_mean",
        "invariant_violation_count_sum",
        "duplicate_commit_count_sum",
    ])
    writer.writeheader()
    for row in summary_rows:
        group = groups[(row["workload"], row["sold_counter_mode"], row["algorithm"], row["threads"])]
        writer.writerow({
            "workload": row["workload"],
            "sold_counter_mode": row["sold_counter_mode"],
            "algorithm": row["algorithm"],
            "thread_count": row["threads"],
            "runs": row["runs"],
            "committed_tx_per_sec_mean": row["txps_mean"],
            "committed_tx_per_sec_stddev": row["txps_stddev"],
            "tx_latency_us_p99_mean": row["tx_latency_p99_mean_us"],
            "server_queue_wait_us_p99_mean": row["queue_wait_p99_mean_us"],
            "lock_fail_count_mean": row["lock_fail_mean"],
            "validation_fail_count_mean": row["validation_fail_mean"],
            "invariant_violation_count_sum": sum(f(r, "invariant_violation_count") for r in group),
            "duplicate_commit_count_sum": sum(f(r, "duplicate_commit_count") for r in group),
        })

lines = [
    "# Sold Counter Controlled Comparison",
    "",
    "This comparison separates the application-level shared `sold_count` metadata bottleneck from arbitration-queue behavior. It is not a universal ranking table.",
    "",
    f"- Rows: {len(rows)}",
    f"- Duration per run: {duration_sec} sec",
    f"- Repetitions: {repetitions}",
    f"- Latency sampling mode in CLI: reservoir; implementation is a bounded rotating sample, sample size {sample_size}",
    f"- Adaptive defaults: routing_margin_us={routing_margin_us:g}, cost_window_ms={cost_window_ms}, hot_shards={hot_shards}",
    f"- Correctness-clean: {all(f(r, 'invariant_violation_count') == 0 and f(r, 'duplicate_commit_count') == 0 for r in rows)}",
    "",
    "## Summary By Configuration",
    "",
    "| Workload | Sold counter | Algorithm | Threads | Runs | TX/sec mean | TX/sec stddev | tx p99 us mean | queue wait p99 us mean | lock fails mean | validation fails mean |",
    "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|",
]
for row in summary_rows:
    lines.append(
        f"| {row['workload']} | {row['sold_counter_mode']} | {row['algorithm']} | {row['threads']} | {row['runs']} | "
        f"{row['txps_mean']:.2f} | {row['txps_stddev']:.2f} | {row['tx_latency_p99_mean_us']:.2f} | "
        f"{row['queue_wait_p99_mean_us']:.2f} | {row['lock_fail_mean']:.2f} | {row['validation_fail_mean']:.2f} |"
    )

lines += [
    "",
    "## Interpretation",
    "",
    "- `global` sold counter represents an application-level shared metadata bottleneck. Every successful write transaction updates the same metadata object, so even per-object or per-shard arbitration can still be forced through another shared object.",
    "- `per_product` sold counters isolate arbitration queue behavior by removing that extra shared metadata object from every transaction.",
    "- Per-object/per-shard arbitration only helps when the data model does not force every transaction through a separate shared object. If `global` underperforms or shows worse tails, the result should be read as a data-model contention effect, not as a failure of sharded queueing alone.",
]

by_pair = defaultdict(dict)
for row in summary_rows:
    by_pair[(row["workload"], row["algorithm"], row["threads"])][row["sold_counter_mode"]] = row

lines += [
    "",
    "## Per-Product vs Global Delta",
    "",
    "| Workload | Algorithm | Threads | Per-product/global TX/sec ratio | p99 latency delta us | queue wait p99 delta us |",
    "|---|---|---:|---:|---:|---:|",
]
for (workload, algorithm, threads), modes in sorted(by_pair.items()):
    if "global" not in modes or "per_product" not in modes:
        continue
    g = modes["global"]
    p = modes["per_product"]
    ratio = p["txps_mean"] / g["txps_mean"] if g["txps_mean"] else math.inf
    lines.append(
        f"| {workload} | {algorithm} | {threads} | {ratio:.3f} | "
        f"{p['tx_latency_p99_mean_us'] - g['tx_latency_p99_mean_us']:.2f} | "
        f"{p['queue_wait_p99_mean_us'] - g['queue_wait_p99_mean_us']:.2f} |"
    )

(results_dir / "sold_counter_comparison_summary.md").write_text("\n".join(lines) + "\n")
(results_dir / "run_metadata.json").write_text(json.dumps({
    "duration_sec": duration_sec,
    "repetitions": repetitions,
    "workloads": ["mixed_hot4_write50", "high_hot16_write100"],
    "sold_counter_modes": ["global", "per_product"],
    "algorithms": [
        "hybrid_static_arbitration_occ_per_shard_8",
        "hybrid_adaptive_arbitration_occ_per_shard_8",
    ],
    "threads": [2, 4],
    "latency_sampling_mode_cli": "reservoir",
    "latency_sampling_implementation_note": "bounded rotating sample, not statistically uniform Algorithm R reservoir sampling",
    "latency_sample_size": sample_size,
    "routing_margin_us": routing_margin_us,
    "cost_window_ms": cost_window_ms,
    "hot_shards": hot_shards,
    "scope": "controlled shared-metadata bottleneck comparison",
    "claim_boundary": "prototype-relative local DSM/OCC evidence; not hardware RDMA or two-node DSM-over-verbs throughput",
}, indent=2) + "\n")
print(results_dir / "sold_counter_comparison_summary.md")
PY

echo "Sold-counter comparison completed through run index $run_index"

#!/usr/bin/env bash
# Phase 5 adaptive-routing calibration.
#
# This is intentionally small: it calibrates routing margin and cost window for
# three workloads only. Do not treat the full sweep as main-paper figures.

set -euo pipefail

BENCHMARK_BIN="${BENCHMARK_BIN:-./build/phase2_dsm_benchmark}"
RESULTS_DIR="${RESULTS_DIR:-./results/phase5_adaptive_routing/calibration}"
DURATION_SEC="${DURATION_SEC:-5}"
THREADS="${THREADS:-2}"
REPETITIONS="${REPETITIONS:-2}"
MAX_RETRIES="${MAX_RETRIES:-100}"
LATENCY_SAMPLE_SIZE="${LATENCY_SAMPLE_SIZE:-10000}"
HOT_SHARDS="${HOT_SHARDS:-8}"
MIN_SAMPLES_BEFORE_ADAPT="${MIN_SAMPLES_BEFORE_ADAPT:-100}"
ADAPTIVE_OBJECT_SCOPE="${ADAPTIVE_OBJECT_SCOPE:-shard}"

mkdir -p "$RESULTS_DIR"

WORKLOADS=(low_uniform_read95 mixed_hot4_write50 high_hot16_write100)
ROUTING_MARGINS=(5 10 20)
COST_WINDOWS=(100 250 500)

workload_args() {
  case "$1" in
    low_uniform_read95)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 0 --hot-access-prob 0.00 --access-pattern uniform --write-ratio 0.05"
      ;;
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

run_index=0
for workload in "${WORKLOADS[@]}"; do
  read -r -a wargs <<< "$(workload_args "$workload")"
  for margin in "${ROUTING_MARGINS[@]}"; do
    for window in "${COST_WINDOWS[@]}"; do
      for rep in $(seq 1 "$REPETITIONS"); do
        run_index=$((run_index + 1))
        run_id=$(printf "%03d_calib_%s_margin%s_window%s_rep%d" "$run_index" "$workload" "$margin" "$window" "$rep")
        run_dir="$RESULTS_DIR/$run_id"
        mkdir -p "$run_dir"

        app_json="$run_dir/app_metrics.json"
        stdout_file="$run_dir/benchmark.stdout.txt"
        os_file="$run_dir/os_time.txt"

        echo "Running $run_id"
        /usr/bin/time -v "$BENCHMARK_BIN" \
          "${wargs[@]}" \
          --workload-name "$workload" \
          --algorithm hybrid_adaptive_arbitration_occ \
          --arbitration-mode per_shard \
          --hot-shards "$HOT_SHARDS" \
          --sold-counter-mode per_product \
          --adaptive-routing on \
          --routing-margin-us "$margin" \
          --cost-window-ms "$window" \
          --min-samples-before-adapt "$MIN_SAMPLES_BEFORE_ADAPT" \
          --adaptive-object-scope "$ADAPTIVE_OBJECT_SCOPE" \
          --threads "$THREADS" \
          --duration-sec "$DURATION_SEC" \
          --max-retries "$MAX_RETRIES" \
          --latency-sampling reservoir \
          --latency-sample-size "$LATENCY_SAMPLE_SIZE" \
          --output-file "$app_json" \
          > "$stdout_file" 2> "$os_file"

        cat > "$run_dir/manifest.json" <<EOF
{
  "run_id": "$run_id",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "algorithm": "hybrid_adaptive_arbitration_occ",
  "workload": "$workload",
  "thread_count": $THREADS,
  "duration_sec": $DURATION_SEC,
  "matrix": "phase5_adaptive_calibration",
  "appendix_only": false,
  "appendix_reason": "",
  "routing_margin_us": $margin,
  "cost_window_ms": $window,
  "min_samples_before_adapt": $MIN_SAMPLES_BEFORE_ADAPT,
  "adaptive_object_scope": "$ADAPTIVE_OBJECT_SCOPE",
  "arbitration_mode": "per_shard",
  "hot_shards": $HOT_SHARDS,
  "sold_counter_mode": "per_product",
  "latency_sampling_mode": "reservoir",
  "latency_sample_size": $LATENCY_SAMPLE_SIZE,
  "environment": "virtualized Linux + Ubuntu 22.04 + Soft-RoCE/rdma_rxe",
  "scope_note": "Adaptive routing calibration only; not a full final matrix."
}
EOF
      done
    done
  done
done

python3 - "$RESULTS_DIR" <<'PY'
import csv
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

results_dir = Path(sys.argv[1])
rows = []
for manifest_path in sorted(results_dir.glob("*/manifest.json")):
    run_dir = manifest_path.parent
    manifest = json.loads(manifest_path.read_text())
    metrics = json.loads((run_dir / "app_metrics.json").read_text())
    row = {**manifest, **metrics}
    rows.append(row)

def num(row, key):
    value = row.get(key, 0)
    return float(value) if value not in ("", None) else 0.0

groups = defaultdict(list)
for row in rows:
    groups[(row["routing_margin_us"], row["cost_window_ms"], row["workload"])].append(row)

summary_rows = []
for (margin, window, workload), group in sorted(groups.items()):
    arb_ratios = []
    for r in group:
        occ = num(r, "adaptive_route_to_occ_count")
        arb = num(r, "adaptive_route_to_arbitration_count")
        total = occ + arb
        arb_ratios.append(arb / total if total else 0.0)
    summary_rows.append({
        "routing_margin_us": margin,
        "cost_window_ms": window,
        "workload": workload,
        "runs": len(group),
        "correctness_clean": all(num(r, "invariant_violation_count") == 0 and num(r, "duplicate_commit_count") == 0 for r in group),
        "committed_tx_per_sec_mean": statistics.mean(num(r, "committed_tx_per_sec") for r in group),
        "tx_latency_us_p99_mean": statistics.mean(num(r, "tx_latency_us_p99") for r in group),
        "adaptive_route_to_arbitration_ratio_mean": statistics.mean(arb_ratios),
        "oscillation_count_mean": statistics.mean(num(r, "oscillation_count") for r in group),
        "adaptive_insufficient_samples_count_mean": statistics.mean(num(r, "adaptive_insufficient_samples_count") for r in group),
        "invariant_violations": sum(num(r, "invariant_violation_count") for r in group),
        "duplicate_commits": sum(num(r, "duplicate_commit_count") for r in group),
    })

csv_path = results_dir.parent / "calibration_summary.csv"
fields = list(summary_rows[0].keys()) if summary_rows else []
with csv_path.open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    writer.writerows(summary_rows)

by_param = defaultdict(list)
for row in summary_rows:
    by_param[(row["routing_margin_us"], row["cost_window_ms"])].append(row)

def score(param_rows):
    if not param_rows or not all(r["correctness_clean"] for r in param_rows):
        return math.inf
    low = [r for r in param_rows if r["workload"] == "low_uniform_read95"][0]
    hot = [r for r in param_rows if r["workload"] in ("mixed_hot4_write50", "high_hot16_write100")]
    low_arb = low["adaptive_route_to_arbitration_ratio_mean"]
    hot_arb_gap = sum(max(0.0, 0.001 - r["adaptive_route_to_arbitration_ratio_mean"]) for r in hot)
    p99 = sum(r["tx_latency_us_p99_mean"] for r in param_rows) / len(param_rows)
    osc = sum(r["oscillation_count_mean"] for r in param_rows) / len(param_rows)
    return low_arb * 1000.0 + hot_arb_gap * 1000.0 + p99 * 0.01 + osc * 0.1

selected = None
if by_param:
    selected = min(by_param, key=lambda p: score(by_param[p]))

md_path = results_dir.parent / "calibration_summary.md"
lines = [
    "# Phase 5 Adaptive Routing Calibration Summary",
    "",
    "Scope: small calibration matrix only. These rows select a default routing policy; they should not be used as main-paper factorial figures.",
    "",
    f"- Runs: {len(rows)}",
    f"- Correctness-clean: {all(num(r, 'invariant_violation_count') == 0 and num(r, 'duplicate_commit_count') == 0 for r in rows)}",
    f"- Selected default: routing_margin_us={selected[0] if selected else 'n/a'}, cost_window_ms={selected[1] if selected else 'n/a'}, min_samples_before_adapt=100, adaptive_object_scope=shard, hot_shards=8",
    "",
    "## Calibration Table",
    "",
    "| Margin us | Window ms | Workload | Runs | Clean | tx/sec mean | p99 latency us | Arb ratio | Oscillation | Insufficient samples |",
    "|---:|---:|---|---:|---|---:|---:|---:|---:|---:|",
]
for row in summary_rows:
    lines.append(
        f"| {row['routing_margin_us']} | {row['cost_window_ms']} | {row['workload']} | "
        f"{row['runs']} | {str(row['correctness_clean']).lower()} | "
        f"{row['committed_tx_per_sec_mean']:.0f} | {row['tx_latency_us_p99_mean']:.2f} | "
        f"{row['adaptive_route_to_arbitration_ratio_mean']:.6f} | "
        f"{row['oscillation_count_mean']:.2f} | {row['adaptive_insufficient_samples_count_mean']:.2f} |"
    )

lines += [
    "",
    "## Answers",
    "",
    "1. Correctness: all selected rows must have zero invariant violations and zero duplicate commits.",
    "2. Low-contention arbitration: prefer the parameter set with near-zero arbitration ratio under `low_uniform_read95`.",
    "3. Hot workload routing: require nonzero arbitration under hot workloads, while recognizing this prototype is conservative.",
    "4. p99 latency: compare p99 as prototype-relative latency only.",
    "5. Oscillation: prefer lower oscillation count at similar correctness and latency.",
    "6. Final default: use the selected default above unless a manual policy is chosen after review.",
]
md_path.write_text("\n".join(lines) + "\n")
print(csv_path)
print(md_path)
PY

echo "Phase 5 adaptive calibration completed through run index $run_index"

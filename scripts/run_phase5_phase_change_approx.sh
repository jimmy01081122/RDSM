#!/usr/bin/env bash
# Phase 5 scripted phase-change approximation.
#
# This intentionally runs consecutive controlled processes rather than an
# in-process phase-change generator. Treat the output as approximation evidence
# only; it cannot prove continuous adaptive-state reaction inside one process.

set -euo pipefail

BENCHMARK_BIN="${BENCHMARK_BIN:-./build/phase2_dsm_benchmark}"
RESULTS_DIR="${RESULTS_DIR:-./results/phase5_adaptive_routing/phase_change_approx}"
DURATION_SEC="${DURATION_SEC:-10}"
THREADS="${THREADS:-2}"
MAX_RETRIES="${MAX_RETRIES:-100}"
LATENCY_SAMPLE_SIZE="${LATENCY_SAMPLE_SIZE:-5000}"

mkdir -p "$RESULTS_DIR"

SCENARIOS=(
  "phase_change_uniform_to_hot_to_uniform:low_uniform_read95,mixed_hot4_write50,low_uniform_read95"
  "phase_change_hot1_to_hot16:high_hot1_write100,high_hot16_write100"
  "phase_change_read_heavy_to_write_heavy:low_uniform_read95,high_hot16_write100"
  "phase_change_zipf_low_to_zipf_high:zipf90_write50,zipf99_write100"
)

workload_args() {
  case "$1" in
    low_uniform_read95)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 0 --hot-access-prob 0.00 --access-pattern uniform --write-ratio 0.05"
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
    zipf90_write50)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 0 --hot-access-prob 0.00 --access-pattern zipfian_0.8 --write-ratio 0.50"
      ;;
    zipf99_write100)
      echo "--application-case flash_sale --products 64 --users 200 --hot-products 0 --hot-access-prob 0.00 --access-pattern zipfian_0.99 --write-ratio 1.00"
      ;;
    *)
      echo "unknown workload: $1" >&2
      return 1
      ;;
  esac
}

run_index=0
for scenario in "${SCENARIOS[@]}"; do
  IFS=':' read -r scenario_name phase_list <<< "$scenario"
  IFS=',' read -r -a phases <<< "$phase_list"
  phase_index=0
  for workload in "${phases[@]}"; do
    run_index=$((run_index + 1))
    phase_index=$((phase_index + 1))
    run_id=$(printf "%03d_%s_phase%d_%s" "$run_index" "$scenario_name" "$phase_index" "$workload")
    run_dir="$RESULTS_DIR/$run_id"
    mkdir -p "$run_dir"

    read -r -a wargs <<< "$(workload_args "$workload")"
    app_json="$run_dir/app_metrics.json"
    stdout_file="$run_dir/benchmark.stdout.txt"
    os_file="$run_dir/os_time.txt"

    echo "Running $run_id"
    /usr/bin/time -v "$BENCHMARK_BIN" \
      "${wargs[@]}" \
      --workload-name "$workload" \
      --algorithm hybrid_adaptive_arbitration_occ \
      --arbitration-mode per_shard \
      --hot-shards 4 \
      --adaptive-routing on \
      --routing-margin-us 10 \
      --cost-window-ms 250 \
      --min-samples-before-adapt 100 \
      --adaptive-object-scope shard \
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
  "matrix": "phase5_phase_change_script_approx",
  "scenario": "$scenario_name",
  "phase_index": $phase_index,
  "latency_sampling_mode": "reservoir",
  "latency_sample_size": $LATENCY_SAMPLE_SIZE,
  "environment": "virtualized Linux + Ubuntu 22.04 + Soft-RoCE/rdma_rxe",
  "scope_note": "Scripted multi-process phase-change approximation; not in-process continuous adaptation evidence."
}
EOF
  done
done

python3 - "$RESULTS_DIR" <<'PY'
import csv
import json
import sys
from pathlib import Path

results_dir = Path(sys.argv[1])
rows = []
for manifest_path in sorted(results_dir.glob("*/manifest.json")):
    run_dir = manifest_path.parent
    manifest = json.loads(manifest_path.read_text())
    metrics = json.loads((run_dir / "app_metrics.json").read_text())
    rows.append({
        "run_id": manifest["run_id"],
        "scenario": manifest["scenario"],
        "phase_index": manifest["phase_index"],
        "workload": manifest["workload"],
        "committed_tx_per_sec": metrics.get("committed_tx_per_sec", 0),
        "invariant_violation_count": metrics.get("invariant_violation_count", 0),
        "duplicate_commit_count": metrics.get("duplicate_commit_count", 0),
        "latency_sample_count": metrics.get("latency_sample_count", 0),
        "tx_latency_us_p95": metrics.get("tx_latency_us_p95", 0),
        "adaptive_route_to_occ_count": metrics.get("adaptive_route_to_occ_count", 0),
        "adaptive_route_to_arbitration_count": metrics.get("adaptive_route_to_arbitration_count", 0),
        "adaptive_insufficient_samples_count": metrics.get("adaptive_insufficient_samples_count", 0),
        "oscillation_count": metrics.get("oscillation_count", 0),
    })

csv_path = results_dir.parent / "phase_change_summary.csv"
fields = list(rows[0].keys()) if rows else []
with csv_path.open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    writer.writerows(rows)

md_path = results_dir.parent / "phase_change_summary.md"
lines = [
    "# Phase 5 Scripted Phase-Change Approximation Summary",
    "",
    "Scope: consecutive controlled processes only. This approximates phase changes across runs and does not prove continuous in-process adaptive reaction.",
    "",
    "| Scenario | Phase | Workload | tx/sec | Samples | Invariants | Duplicates | Route OCC | Route Arb | Insufficient | Oscillation |",
    "|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|",
]
for row in rows:
    lines.append(
        f"| {row['scenario']} | {row['phase_index']} | {row['workload']} | "
        f"{float(row['committed_tx_per_sec']):.0f} | {row['latency_sample_count']} | "
        f"{row['invariant_violation_count']} | {row['duplicate_commit_count']} | "
        f"{row['adaptive_route_to_occ_count']} | {row['adaptive_route_to_arbitration_count']} | "
        f"{row['adaptive_insufficient_samples_count']} | {row['oscillation_count']} |"
    )
md_path.write_text("\n".join(lines) + "\n")
print(csv_path)
print(md_path)
PY

echo "Phase 5 phase-change approximation completed through run index $run_index"

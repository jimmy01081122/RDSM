# RDSM Phase 2 Handoff

Last updated: 2026-05-27 UTC

## Current State

- Workspace: `/home/node1/RDSM`
- Remote: `git@github.com:jimmy01081122/RDSM.git`
- Main executable: `build/phase2_dsm_benchmark`
- Main report: `report.md`
- Raw results: `results/phase2/`
- Summary CSV: `results/phase2/summary.csv`
- Grouped CSV: `results/phase2/summary_by_config.csv`

## Scope Boundary

- Treat all results as VirtualBox + Ubuntu 22.04 + Soft-RoCE / local RDMA-style DSM protocol evidence.
- Do not describe these numbers as hardware RDMA NIC performance.
- Do not extrapolate absolute latency, throughput, CPU, abort ratio, or queue wait to real RNICs.

## What Was Implemented

- B0: `baseline_occ`
- B1: `backoff_occ` with contention-aware backoff
- B2: `hot_detection_occ`
- B3: `hybrid_arbitration_occ`
- Application case parameter: `--application-case flash_sale|ticket_booking|ad_budget|warehouse_restock`
- Hot access control: `--hot-access-prob`
- Hybrid/hot detection optimization: `--hot-refresh-interval`
- Focused benchmark matrix: `scripts/run_phase2_focused_experiments.sh`
- Application benchmark matrix: `scripts/run_phase2_application_experiments.sh`
- Report parser: `scripts/parse_phase2_results.py`

## Rebuild

```bash
cd /home/node1/RDSM
cmake -S . -B build
cmake --build build -j2
```

## Smoke Test

```bash
./build/phase2_dsm_benchmark \
  --application-case ticket_booking \
  --algorithm hybrid_arbitration_occ \
  --products 64 --users 200 \
  --hot-products 2 --hot-access-prob 0.85 \
  --threads 4 --write-ratio 0.8 \
  --duration-sec 1 --max-retries 20
```

Expected sanity:

- JSON output is valid.
- `invariant_violation_count` is `0`.
- `duplicate_commit_count` is `0`.
- `application_case`, `hot_access_probability`, and `hot_refresh_interval` are present.

## Reproduce Current Report

```bash
cd /home/node1/RDSM
python3 scripts/parse_phase2_results.py
```

## Rerun Experiment Matrices

Fast focused trade-off matrix:

```bash
RESULTS_DIR=./results/phase2 DURATION_SEC=1 REPETITIONS=2 START_INDEX=200 \
  ./scripts/run_phase2_focused_experiments.sh
```

Application scenario matrix:

```bash
RESULTS_DIR=./results/phase2 DURATION_SEC=1 REPETITIONS=1 START_INDEX=400 \
  ./scripts/run_phase2_application_experiments.sh
```

Original phase2 matrix:

```bash
RESULTS_DIR=./results/phase2 DURATION_SEC=1 REPETITIONS=1 \
  ./scripts/run_phase2_experiments.sh
```

After any run:

```bash
python3 scripts/parse_phase2_results.py
```

## Current Dataset

- Total parsed runs: 272
- Focused trade-off runs: 144
- Application scenario runs: 32
- Correctness status: PASS
- Invariant violations: 0
- Duplicate commits: 0

## Interpretation Notes

- `abort_rate = 0.000` does not mean there were no conflicts. Check lock failures, validation failures, and retries.
- `hot_path_ratio = 0.000` is expected for baseline/backoff/hot-detection-only because only hybrid uses server arbitration.
- Hybrid often improves high-contention scenarios by serializing hot transactions.
- Backoff is still useful as a low-overhead mitigation for moderate contention.
- Baseline can remain competitive for low-contention and uniform mixed workloads.

## Known Limitations

- Current benchmark path is a local RDMA-style DSM/OCC simulation, not a full two-node Soft-RoCE benchmark.
- Hybrid arbitration uses a coarse mutation lock; future work should shard by hot object or hot-object group.
- Tail latency is approximated from aggregate latency, not sampled percentiles.
- No crash recovery or durability.
- `perf stat` may fail when `perf_event_paranoid=4`; scripts fall back to `/usr/bin/time -v`.

## Version Control Routine

```bash
git status --short
git add experiments include scripts src report.md HANDOFF.md
git commit -m "phase2: expand benchmark scenarios and hybrid analysis"
git push origin HEAD
```

## Next Research Steps

1. Replace coarse server arbitration with per-object or sharded hot queues.
2. Add adaptive routing based on estimated OCC retry cost vs queue wait.
3. Store latency samples and compute true p50/p95/p99.
4. Add two-node Soft-RoCE transport validation for the benchmark path.
5. Increase run duration and repetitions before publication-style claims.

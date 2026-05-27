# RDSM Phase 3 Handoff

Last updated: 2026-05-27 UTC

## Current State

- Workspace: `/home/node1/RDSM`
- Remote: `git@github.com:jimmy01081122/RDSM.git`
- Main executable: `build/phase2_dsm_benchmark`
- Main report: `report.md` (Chinese full report with Phase 2 + Phase 3)
- Raw results: `results/phase2/`
- Summary CSV: `results/phase2/summary.csv`
- Grouped CSV: `results/phase2/summary_by_config.csv`
- Phase 3 raw results: `results/phase3/two_node_soft_roce_20260527_225225/`
- Phase 3 summary CSV: `results/phase3/two_node_soft_roce_summary.csv`
- Phase 3 validation report: `results/phase3/phase3_two_node_soft_roce_report.md`

## Scope Boundary

- Treat all results as VirtualBox + Ubuntu 22.04 + Soft-RoCE / local RDMA-style DSM protocol evidence.
- Do not describe these numbers as hardware RDMA NIC performance.
- Do not extrapolate absolute latency, throughput, CPU, abort ratio, or queue wait to real RNICs.
- Phase 2 is still a local RDMA-style DSM/OCC protocol benchmark, not an end-to-end two-node DSM-over-verbs benchmark.
- Phase 3 validates two-node Soft-RoCE verbs transport only. It proves the node2 -> node1 RC path works, but it does not measure distributed DSM transaction throughput.
- Latency-related DSM work remains intentionally out of scope for this phase. Perftest latency fields are parsed only as transport diagnostics.

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
- Phase 3 two-node Soft-RoCE validation runner: `scripts/run_phase3_two_node_soft_roce_validation.sh`
- Phase 3 parser: `scripts/parse_phase3_results.py`
- Full Chinese report rewritten in `report.md`

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
python3 scripts/parse_phase3_results.py
```

`scripts/parse_phase2_results.py` regenerates the older English Phase 2 report at `results/phase2/phase2_report.md`. The root `report.md` is reserved for the Chinese full Phase 2 + Phase 3 report.

## Rerun Phase 3 Two-Node Soft-RoCE Validation

Current node layout:

- Client: node2, `192.168.56.102`, `rxe0`
- Server: node1, `192.168.56.101`, `rxe0`
- SSH target: `node1@192.168.56.101`
- SSH key default: `$HOME/.ssh/node2_to_node1`

Command:

```bash
cd /home/node1/RDSM
RESULTS_DIR=./results/phase3 LAT_ITERS=1000 BW_DURATION=2 \
  ./scripts/run_phase3_two_node_soft_roce_validation.sh

python3 scripts/parse_phase3_results.py
```

Fuller sweep defaults can be changed with environment variables:

```bash
SIZES="8 64 256 1024 4096 16384 65536"
TESTS="ib_write_lat ib_read_lat ib_send_lat ib_write_bw"
LAT_ITERS=1000
BW_DURATION=2
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
- Phase 3 rerun rows: 28
- Phase 3 successful rows: 28
- Phase 1 legacy `/stat` rows parsed with Phase 3: 4
- Phase 3 transport evidence: RC QP metadata, Ethernet link type, GID index 1, local GID containing `192.168.56.102`, remote GID containing `192.168.56.101`

## Interpretation Notes

- `abort_rate = 0.000` does not mean there were no conflicts. Check lock failures, validation failures, and retries.
- `hot_path_ratio = 0.000` is expected for baseline/backoff/hot-detection-only because only hybrid uses server arbitration.
- Hybrid often improves high-contention scenarios by serializing hot transactions.
- Backoff is still useful as a low-overhead mitigation for moderate contention.
- Baseline can remain competitive for low-contention and uniform mixed workloads.
- Phase 3 Soft-RoCE numbers are diagnostic. High jitter in VirtualBox/RXE means p99/max latency should not be used for DSM latency claims.
- Compare Phase 2 and Phase 3 by purpose, not by raw units: Phase 2 is local transaction protocol throughput; Phase 3 is verbs-level transport validation.

## Known Limitations

- Current Phase 2 benchmark path is a local RDMA-style DSM/OCC simulation, not a full two-node Soft-RoCE DSM benchmark.
- Phase 3 validates standalone perftest READ/WRITE/SEND/BW transport, not the project DSM transaction path.
- No remote atomic/CAS validation row is included in Phase 3 yet.
- Hybrid arbitration uses a coarse mutation lock; future work should shard by hot object or hot-object group.
- Tail latency is approximated from aggregate latency, not sampled percentiles.
- No crash recovery or durability.
- `perf stat` may fail when `perf_event_paranoid=4`; scripts fall back to `/usr/bin/time -v`.

## Version Control Routine

```bash
git status --short
git add experiments include scripts src results/phase3 report.md HANDOFF.md
git commit -m "phase3: add two-node Soft-RoCE validation report"
git push origin HEAD
```

## Next Research Steps

1. Replace coarse server arbitration with per-object or sharded hot queues.
2. Add adaptive routing based on estimated OCC retry cost vs queue wait.
3. Convert the DSM/OCC benchmark into an end-to-end two-node verbs transport benchmark.
4. Store real DSM transaction latency samples and compute true p50/p95/p99 only after the latency methodology is designed.
5. Add remote atomic/CAS transport validation if the final DSM protocol depends on atomics.
6. Increase run duration and repetitions before publication-style claims.

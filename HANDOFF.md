# RDSM Project Handoff

Last updated: 2026-05-28 UTC

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
- Phase 4 preliminary results: `results/phase4_arbitration/`
- Phase 4 discovery summary: `results/phase4_arbitration/discovery_summary.md`
- Phase 4 sanity check: `results/phase4_arbitration/sanity_check.md`
- Phase 4b cleanup results: `results/phase4b_cleanup/`
- Phase 4b cleanup summary: `results/phase4b_cleanup/phase4b_cleanup_summary.md`
- Phase 3 validation summary bundle: `results/phase3_soft_roce_validation/`
- Phase 5 latency sampling smoke summary: `results/phase5_latency_sampling/latency_overhead_summary.md`
- Paper skeleton: `paper.md`
- Final focused matrix plan: `final_focused_matrix_plan.md`

## Scope Boundary

- Treat all results as virtualized Linux + Ubuntu 22.04 + Soft-RoCE/`rdma_rxe` plus local RDMA-style DSM protocol evidence.
- Do not describe these numbers as hardware RDMA NIC performance.
- Do not extrapolate absolute latency, throughput, CPU, abort ratio, or queue wait to real RNICs.
- Phase 2 is still a local RDMA-style DSM/OCC protocol benchmark, not an end-to-end two-node DSM-over-verbs benchmark.
- Phase 3 validates two-node Soft-RoCE verbs transport only. It proves the node2 -> node1 RC path works, but it does not measure distributed DSM transaction throughput.
- Phase 5 transaction latency sampling is prototype-relative. It is useful for comparing algorithms under identical local conditions, but it is not hardware RDMA latency evidence.
- Full transaction sampling grows with transaction count and can exhaust memory quickly. Use reservoir sampling for longer runs.

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
- Phase 4 arbitration modes in `phase2_dsm_benchmark`: `global`, `per_object`, `per_shard`
- Phase 4 focused runner: `scripts/run_phase4_arbitration_experiments.sh`
- Phase 4b cleanup runner: `scripts/run_phase4b_cleanup_experiments.sh`
- Sold counter mode: `--sold-counter-mode global|per_product`
- Phase 4 metrics: queue wait p50/p95/p99/max, queue length p50/p95/p99, service time p50/p95/p99/max
- Phase 5 latency sampler: `include/latency_sampler.h`, `src/latency_sampler.cpp`
- Phase 5 latency CLI: `--latency-sampling off|full|reservoir`, `--latency-sample-size`, `--latency-output`
- Phase 5 latency summary fields: true transaction p50/p95/p99/max, committed-only latency, abort latency, path-specific cold/hot latency, retry percentiles, and sample count
- Full Chinese report rewritten in `report.md`

## Rebuild

```bash
cd /home/node1/RDSM
cmake -S . -B build
cmake --build build -j1
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

Latency sampler smoke:

```bash
mkdir -p results/phase5_latency_sampling/smoke
./build/phase2_dsm_benchmark \
  --application-case flash_sale \
  --workload-name mixed_hot4_write50 \
  --algorithm hybrid_arbitration_occ \
  --arbitration-mode per_object \
  --products 64 --users 200 \
  --hot-products 4 --hot-access-prob 0.85 \
  --threads 2 --write-ratio 0.5 \
  --duration-sec 1 --max-retries 20 \
  --latency-sampling reservoir \
  --latency-sample-size 1000 \
  --latency-output results/phase5_latency_sampling/smoke/latency_samples.csv
```

## Reproduce Current Report

```bash
cd /home/node1/RDSM
python3 scripts/parse_phase2_results.py
python3 scripts/parse_phase3_results.py
```

Phase 3a Layer 1 cleanup has also collected `ibv_rc_pingpong` and `ib_read_bw` rows in `results/phase3/two_node_soft_roce_20260528_phase3a_layer1/`.

Project-level `two_node_rdma_validation` is deferred. The current `RDMAConnection` wrapper is not complete enough for minimal READ/WRITE/CAS validation without non-trivial RDMA CM, MR exchange, and CQ setup work.

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

## Rerun Phase 4 Arbitration Queue Matrix

Short discovery run:

```bash
RESULTS_DIR=./results/phase4_arbitration DURATION_SEC=1 REPETITIONS=1 \
  ./scripts/run_phase4_arbitration_experiments.sh

RESULTS_DIR=./results/phase4_arbitration \
  python3 scripts/parse_phase2_results.py
```

Longer report-grade runs should increase `DURATION_SEC` and `REPETITIONS`; the current checked-in dataset is only a smoke/discovery matrix.

Phase 4b cleanup/isolation run:

```bash
RESULTS_DIR=./results/phase4b_cleanup DURATION_SEC=1 REPETITIONS=1 \
  ./scripts/run_phase4b_cleanup_experiments.sh

RESULTS_DIR=./results/phase4b_cleanup \
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
- Phase 4 preliminary rows: 40
- Phase 4 correctness status: PASS after object-locking fix; smoke/discovery rows preserve invariants and duplicate-commit checks
- Phase 4b cleanup rows: 24
- Phase 4b correctness status: PASS; invariant violations 0, duplicate commits 0
- Phase 5 latency overhead smoke rows: 9
- Phase 5 latency overhead scope: 1-second, 2-thread smoke only; not final performance evidence
- Phase 5 adaptive routing: not implemented/evaluated yet

## Interpretation Notes

- `abort_rate = 0.000` does not mean there were no conflicts. Check lock failures, validation failures, and retries.
- `hot_path_ratio = 0.000` is expected for baseline/backoff/hot-detection-only because only hybrid uses server arbitration.
- Hybrid often improves high-contention scenarios by serializing hot transactions.
- Backoff is still useful as a low-overhead mitigation for moderate contention.
- Baseline can remain competitive for low-contention and uniform mixed workloads.
- Phase 3 Soft-RoCE numbers are diagnostic. High jitter in the virtualized Soft-RoCE/`rdma_rxe` environment means p99/max transport latency should not be used for DSM latency claims.
- Compare Phase 2 and Phase 3 by purpose, not by raw units: Phase 2 is local transaction protocol throughput; Phase 3 is verbs-level transport validation.
- Phase 5 latency samples are DSM prototype transaction samples. Report measurement overhead with any latency claim, and keep abort latency separate from committed transaction latency.

## Known Limitations

- Current Phase 2 benchmark path is a local RDMA-style DSM/OCC simulation, not a full two-node Soft-RoCE DSM benchmark.
- Phase 3 validates standalone perftest READ/WRITE/SEND/BW transport, not the project DSM transaction path.
- No remote atomic/CAS validation row is included in Phase 3 yet.
- Hybrid arbitration uses a coarse mutation lock; future work should shard by hot object or hot-object group.
- Phase 4 adds queue-level arbitration, but the benchmark is still a local prototype and still has shared application objects such as `sold_count`; do not treat short Phase 4 numbers as final scalability results.
- The first Phase 4 per-object/per-shard attempt exposed a lock-discipline bug between hot arbitration and OCC cold path. It was fixed by using deterministic per-object data locks in both paths before regenerating the checked-in Phase 4 summaries.
- `sold_counter_mode=global` intentionally preserves the application-level shared metadata bottleneck. `sold_counter_mode=per_product` is only for arbitration-isolation validation.
- Full latency sampling is for short smoke/debug runs only. A 1-second full-sampling run can consume hundreds of MB in this environment.
- Reservoir latency sampling is implemented, but even reservoir mode has measurable overhead and must be disclosed.
- Adaptive routing based on retry cost versus queue cost remains future work until implemented and evaluated under steady and phase-change workloads.
- No crash recovery or durability.
- `perf stat` may fail when `perf_event_paranoid=4`; scripts fall back to `/usr/bin/time -v`.

## Version Control Routine

```bash
git status --short
git add experiments include scripts src results/phase3 results/phase4_arbitration results/phase4b_cleanup report.md HANDOFF.md paper.md
git add -f results/phase5_latency_sampling/latency_overhead_summary.md results/phase5_latency_sampling/latency_overhead_summary.csv
git commit -m "phase5: add transaction latency sampling"
git push origin HEAD
```

## Next Research Steps

1. Add adaptive routing based on estimated OCC retry cost vs queue wait.
2. Evaluate adaptive routing under steady workloads and phase-change workloads.
3. Convert the DSM/OCC benchmark into an end-to-end two-node verbs transport benchmark.
4. Run the final focused matrix only after confirming duration/repetitions/thread scope.
5. Add remote atomic/CAS transport validation if the final DSM protocol depends on atomics.
6. Increase run duration and repetitions before publication-style claims.

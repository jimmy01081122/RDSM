# RDSM Project Handoff

Last updated: 2026-05-28 UTC

## Current State

- Workspace: `/home/node1/RDSM`
- Remote: `git@github.com:jimmy01081122/RDSM.git`
- Main executable: `build/phase2_dsm_benchmark`
- Current final report: `paper.md`
- Chinese report snapshot: `report.md` (stale Phase 2/3-oriented snapshot; see warning at top of file)
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
- Phase 5 latency sampling policy: `results/phase5_latency_sampling/latency_sampler_policy.md`
- Phase 5 adaptive routing design/smoke: `results/phase5_adaptive_routing/`
- Final focused matrix: `results/final_focused_matrix/`
- Final sold-counter comparison: `results/final_sold_counter_comparison/`
- Final convergence summary: `results/final_project_convergence_summary.md`
- Final audit bug report: `results/final_audit_bug_report.md`
- Audit output location note: there is currently no separate `audit/*.md` directory; the confirmed audit report and patch plan live at `results/final_audit_bug_report.md`.
- Chinese final paper: `paper_zh.md`
- Reading guide: `READING_GUIDE.md`
- Corrected preliminary prototype bundle: `prev_project/`
- Paper skeleton: `paper.md`
- Final focused matrix plan: `final_focused_matrix_plan.md`

## Scope Boundary

- Treat all results as virtualized Linux + Ubuntu 22.04 + Soft-RoCE/`rdma_rxe` plus local RDMA-style DSM protocol evidence.
- Do not describe these numbers as hardware RDMA NIC performance.
- Do not extrapolate absolute latency, throughput, CPU, abort ratio, or queue wait to real RNICs.
- Final phase numbering is:
  - Phase 1: Two-VM Soft-RoCE Feasibility and Trust Boundary.
  - Phase 2: RDMA-style DSM/OCC Local Protocol Prototype.
  - Phase 3: Contention Behavior and Static Hybrid Arbitration.
  - Phase 4: Scalable Arbitration Queues and Cleanup.
  - Phase 5: Latency Sampling and Adaptive Routing.
  - Future Phase 6: Project-level Two-node RDMA Wrapper Validation.
  - Future Phase 7: Two-node DSM Transaction over RDMA Verbs.
- Phase 1 is the old Phase 3/3a Soft-RoCE validation evidence. Old directory names are retained for reproducibility, but final writing treats them as Phase 1 evidence.
- Phase 2/3/4/5 are local protocol benchmarks unless explicitly stated otherwise.
- Phase 2 is still a local RDMA-style DSM/OCC protocol benchmark, not an end-to-end two-node DSM-over-verbs benchmark.
- Phase 1 validates two-node Soft-RoCE verbs transport only. It proves the node2 -> node1 RC path works, but it does not measure distributed DSM transaction throughput.
- Phase 5 transaction latency sampling is prototype-relative. It is useful for comparing algorithms under identical local conditions, but it is not hardware RDMA latency evidence.
- Full transaction sampling grows with transaction count and can exhaust memory quickly. It is debug-only and guarded; use `bounded_rotation` for latency analysis.
- `bounded_rotation` is a bounded rotating sample, not statistically uniform Algorithm R reservoir sampling. The historical `reservoir` CLI value remains an alias. Treat p95/p99 as prototype-relative tail indicators under the same sampling policy.
- Do not run final matrix with `--latency-sampling=full`.
- Current absence: no project-level two-node DSM-over-verbs path.
- Current absence: no project-level remote CAS validation.

## Pre-Phase: Preliminary Prototype and Problem Origin

- Previous project location: `/home/node1/RDSM/prev_project`
- Previous documents used: `prev_project/README.md`, `prev_project/docs/FINAL_SUMMARY.md`, `prev_project/docs/PROJECT_REPORT.md`, `prev_project/docs/COMPLETION_REPORT.md`, and `prev_project/docs/ENV.md`.
- Previous implemented components confirmed from source/docs: FaRM-like DSM prototype, HERD-like key-value store, RDMA verbs wrapper, slab allocator / memory manager, OCC transaction path, performance monitor, OS-level analysis tools, and benchmark executables `farm_benchmark`, `herd_benchmark`, `os_analysis`.
- Corrected interpretation: old nanosecond RDMA/HERD numbers are historical local prototype observations or local software-path measurements unless measured from `post_send` to CQE completion across a real two-node verbs path. They are not hardware RDMA operation latency.
- Motivation link: the broad earlier prototype exposed the hot-object contention problem and the limitations of OCC retry storms, then this project narrowed the scope into controlled Soft-RoCE transport validation, local DSM/OCC contention experiments, scalable arbitration queues, bounded latency sampling, and adaptive-routing evaluation.
- Warning: Do not reuse old Soft-RoCE/WSL2 latency numbers as hardware RDMA evidence. Old numbers are allowed only as historical prototype observations.

## One-sided / Two-sided Clarification

- The local DSM/OCC prototype uses RDMA-style one-sided READ/WRITE/CAS abstractions for protocol development, but it is not a complete two-node verbs execution path.
- The two-VM Soft-RoCE validation includes one-sided RDMA READ/WRITE tools and message-style SEND latency tests.
- Do not describe the entire project as purely one-sided.
- Project-level two-node DSM transactions over RDMA verbs and project-level remote atomic/CAS validation remain future work.

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
- Phase 5 latency CLI: `--latency-sampling off|full|bounded_rotation|reservoir`, `--latency-sample-size`, `--latency-output`, `--allow-dangerous-full-sampling`
- Phase 5 latency summary fields: transaction p50/p95/p99/max, committed-only latency, abort latency, path-specific cold/hot latency, retry percentiles, and sample count
- Phase 5 adaptive routing prototype: `hybrid_adaptive_arbitration_occ`
- Phase 5 adaptive routing CLI: `--adaptive-routing on|off`, `--routing-margin-us`, `--cost-window-ms`, `--min-samples-before-adapt`, `--adaptive-object-scope global|shard|object`
- Phase 5 phase-change approximation script: `scripts/run_phase5_phase_change_approx.sh`
- Full Chinese report rewritten in `report.md`
- Research plan checklist: `PROJECT_PLAN_STATUS.md`

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
  --latency-sampling bounded_rotation \
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

- Phase 2 parsed data rows: 272 (`results/phase2/summary.csv` has 273 file lines including the header)
- Focused trade-off runs: 144
- Application scenario runs: 32
- Correctness status: PASS
- Invariant violations: 0
- Duplicate commits: 0
- Phase 3 rerun rows: 32
- Phase 3 successful rerun rows: 32
- Phase 1 legacy `/stat` rows parsed with Phase 3: 4
- Phase 3 total parsed data rows: 36 (`results/phase3/two_node_soft_roce_summary.csv` has 37 file lines including the header)
- Phase 3 transport evidence: RC QP metadata, Ethernet link type, GID index 1, local GID containing `192.168.56.102`, remote GID containing `192.168.56.101`
- Phase 4 preliminary rows: 40
- Phase 4 correctness status: PASS after object-locking fix; smoke/discovery rows preserve invariants and duplicate-commit checks
- Phase 4b cleanup rows: 24
- Phase 4b correctness status: PASS; invariant violations 0, duplicate commits 0
- Phase 5 latency overhead smoke rows: 9
- Phase 5 latency overhead scope: 1-second, 2-thread smoke only; not final performance evidence
- Phase 5 latency default sample size: 10,000; smoke sample size: 5,000 or lower
- Phase 5 adaptive routing smoke rows: 3
- Phase 5 adaptive calibration rows: 54
- Phase 5 adaptive calibration correctness: PASS; invariant violations 0, duplicate commits 0
- Selected adaptive default: `routing_margin_us=5`, `cost_window_ms=500`, `min_samples_before_adapt=100`, `adaptive_object_scope=shard`, `hot_shards=8`
- Phase 5 scripted phase-change approximation rows: 18
- Phase 5 scripted phase-change approximation correctness: PASS; invariant violations 0, duplicate commits 0
- Phase 5 adaptive routing status: calibrated prototype included in the reduced final matrix; performance claims must be based on per-workload final-matrix analysis and remain prototype-relative
- Reduced final focused matrix rows: 540
- Reduced final focused matrix correctness: PASS; invariant violations 0, duplicate commits 0
- Reduced final focused matrix scope: 10-second runs, 3 repetitions, threads 1/2/4, historical bounded `reservoir` alias with sample size 10,000
- Reduced final focused matrix status: completed; this is not a publication-grade full evaluation
- Final sold-counter comparison rows: 48
- Final sold-counter comparison correctness: PASS; invariant violations 0, duplicate commits 0
- Final sold-counter comparison path: `results/final_sold_counter_comparison/`
- Final sold-counter comparison status: completed; compare `global` vs `per_product` only as a shared-metadata bottleneck study

## Interpretation Notes

- `abort_rate = 0.000` does not mean there were no conflicts. Check lock failures, validation failures, and retries.
- Counter schema version 2 computes `abort_rate = (final_abort_tx + business_abort_tx) / logical_tx`. Historical rows used a mixed retry-attempt denominator and must be compared only with an explicit schema-sensitive migration.
- `hot_path_ratio = 0.000` is expected for baseline/backoff/hot-detection-only because only hybrid uses server arbitration.
- Hybrid often improves high-contention scenarios by serializing hot transactions.
- Backoff is still useful as a low-overhead mitigation for moderate contention.
- Baseline can remain competitive for low-contention and uniform mixed workloads.
- Phase 3 Soft-RoCE numbers are diagnostic. High jitter in the virtualized Soft-RoCE/`rdma_rxe` environment means p99/max transport latency should not be used for DSM latency claims.
- Compare Phase 2 and Phase 3 by purpose, not by raw units: Phase 2 is local transaction protocol throughput; Phase 3 is verbs-level transport validation.
- Phase 5 latency samples are DSM prototype transaction samples. Report measurement overhead with any latency claim, and keep abort latency separate from committed transaction latency.
- Adaptive-routing smoke rows verify plumbing and correctness only. They do not prove adaptive routing improves throughput or tail latency.
- The phase-change approximation script restarts the benchmark between phases; it cannot prove continuous in-process adaptation.

## Known Limitations

- Current Phase 2 benchmark path is a local RDMA-style DSM/OCC simulation, not a full two-node Soft-RoCE DSM benchmark.
- Phase 3 validates standalone perftest READ/WRITE/SEND/BW transport, not the project DSM transaction path.
- No remote atomic/CAS validation row is included in Phase 3 yet.
- Hybrid arbitration now supports global, per-object, and per-shard arbitration. Future work can still improve production-grade scheduling, fairness, and recovery semantics.
- Phase 4 adds queue-level arbitration, but the benchmark is still a local prototype and still has shared application objects such as `sold_count`; do not treat short Phase 4 numbers as final scalability results.
- The first Phase 4 per-object/per-shard attempt exposed a lock-discipline bug between hot arbitration and OCC cold path. It was fixed by using deterministic per-object data locks in both paths before regenerating the checked-in Phase 4 summaries.
- `sold_counter_mode=global` intentionally preserves the application-level shared metadata bottleneck. `sold_counter_mode=per_product` is only for arbitration-isolation validation.
- Full latency sampling is for short smoke/debug runs only. The benchmark rejects full sampling when `duration_sec > 2` or `threads > 2` unless `--allow-dangerous-full-sampling` is passed.
- `bounded_rotation` is not statistically uniform Algorithm R reservoir sampling. Even this bounded mode has measurable overhead and must be disclosed. The historical `reservoir` value remains an input alias. The default sample size is 10,000.
- Adaptive routing based on retry cost versus queue cost is implemented as a minimal prototype, calibrated, and included in the reduced final matrix. It is not yet a mature production routing policy.
- No crash recovery or durability.
- `perf stat` may fail when `perf_event_paranoid=4`; scripts fall back to `/usr/bin/time -v`.

## Code Fixes Applied 2026-06-01

- OCC partial lock acquisition now rolls back owned lock bits and records lock-acquisition aborts.
- Latency histogram buckets, arbitrator counters, and object count are atomic where shared reads or writes can occur.
- `latency_us_p50/p95/p99` compatibility keys now emit real sampler percentiles; synthetic average-derived p95/p99 fields were removed.
- Duplicate-commit and adaptive hot-to-OCC route instrumentation are active. The latter is a route-level interference proxy.
- Zipfian distributions are cached per thread, ineffective in-mutex read spinning was removed, and arbitrator aborts are counted once.
- `bounded_rotation` is the canonical CLI and JSON metadata name; the historical `reservoir` value remains an accepted alias.
- Counter schema version 2 separates logical transactions, OCC commit attempts, failed OCC attempts, final aborts, and business aborts. Historical mixed-denominator `abort_rate` rows are schema-sensitive.
- Dormant RDMA CAS now requires a caller-provided registered local result buffer and `lkey`; `RDMAConnection` copying is disabled. This is build verification only, not hardware validation.

## Future Phase 6: Project-level Two-node RDMA Wrapper Validation

Purpose: validate the project's own RDMA wrapper across two VMs before attempting DSM transactions.

Required future capabilities:

```text
RDMA connection setup
Protection Domain / CQ / QP setup
Memory Region registration
remote address / rkey exchange
RDMA WRITE validation
RDMA READ validation
RDMA CAS validation
CQ completion validation
error handling and timeout
```

Future outputs:

```text
results/phase6_two_node_wrapper_validation/summary.md
results/phase6_two_node_wrapper_validation/summary.csv
results/phase6_two_node_wrapper_validation/run_metadata.json
```

Allowed future claim: the project RDMA wrapper can execute READ/WRITE/CAS over two-node Soft-RoCE. Still forbidden: hardware RDMA performance, DSM transaction throughput, RNIC offload, and real RDMA p99 latency.

## Future Phase 7: Two-node DSM Transaction over RDMA Verbs

Prerequisite: Future Phase 6 must succeed first.

Minimal future transaction:

```text
single-object transaction
RDMA READ object/version
RDMA CAS lock
RDMA WRITE update
RDMA WRITE unlock/version
verify final value/version
```

Future outputs:

```text
results/phase7_two_node_dsm_transaction/summary.md
results/phase7_two_node_dsm_transaction/summary.csv
results/phase7_two_node_dsm_transaction/correctness_report.md
```

Allowed future claim: a minimal DSM/OCC transaction path can run over two-node Soft-RoCE. Still forbidden: hardware RDMA performance, production DSM, cluster scalability, and durability.

## Reproduction Commands for Completed Current-cycle Artifacts

Adaptive calibration reproduction command:

```bash
DURATION_SEC=5 REPETITIONS=2 LATENCY_SAMPLE_SIZE=10000 \
  ./scripts/run_phase5_adaptive_calibration.sh
```

Phase-change approximation reproduction command:

```bash
RESULTS_DIR=./results/phase5_adaptive_routing/phase_change_approx \
  DURATION_SEC=5 REPETITIONS=2 LATENCY_SAMPLE_SIZE=10000 \
  ROUTING_MARGIN_US=5 COST_WINDOW_MS=500 HOT_SHARDS=8 \
  ./scripts/run_phase5_phase_change_approx.sh
```

Reduced final matrix reproduction command:

```bash
DURATION_SEC=10 REPETITIONS=3 LATENCY_SAMPLE_SIZE=10000 \
  ROUTING_MARGIN_US=5 COST_WINDOW_MS=500 \
  ./scripts/run_final_focused_matrix.sh
```

Final matrix smoke subset reproduction command:

```bash
FINAL_MATRIX_SMOKE=1 RESULTS_DIR=./results/final_focused_matrix_smoke \
  DURATION_SEC=10 REPETITIONS=3 LATENCY_SAMPLE_SIZE=10000 \
  ROUTING_MARGIN_US=5 COST_WINDOW_MS=500 \
  ./scripts/run_final_focused_matrix.sh
```

Do not run the reduced final matrix with full latency sampling.

Controlled sold-counter comparison reproduction command:

```bash
./scripts/run_sold_counter_comparison.sh
```

This comparison is complete in `results/final_sold_counter_comparison/`.

## Version Control Routine

```bash
git status --short
git add experiments include scripts src README.md report.md HANDOFF.md paper.md PROJECT_PLAN_STATUS.md docs
git add -f results/phase5_latency_sampling results/phase5_adaptive_routing results/final_focused_matrix results/final_sold_counter_comparison results/final_project_convergence_summary.md results/final_audit_bug_report.md
git commit -m "final: consolidate evaluation and audit"
git push origin HEAD
```

## Next Research Steps

1. Use `paper.md` as the current final research report and update `report.md` only if a full Chinese final report is required.
2. Keep the audit bug report visible; apply code fixes only after explicit authorization or `APPLY_CODE_FIXES=1`.
3. Optionally run extended validation with longer duration/repetitions if requested.
4. Treat project-level two-node RDMA wrapper validation as Future Phase 6, not current-cycle work.
5. Treat two-node DSM transaction over RDMA verbs as Future Phase 7, not current-cycle work.

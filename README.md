# RDSM: Contention-aware Transaction Routing for RDMA-style DSM
- Instructor : CW Hsueh
- Class : AOS (CSIE7010)
- Year : 2026 Spring

This repository studies contention-aware transaction routing for an RDMA-style DSM prototype under constrained software-RDMA prototyping.

## Claim Boundary

The environment is virtualized Linux + Ubuntu 22.04 + Soft-RoCE/`rdma_rxe`; no hardware RDMA NIC is available. Results in this repository are transport diagnostics, local protocol evidence, or prototype-relative comparisons. They must not be described as hardware RDMA latency/throughput, RNIC offload, PCIe/switch behavior, bare-metal scalability, project-level two-node DSM-over-verbs throughput, or project-level remote CAS correctness.

## Phase Structure

- Stage 0: preliminary prototype and problem origin from `prev_project/`.
- Phase 1: two-VM Soft-RoCE verbs feasibility and trust boundary.
- Phase 2: local RDMA-style DSM/OCC protocol prototype.
- Phase 3: contention behavior, backoff, hot detection, and static hybrid arbitration.
- Phase 4: scalable arbitration queues plus global/per-product sold-counter cleanup.
- Phase 5: bounded transaction latency sampling and adaptive-routing prototype.
- Future Phase 6: project-level two-node RDMA wrapper validation.
- Future Phase 7: two-node DSM transaction over RDMA verbs.

## Algorithms

- `baseline_occ`
- `backoff_occ`
- `hot_detection_occ`
- `hybrid_arbitration_occ`
- `hybrid_static_arbitration_occ_global`
- `hybrid_static_arbitration_occ_per_object`
- `hybrid_static_arbitration_occ_per_shard_8`
- `hybrid_adaptive_arbitration_occ_per_shard_8`

The final focused matrix uses the labeled variants above for reporting. The executable exposes them through `--algorithm`, `--arbitration-mode`, `--hot-shards`, and `--adaptive-routing`.

## Build

```bash
cmake -S . -B build
cmake --build build -j2
```

The main benchmark executable is:

```bash
./build/phase2_dsm_benchmark
```

## Project Structure

```text
RDSM/
├── include/                         # Public headers and prototype interfaces
│   ├── latency_sampler.h            # Bounded transaction latency sampler
│   └── *.h                          # DSM objects, OCC, backoff, RDMA connection helpers
├── src/                             # Core implementation
│   ├── dsm_object.cpp               # Versioned object store and statistics
│   ├── occ_engine.cpp               # Local RDMA-style OCC transaction engine
│   ├── server_arbitration.cpp       # Legacy server-arbitration component
│   ├── latency_sampler.cpp          # Bounded rotating sample implementation
│   └── rdma_conn.cpp                # Project RDMA wrapper prototype
├── experiments/
│   └── phase2_dsm_benchmark.cpp     # Main local DSM/OCC benchmark driver
├── results/                         # Checked-in summaries and selected raw artifacts
│   ├── final_focused_matrix/         # Reduced final focused matrix
│   └── final_sold_counter_comparison/# Global vs per-product comparison
├── docs/                            # Methodology notes and Stage 0 source discussion
├── prev_project/                    # Corrected historical preliminary prototype
├── paper.md                         # Current English final research report
├── paper_zh.md                      # Chinese version of the final paper
├── HANDOFF.md                       # Reproduction/status handoff
├── PROJECT_PLAN_STATUS.md           # Research checklist
└── READING_GUIDE.md                 # Chinese reading guide
```

## Useful Commands

Run a short benchmark:

```bash
./build/phase2_dsm_benchmark \
  --application-case flash_sale \
  --workload-name mixed_hot4_write50 \
  --algorithm hybrid_arbitration_occ \
  --arbitration-mode per_shard \
  --hot-shards 8 \
  --products 64 --users 200 \
  --hot-products 4 --hot-access-prob 0.85 \
  --threads 2 --write-ratio 0.5 \
  --duration-sec 1 \
  --sold-counter-mode per_product \
  --latency-sampling bounded_rotation \
  --latency-sample-size 1000
```

Run the CTest regression suite:

```bash
ctest --test-dir build --output-on-failure
```

Run a direct five-algorithm smoke matrix:

```bash
for algo in baseline_occ backoff_occ hot_detection_occ \
  hybrid_arbitration_occ hybrid_adaptive_arbitration_occ; do
  ./build/phase2_dsm_benchmark \
    --products 4 --users 10 --threads 2 --duration-sec 3 \
    --algorithm "$algo" --hot-products 1 --hot-access-prob 0.9 \
    --arbitration-mode per_object --write-ratio 1.0
done
```

Historical result matrices remain checked in under `results/`. Their original batch
runner and parser scripts are not present in this repository snapshot. Do not invoke
or claim an automated regeneration path until replacement tooling is added.

## Current Key Artifacts

- Final paper: `paper.md`
- Chinese final paper: `paper_zh.md`
- Handoff: `HANDOFF.md`
- Plan/status checklist: `PROJECT_PLAN_STATUS.md`
- Reading guide: `READING_GUIDE.md`
- Corrected previous prototype bundle: `prev_project/`
- Final matrix: `results/final_focused_matrix/`
- Sold-counter comparison: `results/final_sold_counter_comparison/`
- Final convergence summary: `results/final_project_convergence_summary.md`
- Audit report and patch plan: `results/final_audit_bug_report.md`

## Latency Sampling Note

Use `--latency-sampling bounded_rotation` for the mutex-protected bounded rotating buffer. The historical CLI value `reservoir` remains accepted as an alias, but JSON metadata reports the honest canonical name `bounded_rotation`. This is not statistically uniform Algorithm R reservoir sampling. Treat p95/p99 values as prototype-relative tail indicators under identical collection policy, not as unbiased estimates of the full latency distribution. The compatibility keys `latency_us_p50/p95/p99` and `tx_latency_us_p50/p95/p99` report the same sampler-backed values.

Counter schema version 2 reports `logical_tx`, `occ_attempts`, `occ_failed_attempts`, and `final_abort_tx`. The legacy JSON keys `attempted_tx` and `aborted_tx` remain as aliases for `logical_tx` and `final_abort_tx`. Do not silently compare schema-version-2 `abort_rate` against historical rows that used the mixed retry-attempt denominator.

## VM ENV SETTING
`/docs/ENVIRONMENT_AND_REPRODUCTION_GUIDE.md`

# Final Project Convergence Summary

Date: 2026-05-28 UTC

## 1. Phase Structure Consistency

Consistent. The current report structure is:

- Stage 0: Preliminary prototype and problem origin.
- Phase 1: Two-VM Soft-RoCE feasibility and trust boundary.
- Phase 2: Local RDMA-style DSM/OCC protocol prototype.
- Phase 3: Contention behavior and static hybrid arbitration.
- Phase 4: Scalable arbitration queues and cleanup.
- Phase 5: Bounded latency sampling and adaptive routing.
- Future Phase 6: Project-level two-node RDMA wrapper validation.
- Future Phase 7: Two-node DSM transaction over RDMA verbs.

Old directories whose names include `phase3` are preserved for reproducibility, but the final narrative treats the Soft-RoCE transport validation as Phase 1 evidence.

## 2. Claim Boundary Consistency

Consistent. The project has no hardware RDMA NIC and does not claim hardware RDMA latency, throughput, RNIC offload, PCIe behavior, switch behavior, RoCE congestion-control behavior, bare-metal scalability, project-level two-node DSM-over-verbs throughput, or project-level remote CAS correctness.

Soft-RoCE is used as verbs compatibility and transport diagnostic evidence. Local DSM/OCC benchmarks are used as prototype-relative protocol evidence.

## 3. Current-cycle Experiment Completion

Complete for the requested reduced scope:

- Phase 2 parsed data rows: 272.
- Phase 3 transport validation data rows: 36 total, including 32 rerun rows and 4 legacy `/stat` rows.
- Phase 4b cleanup/isolation validation: complete.
- Phase 5 adaptive calibration: complete.
- Phase 5 phase-change approximation: complete.
- Reduced final focused matrix: complete with 540 data rows.
- Controlled sold-counter comparison: complete with 48 data rows.

## 4. Sold-counter Comparison

Complete. Results are in `results/final_sold_counter_comparison/`.

The comparison uses:

- Workloads: `mixed_hot4_write50`, `high_hot16_write100`
- Sold-counter modes: `global`, `per_product`
- Algorithms: `hybrid_static_arbitration_occ_per_shard_8`, `hybrid_adaptive_arbitration_occ_per_shard_8`
- Threads: 2, 4
- Duration: 10 seconds
- Repetitions: 3
- CLI latency mode: `reservoir`
- Sample size: 10000
- Adaptive defaults: `routing_margin_us=5`, `cost_window_ms=500`, `hot_shards=8`

All 48 rows are correctness-clean. The conclusion is a data-model lesson: a global shared metadata object can mask sharded arbitration benefits; per-product counters isolate arbitration queue behavior better, but the performance effect is workload- and thread-count-dependent.

## 5. Correctness-clean Status

All current final rows are correctness-clean:

- `results/final_focused_matrix/summary.csv`: invariant violations 0, duplicate commits 0.
- `results/final_sold_counter_comparison/summary.csv`: invariant violations 0, duplicate commits 0.

Important audit caveat: `duplicate_commit_count` is currently a dead counter in code, so the stronger correctness claim should rely on implemented invariants such as stock/sold consistency unless the duplicate detector is fixed and validated.

## 6. Future Phase Exclusion

Future Phase 6 and Phase 7 are clearly excluded from current claims:

- Phase 6 must validate the project RDMA wrapper across two nodes, including READ/WRITE/CAS and CQ completion.
- Phase 7 must implement and validate minimal two-node DSM/OCC transactions over verbs.

Neither phase was implemented or run in this cycle.

## 7. Optional Remaining Work

- Translate and integrate `paper.md` into a full final Chinese `report.md` if required.
- Run longer-duration or higher-repetition validation only if explicitly requested.
- Apply audit code fixes after explicit authorization or `APPLY_CODE_FIXES=1`.

## 8. Future Work

- Implement project-level two-node RDMA wrapper validation.
- Implement two-node DSM transaction over RDMA verbs.
- Replace bounded rotating latency sampling with Algorithm R or rename the CLI/reporting mode.
- Repair dead counters and the mixed `attempted_tx` denominator before making stronger correctness/statistical claims.
- Add production concerns such as fairness, crash recovery, durability, and stronger workload coverage only after the current evidence boundary is preserved.

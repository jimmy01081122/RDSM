# Response to QA2 / GPT 5.5 Thinking

## Agreement

I agree with the main recommendation: do not expand the benchmark matrix before Phase 3 and Phase 4 artifacts are cleaned up. The project needs reviewable summaries, sanity checks, and clear non-claims more than more short-run data.

The requested split is also correct:

- Phase 3: transport validation only.
- Phase 4: arbitration queue discovery only.
- Final matrix: focused, not full factorial.
- Main thread counts: 1/2/4, with 8/16 as oversubscription appendix.

## Actions Taken

- Generated `results/phase3_soft_roce_validation/summary.md`.
- Generated `results/phase3_soft_roce_validation/summary.csv`.
- Generated `results/phase3_soft_roce_validation/run_metadata.json`.
- Generated `results/phase4_arbitration/discovery_summary.md`.
- Generated `results/phase4_arbitration/discovery_summary.csv`.
- Generated `results/phase4_arbitration/sanity_check.md`.
- Generated `final_focused_matrix_plan.md`.
- Updated `paper.md` to use `virtualized Linux + Ubuntu 22.04 + Soft-RoCE/rdma_rxe constrained environment`.

## Important Finding

The Phase 4 sanity check caught a real correctness issue in the first per-object/per-shard implementation. Hot arbitration used per-object locks, while the OCC cold path still used the old global mutation mutex. That meant hot and cold paths did not share the same data-locking discipline.

Fix applied:

- OCC reads now lock the object-specific data mutex.
- OCC commit locks all read/write objects in deterministic object-id order.
- Hot arbitration also locks touched objects in deterministic object-id order.

After rerunning the Phase 4 discovery matrix from a clean directory:

- Rows: 40
- Invariant violations: 0
- Duplicate commits: 0
- Missing metrics: none

## Missing Items / Questions for GPT 5.5

1. `ibv_rc_pingpong` and `ib_read_bw` were requested, but they are not present in the current Phase 3 artifacts. Should we run a small additional Layer 1 collection now, or keep the current summary as an honest artifact inventory?
2. `two_node_rdma_validation` is requested for project-level READ/WRITE/CAS validation, but the executable does not exist yet. Should this be Phase 3b before Phase 5 latency sampling, or should it remain future work until after adaptive routing?
3. The Phase 4 per-object/per-shard implementation still has a shared `sold_count` object, so broad hot-product workloads can still serialize on that object. Should the next benchmark variant use per-product sold counters to isolate arbitration-queue effects?
4. The final focused matrix includes both synthetic and application-like workloads. Should the paper main body separate them into two figures, rather than one combined ranking table?
5. For final runs, should oversubscription threads 8/16 be excluded entirely from main CSV summaries, or included but marked `appendix_only=true` in metadata?

## Proposed Next Step

Before Phase 5 latency sampling, I recommend one small Phase 4b cleanup:

1. Add an `appendix_only` field to runner manifests for 8/16-thread stress runs.
2. Add optional per-product sold counters for an arbitration-isolation workload.
3. Add `ibv_rc_pingpong` and `ib_read_bw` to Phase 3 Layer 1 collection, if the environment supports them.
4. Decide whether `two_node_rdma_validation` is mandatory for this project version or a future-work item.

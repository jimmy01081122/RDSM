# Phase 4b Artifact Verification Summary

Verification date: 2026-05-28 UTC

Scope: verify existing Phase 4b cleanup/isolation artifacts before Phase 5 work. This verification does not create a new performance claim.

| Check | Result | Evidence |
|---|---|---|
| All Phase 4b rows stock/sold invariant-clean | PASS | `results/phase4b_cleanup/summary.csv` has 24 historical rows with `invariant_violation_count=0`. Their pre-fix `duplicate_commit_count=0` fields cannot support a historical no-duplicate-commit claim. |
| Invariant violations are zero | PASS | Total invariant violations across summary rows: 0. |
| Duplicate commits are zero | PASS | Total duplicate commit count across summary rows: 0. |
| Both sold counter modes present | PASS | `sold_counter_mode` contains `global` and `per_product`. |
| Required workload `mixed_hot4_write50` present | PASS | Present in both raw summary and Phase 4b aggregate summary. |
| Required workload `high_hot16_write100` present | PASS | Present in both raw summary and Phase 4b aggregate summary. |
| `appendix_only` present in summaries/manifests | PASS | Present in `summary.csv`, `phase4b_cleanup_summary.csv`, and sampled run manifests. |
| `appendix_reason` present in summaries/manifests | PASS | Present in `summary.csv` and run manifests; aggregate summary keeps the appendix flag but not the reason text. |
| 8-thread rows marked appendix-only | PASS | All `thread_count=8` manifest/summary rows have `appendix_only=true` and reason `oversubscription_threads_exceed_exposed_cores`. |
| 1/2/4-thread rows non-appendix | PASS_WITH_NOTE | Phase 4b only contains 4-thread and 8-thread rows; all 4-thread rows are non-appendix. There are no 1-thread or 2-thread Phase 4b rows to check. |
| Queue wait metrics parse correctly | PASS | `server_queue_wait_us_p50/p95/p99/max` are present in `summary.csv`; aggregate summary reports p99 queue wait. |
| Queue length metrics parse correctly | PASS | `queue_length_p50/p95/p99` are present in `summary.csv`; aggregate summary reports p95 queue length. |
| Service time metrics parse correctly | PASS | `service_time_us_p50/p95/p99/max` are present in `summary.csv`; aggregate summary reports p95 service time. |
| `paper.md` states Phase 4b is cleanup/isolation only | PASS | Section `7.1 Phase 4b: Cleanup and Isolation Validation` says it is not a new performance claim. |

## Interpretation

Phase 4b artifacts are suitable as cleanup/isolation evidence for the sold-counter modeling issue. They should remain separate from final performance ranking because the current rows are short discovery runs and include appendix-only 8-thread oversubscription rows.

## Notes for Phase 5

- Main-body Phase 5 rows should use `threads=1,2,4`.
- Phase 4b does not contain 1-thread or 2-thread rows; this is acceptable because Phase 4b was intentionally limited and should not be treated as the final matrix.
- The aggregate `phase4b_cleanup_summary.csv` includes `appendix_only` but not `appendix_reason`; the detailed `summary.csv` and manifests preserve the reason.

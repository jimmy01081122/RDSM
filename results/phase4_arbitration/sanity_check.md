# Phase 4 Sanity Check

| Check | Result | Evidence |
|---|---|---|
| 1. global/per_object/per_shard 是否都有結果？ | PASS | modes=global, per_object, per_shard |
| 2. stock/sold invariant 是否全數通過？ | PASS | invariant violations=0 |
| 3. high-hot workloads 的 hot_path_ratio 是否高？ | PASS | high-hot mean hot_path_ratio=0.976 |
| 4. low-contention/read-heavy workloads 的 hot_path_ratio 是否低？ | PASS | low_uniform_read95 mean hot_path_ratio=0.029 |
| 5. queue_wait、queue_length、service_time quantiles 是否有意義？ | PASS | missing metrics=none; p99 wait/service/queue length are populated |
| 6. per_shard 的 4/8/16 是否能看到差異？ | PASS | shard values=[4, 8, 16]; discovery rows expose different queue/throughput values, but not final claims |
| 7. 是否有 deadlock、timeout、missing metrics？ | PASS_WITH_NOTE | all 40 runs completed; no missing metrics; previous lock bug was fixed before this summary. Pre-fix `duplicate_commit_count=0` fields cannot support a historical no-duplicate-commit claim. |

## Important Caveat

These sanity checks validate instrumentation and stock/sold invariants for a short historical discovery matrix. They do not establish final performance ranking or a historical no-duplicate-commit claim.

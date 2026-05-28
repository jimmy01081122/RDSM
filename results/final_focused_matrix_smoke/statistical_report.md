# Final Focused Matrix Statistical Report

- Duration per run: 1 sec
- Target repetitions: 1
- Latency sampling mode: reservoir
- Latency sample size: 10000
- Appendix-only filtering rule: final matrix contains only 1/2/4-thread main-body rows; rows with threads > 4 must be appendix-only in any future extension.

| Algorithm | Workload | Threads | Runs | tx/sec mean | tx/sec stddev | tx/sec 95% CI | p99 latency mean us | Clean |
|---|---|---:|---:|---:|---:|---:|---:|---|
| baseline_occ | mixed_hot4_write50 | 1 | 1 | 543062.00 | 0.00 | 0.00 | 2.00 | true |
| hybrid_adaptive_arbitration_occ_per_shard_8 | mixed_hot4_write50 | 1 | 1 | 442362.00 | 0.00 | 0.00 | 2.00 | true |

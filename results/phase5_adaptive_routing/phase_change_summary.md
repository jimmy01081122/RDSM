# Phase 5 Scripted Phase-Change Approximation Summary

Scope: consecutive controlled processes only. This approximates phase changes across runs and does not prove continuous in-process adaptive reaction.

| Scenario | Phase | Workload | tx/sec | Samples | Invariants | Duplicates | Route OCC | Route Arb | Insufficient | Oscillation |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| phase_change_uniform_to_hot_to_uniform | 1 | low_uniform_read95 | 1247544 | 1000 | 0 | 0 | 1247544 | 0 | 0 | 0 |
| phase_change_uniform_to_hot_to_uniform | 2 | mixed_hot4_write50 | 413696 | 1000 | 0 | 0 | 413354 | 342 | 342 | 4 |
| phase_change_uniform_to_hot_to_uniform | 3 | low_uniform_read95 | 1266177 | 1000 | 0 | 0 | 1266177 | 0 | 0 | 0 |
| phase_change_hot1_to_hot16 | 1 | high_hot1_write100 | 365321 | 1000 | 0 | 0 | 365223 | 98 | 98 | 1 |
| phase_change_hot1_to_hot16 | 2 | high_hot16_write100 | 306285 | 1000 | 0 | 0 | 305884 | 401 | 401 | 4 |
| phase_change_read_heavy_to_write_heavy | 1 | low_uniform_read95 | 1159276 | 1000 | 0 | 0 | 1159276 | 0 | 0 | 0 |
| phase_change_read_heavy_to_write_heavy | 2 | high_hot16_write100 | 361642 | 1000 | 0 | 0 | 361241 | 401 | 401 | 4 |
| phase_change_zipf_low_to_zipf_high | 1 | zipf90_write50 | 282093 | 1000 | 0 | 0 | 282077 | 16 | 16 | 2 |
| phase_change_zipf_low_to_zipf_high | 2 | zipf99_write100 | 220161 | 1000 | 0 | 0 | 220120 | 41 | 41 | 3 |

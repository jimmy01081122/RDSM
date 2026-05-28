# Phase 5 Adaptive Routing Calibration Summary

Scope: small calibration matrix only. These rows select a default routing policy; they should not be used as main-paper factorial figures.

- Runs: 54
- Correctness-clean: True
- Selected default: routing_margin_us=5.0, cost_window_ms=500, min_samples_before_adapt=100, adaptive_object_scope=shard, hot_shards=8

## Calibration Table

| Margin us | Window ms | Workload | Runs | Clean | tx/sec mean | p99 latency us | Arb ratio | Oscillation | Insufficient samples |
|---:|---:|---|---:|---|---:|---:|---:|---:|---:|
| 5.0 | 100 | high_hot16_write100 | 2 | true | 243526 | 144.50 | 0.000657 | 8.00 | 800.50 |
| 5.0 | 100 | low_uniform_read95 | 2 | true | 988553 | 3.50 | 0.000000 | 0.00 | 0.00 |
| 5.0 | 100 | mixed_hot4_write50 | 2 | true | 349717 | 91.00 | 0.000212 | 4.00 | 370.00 |
| 5.0 | 250 | high_hot16_write100 | 2 | true | 243922 | 133.00 | 0.000657 | 8.00 | 801.50 |
| 5.0 | 250 | low_uniform_read95 | 2 | true | 1033334 | 3.00 | 0.000000 | 0.00 | 0.00 |
| 5.0 | 250 | mixed_hot4_write50 | 2 | true | 353181 | 93.00 | 0.000211 | 4.00 | 372.50 |
| 5.0 | 500 | high_hot16_write100 | 2 | true | 241118 | 109.00 | 0.000665 | 8.00 | 801.00 |
| 5.0 | 500 | low_uniform_read95 | 2 | true | 1034278 | 3.00 | 0.000000 | 0.00 | 0.00 |
| 5.0 | 500 | mixed_hot4_write50 | 2 | true | 356474 | 34.50 | 0.000215 | 4.00 | 383.50 |
| 10.0 | 100 | high_hot16_write100 | 2 | true | 243055 | 135.00 | 0.000659 | 8.00 | 801.00 |
| 10.0 | 100 | low_uniform_read95 | 2 | true | 1013785 | 3.00 | 0.000000 | 0.00 | 0.00 |
| 10.0 | 100 | mixed_hot4_write50 | 2 | true | 356728 | 90.00 | 0.000209 | 4.00 | 373.00 |
| 10.0 | 250 | high_hot16_write100 | 2 | true | 240413 | 128.50 | 0.000666 | 8.00 | 800.50 |
| 10.0 | 250 | low_uniform_read95 | 2 | true | 1047600 | 3.00 | 0.000000 | 0.00 | 0.00 |
| 10.0 | 250 | mixed_hot4_write50 | 2 | true | 347070 | 89.00 | 0.000212 | 4.50 | 367.00 |
| 10.0 | 500 | high_hot16_write100 | 2 | true | 236524 | 125.50 | 0.000678 | 8.00 | 801.00 |
| 10.0 | 500 | low_uniform_read95 | 2 | true | 1037223 | 3.00 | 0.000000 | 0.00 | 0.00 |
| 10.0 | 500 | mixed_hot4_write50 | 2 | true | 352386 | 59.00 | 0.000215 | 4.00 | 379.50 |
| 20.0 | 100 | high_hot16_write100 | 2 | true | 242599 | 136.00 | 0.000660 | 8.00 | 801.00 |
| 20.0 | 100 | low_uniform_read95 | 2 | true | 1017013 | 2.50 | 0.000000 | 0.50 | 1.00 |
| 20.0 | 100 | mixed_hot4_write50 | 2 | true | 356773 | 58.50 | 0.000205 | 4.00 | 366.00 |
| 20.0 | 250 | high_hot16_write100 | 2 | true | 242829 | 103.50 | 0.000660 | 8.00 | 801.50 |
| 20.0 | 250 | low_uniform_read95 | 2 | true | 1054268 | 3.00 | 0.000000 | 0.00 | 0.00 |
| 20.0 | 250 | mixed_hot4_write50 | 2 | true | 347715 | 89.50 | 0.000213 | 4.00 | 370.50 |
| 20.0 | 500 | high_hot16_write100 | 2 | true | 240268 | 248.00 | 0.000666 | 8.00 | 800.00 |
| 20.0 | 500 | low_uniform_read95 | 2 | true | 1043855 | 3.50 | 0.000000 | 0.00 | 0.00 |
| 20.0 | 500 | mixed_hot4_write50 | 2 | true | 356199 | 71.50 | 0.000212 | 4.00 | 377.00 |

## Answers

1. Correctness: all calibration rows are correctness-clean when invariant violations and duplicate commits are both zero.
2. Low-contention arbitration: all selected candidates keep `low_uniform_read95` arbitration near zero.
3. Hot workload routing: the prototype routes a small nonzero fraction of known-hot transactions during cold-start/insufficient-sample periods; this is conservative and should be discussed as a limitation.
4. p99 latency: compare p99 as prototype-relative latency only; the selected default has the lowest aggregate score under the current heuristic.
5. Oscillation: oscillation is low in low-uniform and bounded in hot workloads, but not zero.
6. Final default: use the selected default above unless a manual policy is chosen after review.

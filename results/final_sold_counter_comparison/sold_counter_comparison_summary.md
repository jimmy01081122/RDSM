# Sold Counter Controlled Comparison

This comparison separates the application-level shared `sold_count` metadata bottleneck from arbitration-queue behavior. It is not a universal ranking table.

- Rows: 48
- Duration per run: 10 sec
- Repetitions: 3
- Latency sampling mode in CLI: reservoir; implementation is a bounded rotating sample, sample size 10000
- Adaptive defaults: routing_margin_us=5, cost_window_ms=500, hot_shards=8
- Correctness-clean: True

## Summary By Configuration

| Workload | Sold counter | Algorithm | Threads | Runs | TX/sec mean | TX/sec stddev | tx p99 us mean | queue wait p99 us mean | lock fails mean | validation fails mean |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| high_hot16_write100 | global | hybrid_adaptive_arbitration_occ_per_shard_8 | 2 | 3 | 178203.57 | 2387.91 | 338.00 | 65.33 | 0.00 | 18136.67 |
| high_hot16_write100 | global | hybrid_adaptive_arbitration_occ_per_shard_8 | 4 | 3 | 158060.03 | 16721.02 | 621.33 | 386.33 | 0.00 | 22379.33 |
| high_hot16_write100 | global | hybrid_static_arbitration_occ_per_shard_8 | 2 | 3 | 366415.93 | 12410.49 | 119.33 | 1.00 | 0.00 | 0.00 |
| high_hot16_write100 | global | hybrid_static_arbitration_occ_per_shard_8 | 4 | 3 | 421934.90 | 59328.37 | 134.00 | 1.00 | 0.00 | 0.00 |
| high_hot16_write100 | per_product | hybrid_adaptive_arbitration_occ_per_shard_8 | 2 | 3 | 214107.23 | 5766.82 | 158.33 | 1.00 | 0.00 | 12520.33 |
| high_hot16_write100 | per_product | hybrid_adaptive_arbitration_occ_per_shard_8 | 4 | 3 | 187267.73 | 1276.13 | 474.33 | 338.67 | 0.00 | 23268.67 |
| high_hot16_write100 | per_product | hybrid_static_arbitration_occ_per_shard_8 | 2 | 3 | 513390.13 | 8852.63 | 4.67 | 1.00 | 0.00 | 0.00 |
| high_hot16_write100 | per_product | hybrid_static_arbitration_occ_per_shard_8 | 4 | 3 | 466297.63 | 11612.75 | 133.67 | 42.00 | 0.00 | 0.00 |
| mixed_hot4_write50 | global | hybrid_adaptive_arbitration_occ_per_shard_8 | 2 | 3 | 282577.43 | 5230.94 | 65.67 | 108.67 | 0.00 | 14783.00 |
| mixed_hot4_write50 | global | hybrid_adaptive_arbitration_occ_per_shard_8 | 4 | 3 | 276879.20 | 35228.64 | 24.67 | 1012.67 | 0.00 | 18464.67 |
| mixed_hot4_write50 | global | hybrid_static_arbitration_occ_per_shard_8 | 2 | 3 | 344135.17 | 14727.96 | 36.00 | 1.00 | 0.00 | 1054.67 |
| mixed_hot4_write50 | global | hybrid_static_arbitration_occ_per_shard_8 | 4 | 3 | 437698.37 | 68840.76 | 274.00 | 42.33 | 0.00 | 841.67 |
| mixed_hot4_write50 | per_product | hybrid_adaptive_arbitration_occ_per_shard_8 | 2 | 3 | 311038.20 | 16390.73 | 105.00 | 43.33 | 0.00 | 11506.67 |
| mixed_hot4_write50 | per_product | hybrid_adaptive_arbitration_occ_per_shard_8 | 4 | 3 | 246638.13 | 16067.82 | 306.33 | 374.67 | 0.00 | 21275.67 |
| mixed_hot4_write50 | per_product | hybrid_static_arbitration_occ_per_shard_8 | 2 | 3 | 467513.83 | 18080.29 | 3.33 | 1.00 | 0.00 | 132.33 |
| mixed_hot4_write50 | per_product | hybrid_static_arbitration_occ_per_shard_8 | 4 | 3 | 405027.77 | 12603.46 | 93.67 | 78.67 | 0.00 | 247.00 |

## Interpretation

- `global` sold counter represents an application-level shared metadata bottleneck. Every successful write transaction updates the same metadata object, so even per-object or per-shard arbitration can still be forced through another shared object.
- `per_product` sold counters isolate arbitration queue behavior by removing that extra shared metadata object from every transaction.
- Per-object/per-shard arbitration only helps when the data model does not force every transaction through a separate shared object. If `global` underperforms or shows worse tails, the result should be read as a data-model contention effect, not as a failure of sharded queueing alone.

## Per-Product vs Global Delta

| Workload | Algorithm | Threads | Per-product/global TX/sec ratio | p99 latency delta us | queue wait p99 delta us |
|---|---|---:|---:|---:|---:|
| high_hot16_write100 | hybrid_adaptive_arbitration_occ_per_shard_8 | 2 | 1.201 | -179.67 | -64.33 |
| high_hot16_write100 | hybrid_adaptive_arbitration_occ_per_shard_8 | 4 | 1.185 | -147.00 | -47.67 |
| high_hot16_write100 | hybrid_static_arbitration_occ_per_shard_8 | 2 | 1.401 | -114.67 | 0.00 |
| high_hot16_write100 | hybrid_static_arbitration_occ_per_shard_8 | 4 | 1.105 | -0.33 | 41.00 |
| mixed_hot4_write50 | hybrid_adaptive_arbitration_occ_per_shard_8 | 2 | 1.101 | 39.33 | -65.33 |
| mixed_hot4_write50 | hybrid_adaptive_arbitration_occ_per_shard_8 | 4 | 0.891 | 281.67 | -638.00 |
| mixed_hot4_write50 | hybrid_static_arbitration_occ_per_shard_8 | 2 | 1.359 | -32.67 | 0.00 |
| mixed_hot4_write50 | hybrid_static_arbitration_occ_per_shard_8 | 4 | 0.925 | -180.33 | 36.33 |

# Phase 4 Arbitration Discovery Summary

This is a short discovery/smoke dataset. It must not be used as a final performance claim.

| Workload | Mode | Shards | Runs | Threads | Mean tx/sec | Hot path | p99 queue wait us | p95 queue length | p95 service us | Invariant violations | Duplicate commits |
|---|---|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|
| high_hot16_write100 | global | 1 | 2 | 8;16 | 1818104 | 1.000 | 1.00 | 11.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | per_object | 1 | 2 | 8;16 | 944108 | 1.000 | 432.50 | 4.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | per_shard | 4 | 2 | 8;16 | 1453542 | 1.000 | 1.00 | 11.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | per_shard | 8 | 2 | 8;16 | 1100688 | 1.000 | 210.50 | 9.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | per_shard | 16 | 2 | 8;16 | 609688 | 1.000 | 485.00 | 4.00 | 1.00 | 0 | 0 |
| high_hot1_write100 | global | 1 | 2 | 8;16 | 1832659 | 0.950 | 1.00 | 11.00 | 1.00 | 0 | 0 |
| high_hot1_write100 | per_object | 1 | 2 | 8;16 | 1790364 | 0.950 | 1.00 | 11.00 | 1.00 | 0 | 0 |
| high_hot1_write100 | per_shard | 4 | 2 | 8;16 | 1833978 | 0.950 | 1.00 | 11.00 | 1.00 | 0 | 0 |
| high_hot1_write100 | per_shard | 8 | 2 | 8;16 | 1698969 | 0.955 | 1.00 | 11.00 | 1.00 | 0 | 0 |
| high_hot1_write100 | per_shard | 16 | 2 | 8;16 | 1827110 | 0.950 | 1.00 | 11.00 | 1.00 | 0 | 0 |
| low_uniform_read95 | global | 1 | 2 | 8;16 | 2372008 | 0.035 | 1341.00 | 11.00 | 1.00 | 0 | 0 |
| low_uniform_read95 | per_object | 1 | 2 | 8;16 | 1453632 | 0.010 | 3212.50 | 7.50 | 42.00 | 0 | 0 |
| low_uniform_read95 | per_shard | 4 | 2 | 8;16 | 2123180 | 0.065 | 1264.00 | 11.00 | 1.00 | 0 | 0 |
| low_uniform_read95 | per_shard | 8 | 2 | 8;16 | 1536679 | 0.010 | 3274.50 | 7.50 | 32.00 | 0 | 0 |
| low_uniform_read95 | per_shard | 16 | 2 | 8;16 | 1611665 | 0.025 | 2624.00 | 11.00 | 33.50 | 0 | 0 |
| zipf99_write100 | global | 1 | 2 | 8;16 | 421441 | 0.400 | 2252.00 | 11.00 | 1.00 | 0 | 0 |
| zipf99_write100 | per_object | 1 | 2 | 8;16 | 347460 | 0.710 | 738.50 | 11.00 | 1.00 | 0 | 0 |
| zipf99_write100 | per_shard | 4 | 2 | 8;16 | 426922 | 0.600 | 488.50 | 11.00 | 1.00 | 0 | 0 |
| zipf99_write100 | per_shard | 8 | 2 | 8;16 | 419288 | 0.590 | 362.50 | 11.00 | 1.00 | 0 | 0 |
| zipf99_write100 | per_shard | 16 | 2 | 8;16 | 413642 | 0.730 | 684.00 | 11.00 | 1.00 | 0 | 0 |

## Notes

- The runs cover `global`, `per_object`, and `per_shard` arbitration modes.
- Per-shard was sampled at 4, 8, and 16 shards.
- The matrix uses only 1-second measurement and one repetition, so it is useful for sanity checking only.

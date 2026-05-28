# Phase 4b Cleanup Summary

Phase 4b is cleanup/isolation validation, not a new performance claim. It compares the original shared global sold counter with per-product sold counters for two focused workloads only.

- Rows: 24
- Invariant violations: 0
- Duplicate commits: 0

| Workload | Sold counter | Mode | Shards | Appendix | Runs | Mean tx/sec | Hot path | p99 queue wait us | p95 queue length | p95 service us | Invariant violations | Duplicate commits |
|---|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| high_hot16_write100 | global | global | 1 | false | 1 | 889347 | 1.000 | 96.00 | 3.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | global | global | 1 | true | 1 | 1888816 | 1.000 | 1.00 | 7.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | global | per_object | 1 | false | 1 | 1804903 | 1.000 | 100.00 | 2.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | global | per_object | 1 | true | 1 | 885015 | 1.000 | 104.00 | 4.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | global | per_shard | 8 | false | 1 | 442809 | 1.000 | 211.00 | 3.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | global | per_shard | 8 | true | 1 | 1111130 | 1.000 | 72.00 | 7.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | per_product | global | 1 | false | 1 | 862209 | 1.000 | 99.00 | 3.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | per_product | global | 1 | true | 1 | 1621258 | 1.000 | 1.00 | 7.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | per_product | per_object | 1 | false | 1 | 1148439 | 1.000 | 91.00 | 1.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | per_product | per_object | 1 | true | 1 | 1353337 | 1.000 | 164.00 | 3.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | per_product | per_shard | 8 | false | 1 | 1073643 | 1.000 | 67.00 | 2.00 | 1.00 | 0 | 0 |
| high_hot16_write100 | per_product | per_shard | 8 | true | 1 | 1657424 | 1.000 | 91.00 | 7.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | global | global | 1 | false | 1 | 621017 | 0.780 | 153.00 | 3.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | global | global | 1 | true | 1 | 1567496 | 0.770 | 1.00 | 7.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | global | per_object | 1 | false | 1 | 654058 | 0.800 | 189.00 | 3.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | global | per_object | 1 | true | 1 | 1438011 | 0.790 | 1.00 | 7.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | global | per_shard | 8 | false | 1 | 452871 | 0.810 | 219.00 | 3.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | global | per_shard | 8 | true | 1 | 1161866 | 0.770 | 58.00 | 7.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | per_product | global | 1 | false | 1 | 729146 | 0.760 | 110.00 | 3.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | per_product | global | 1 | true | 1 | 1445582 | 0.770 | 1.00 | 7.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | per_product | per_object | 1 | false | 1 | 898881 | 0.770 | 106.00 | 2.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | per_product | per_object | 1 | true | 1 | 1140781 | 0.770 | 108.00 | 7.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | per_product | per_shard | 8 | false | 1 | 905487 | 0.770 | 97.00 | 2.00 | 1.00 | 0 | 0 |
| mixed_hot4_write50 | per_product | per_shard | 8 | true | 1 | 1182734 | 0.770 | 111.00 | 7.00 | 1.00 | 0 | 0 |

## Interpretation Boundary

- `global` sold counter represents an application-level global metadata bottleneck.
- `per_product` sold counters isolate arbitration queue behavior by removing the shared sold-count object from the write set.
- Threads greater than 4 are marked appendix-only because the environment exposes 4 vCPU/core contexts.
- These runs use short discovery settings and should not be used as final performance ranking.

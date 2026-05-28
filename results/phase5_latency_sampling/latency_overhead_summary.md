# Phase 5 Latency Sampling Overhead Summary

Scope: 1-second, 2-thread smoke check only. These rows validate measurement plumbing and rough overhead; they are not final performance results.

| Workload | Sampling | Sample size | Samples | tx/sec | Delta vs off | Abort rate | Retry/commit | Max RSS KB | Invariants | Duplicates |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| low_uniform_read95 | off | 0 | 0 | 1824482 | 0.00% | 0.0000 | 0.0000 | 5164 | 0 | 0 |
| low_uniform_read95 | reservoir | 5000 | 5000 | 1557155 | -14.65% | 0.0000 | 0.0000 | 7604 | 0 | 0 |
| low_uniform_read95 | full | 0 | 968070 | 968070 | -46.94% | 0.0000 | 0.0000 | 505400 | 0 | 0 |
| mixed_hot4_write50 | off | 0 | 0 | 836012 | 0.00% | 0.0000 | 0.0000 | 15912 | 0 | 0 |
| mixed_hot4_write50 | reservoir | 5000 | 5000 | 790490 | -5.45% | 0.0000 | 0.0000 | 20700 | 0 | 0 |
| mixed_hot4_write50 | full | 0 | 549188 | 549188 | -34.31% | 0.0000 | 0.0000 | 302080 | 0 | 0 |
| high_hot16_write100 | off | 0 | 0 | 940628 | 0.00% | 0.0000 | 0.0000 | 15784 | 0 | 0 |
| high_hot16_write100 | reservoir | 5000 | 5000 | 914307 | -2.80% | 0.0000 | 0.0000 | 20496 | 0 | 0 |
| high_hot16_write100 | full | 0 | 644352 | 644352 | -31.50% | 0.0000 | 0.0000 | 351264 | 0 | 0 |

## Notes

- Default final-use mode remains `reservoir`; `full` sampling is for short smoke/debug runs only because it grows with transaction count and can exhaust memory.
- The reservoir sample size was reduced to 5,000 for this smoke check to avoid OOM risk while validating the path.
- Raw full-sampling CSV files are intentionally not checked in because they can reach hundreds of MB per second.
- All outliers are kept; aborted transaction latency is reported separately from committed transaction latency in run JSON.
- This smoke check shows measurable overhead even for reservoir sampling; final claims must report measurement overhead.

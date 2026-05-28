# Phase 5 Adaptive Routing Smoke Summary

Scope: 1-second, 2-thread smoke only. These rows validate minimal adaptive-routing plumbing and correctness; they are not final performance claims.

| Workload | tx/sec | Samples | Invariants | Duplicates | Route OCC | Route Arb | Insufficient | Bad-route proxy | Est OCC p95 us | Est Arb p95 us | Decision p95 us | Oscillation |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| low_uniform_read95 | 1180333 | 5000 | 0 | 0 | 1180333 | 0 | 0 | 0 | 0.0 | 0.0 | 0.0 | 0 |
| mixed_hot4_write50 | 424364 | 5000 | 0 | 0 | 424023 | 341 | 341 | 0 | 2.0 | 3.0 | 0.0 | 4 |
| high_hot16_write100 | 305286 | 5000 | 0 | 0 | 304885 | 401 | 401 | 0 | 2.0 | 3.0 | 0.0 | 4 |

## Notes

- The adaptive router is a minimal prototype, not a final calibrated policy.
- Cold-start decisions fall back to static hybrid arbitration for known-hot objects and are counted separately.
- Latency sampling uses bounded reservoir mode with sample size 5,000 to avoid OOM.

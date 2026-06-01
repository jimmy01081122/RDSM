# Final Focused Matrix Summary

This is a reduced focused final matrix, not a publication-grade full evaluation.

Synthetic and application-like workloads are reported separately. This file intentionally avoids a universal ranking table across unrelated workload families.

- Rows: 540
- Invariant violations: 0
- Historical `duplicate_commit_count` fields: 0; these pre-fix rows cannot support a no-duplicate-commit claim
- Duration per run: 10 sec
- Repetitions: 3
- Historical latency sampling CLI alias: `reservoir`; current canonical name: `bounded_rotation`, sample size 10000

## Synthetic Workloads

- Rows: 324
| Workload | Algorithms | Threads | Runs | Stock/sold invariant-clean |
|---|---:|---|---:|---|
| high_hot16_write100 | 6 | 1,2,4 | 54 | true |
| high_hot1_write100 | 6 | 1,2,4 | 54 | true |
| low_uniform_read95 | 6 | 1,2,4 | 54 | true |
| mixed_hot4_write50 | 6 | 1,2,4 | 54 | true |
| mixed_uniform_write20 | 6 | 1,2,4 | 54 | true |
| zipf99_write100 | 6 | 1,2,4 | 54 | true |

## Application-like Workloads

- Rows: 216
| Workload | Algorithms | Threads | Runs | Stock/sold invariant-clean |
|---|---:|---|---:|---|
| ad_budget_read_heavy_dashboard | 6 | 1,2,4 | 54 | true |
| flash_sale_spike | 6 | 1,2,4 | 54 | true |
| long_tail_marketplace_zipf | 6 | 1,2,4 | 54 | true |
| ticket_booking_hot_event | 6 | 1,2,4 | 54 | true |

## Interpretation Boundary

- These rows are prototype-relative local DSM/OCC results.
- They are not hardware RDMA latency, throughput, RNIC offload, PCIe, switch, or bare-metal scalability evidence.
- Adaptive routing should be interpreted through calibration and focused workload behavior, not as a mature production policy.
- These rows were generated before the scoped duplicate-application detector was activated. Post-fix CTest and smoke runs validate that detector separately.

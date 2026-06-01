# Final Focused Matrix Summary

This is a reduced focused final matrix, not a publication-grade full evaluation.

Synthetic and application-like workloads are reported separately. This file intentionally avoids a universal ranking table across unrelated workload families.

- Rows: 2
- Invariant violations: 0
- Historical `duplicate_commit_count` fields: 0; these pre-fix rows cannot support a no-duplicate-commit claim
- Duration per run: 1 sec
- Repetitions: 1
- Historical latency sampling CLI alias: `reservoir`; current canonical name: `bounded_rotation`, sample size 10000

## Synthetic Workloads

- Rows: 2
| Workload | Algorithms | Threads | Runs | Stock/sold invariant-clean |
|---|---:|---|---:|---|
| mixed_hot4_write50 | 2 | 1 | 2 | true |

## Application-like Workloads

- Rows: 0
| Workload | Algorithms | Threads | Runs | Stock/sold invariant-clean |
|---|---:|---|---:|---|

## Interpretation Boundary

- These rows are prototype-relative local DSM/OCC results.
- They are not hardware RDMA latency, throughput, RNIC offload, PCIe, switch, or bare-metal scalability evidence.
- Adaptive routing should be interpreted through calibration and focused workload behavior, not as a mature production policy.

# Final Focused Matrix Summary

This is a reduced focused final matrix, not a publication-grade full evaluation.

Synthetic and application-like workloads are reported separately. This file intentionally avoids a universal ranking table across unrelated workload families.

- Rows: 2
- Invariant violations: 0
- Duplicate commits: 0
- Duration per run: 1 sec
- Repetitions: 1
- Latency sampling: reservoir, sample size 10000

## Synthetic Workloads

- Rows: 2
| Workload | Algorithms | Threads | Runs | Correctness-clean |
|---|---:|---|---:|---|
| mixed_hot4_write50 | 2 | 1 | 2 | true |

## Application-like Workloads

- Rows: 0
| Workload | Algorithms | Threads | Runs | Correctness-clean |
|---|---:|---|---:|---|

## Interpretation Boundary

- These rows are prototype-relative local DSM/OCC results.
- They are not hardware RDMA latency, throughput, RNIC offload, PCIe, switch, or bare-metal scalability evidence.
- Adaptive routing should be interpreted through calibration and focused workload behavior, not as a mature production policy.

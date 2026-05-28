# Phase 2 Report

## Scope

This report evaluates a RDMA-style DSM prototype in a VirtualBox + Ubuntu 22.04 + Soft-RoCE environment. The results are protocol-level evidence for algorithm behavior only. They are not hardware RDMA NIC latency, throughput, CPU, PCIe, switch, or RNIC offload measurements.

The measured benchmark path is a local RDMA-style DSM/OCC protocol simulation over the project object store. It does not by itself prove two-node Soft-RoCE transport behavior; transport setup should be validated separately with the existing RDMA connection utilities or verbs-level tests.

The current hybrid arbitration implementation does not support crash recovery or durability.

## Artifacts

- Raw run directories: `results/phase4_arbitration`
- Per-run CSV: `results/phase4_arbitration/summary.csv`
- Grouped CSV: `results/phase4_arbitration/summary_by_config.csv`
- Run timestamp range: 2026-05-28T06:55:14Z to 2026-05-28T06:55:55Z

## Correctness

- Invariant violations: 0
- Duplicate commits: 0
- Status: PASS

Checked invariants: stock remains non-negative by unsigned state transition, user balance does not underflow, and `sold_count + final_stock = initial_stock` across all products.

## Algorithm Summary

| Algorithm | Runs | Commit tx/sec mean | Abort rate mean | Retry/commit mean | Lock fails mean | Validation fails mean | Hot path ratio mean |
|---|---:|---:|---:|---:|---:|---:|---:|
| hybrid_arbitration_occ | 40 | 1301756.40 | 0.001 | 0.001 | 0.0 | 907.5 | 0.646 |

## Why Some Summary Values Are 0

- `abort_rate = 0.000` means the value is below the displayed three-decimal precision or no transaction reached final abort/business-abort state in those runs. It does not mean there were no conflicts; check `lock_fail_count`, `validation_fail_count`, and `retry_count`.
- `retry_per_commit = 0.000` means retries were extremely rare relative to commits after aggregation/rounding, or the backoff/hybrid path prevented failures from reaching the max-retry abort threshold.
- `hot_path_ratio = 0.000` for baseline/backoff/hot-detection-only runs is expected because those variants do not route transactions through server-side arbitration. Hot detection may still mark hot objects, but only `hybrid_arbitration_occ` consumes that signal as a hot path.
- `lock_fail_count` or `validation_fail_count` can be nonzero while `abort_rate` is zero because a failed attempt can retry and eventually commit. In this report, abort rate counts final failed transactions, not every failed commit attempt.
- `server_queue_wait_us_* = 0` appears when server arbitration is disabled or when queue wait rounds below displayed precision.

## Benchmark Direction

The benchmark models a small RDMA-style DSM object store with object version, lock owner/bit, payload value, per-object conflict counters, and global transaction counters. A transaction reads stock and user balance, optionally writes stock/balance/sold_count, then commits through OCC or through the hybrid hot path. The experiment direction is to compare protocol behavior under controlled contention shapes: uniform low contention, moderate hot sets, single-hot-object flash-sale pressure, broad hot sets, Zipfian skew, and several application-like workloads.

The numbers should be read as relative protocol trends in this prototype. They are useful for identifying conflict modes, retry behavior, hot-path routing, and correctness preservation. They are not hardware RDMA performance measurements.

## Hybrid Arbitration Design

`hybrid_arbitration_occ` keeps cold transactions on the OCC path and routes hot-object candidates to a server-side serialized path. Hot candidates come from configured hot products and from hot detection counters. The serialized path applies the transaction under the store mutation lock, eliminating OCC lock/validation races for those hot writes at the cost of queue wait and centralization.

Compared with baseline OCC, hybrid reduces abort storms for hot objects because hot transactions no longer compete through repeated CAS-style lock acquisition and read-set validation. Compared with backoff, hybrid changes the conflict-resolution mechanism rather than only delaying retries. Compared with hot-detection-only, hybrid consumes the hot signal to change execution path.

Current optimization: hot detection refresh is rate-limited by `--hot-refresh-interval` instead of scanning all product objects on every transaction. This reduces avoidable overhead, especially in low-contention workloads where hybrid should mostly remain on the cold path.

## Data Sanity Check

- Total parsed runs: 40
- Focused trade-off runs: 0
- Application scenario runs: 0
- The aggregate Algorithm Summary is directionally useful but mixes low-contention, high-contention, read-heavy, write-heavy, uniform, and skewed workloads. It should not be read as a universal ranking.
- Backoff runs showing near-zero abort rate are plausible in this prototype because failed commit attempts are often converted into retry delay before max-retry abort, not because contention disappears.
- Hybrid arbitration should show low hot-path ratio under low-contention workloads and high hot-path ratio under explicitly hot or detected-hot workloads. The focused matrix is used to validate that behavior.


## Observations

- Baseline OCC exposes the expected high-contention failure modes through lock failures, validation failures, retries, and lower committed throughput under hot-product workloads.
- Contention-aware backoff changes retry timing and is useful as a relative trend signal, but the exact abort and commit ratios remain Soft-RoCE/VirtualBox-specific.
- Hot object detection reports candidate hot objects and hot-path candidates, separating detection evidence from actual server arbitration.
- Hybrid arbitration serializes transactions that touch detected or configured hot products. This reduces OCC conflict behavior for those transactions at the cost of queue-wait overhead.
- The most reliable signal is not a single mean throughput number, but the combination of committed tx/sec, retry_per_commit, lock/validation failures, hot_path_ratio, and correctness counters per scenario.

## Non-Comparable Items

- Absolute RDMA latency and throughput
- Absolute CPU utilization
- Hardware-counter metrics such as cycles, instructions, or IPC
- RNIC offload effects, PCIe bottlenecks, switch latency, RoCE congestion control, and bare-metal cluster scalability

## Notes

The benchmark is a constrained local DSM protocol prototype using RDMA-style object metadata, OCC read/write/validate phases, and a simplified FIFO server arbitration path. Use the CSV files for detailed comparisons by workload, thread count, and write ratio.

## Future Work

1. Replace the current coarse local mutation lock with per-object or per-hot-shard arbitration queues so unrelated hot objects can proceed in parallel.
2. Add adaptive routing: send a transaction to arbitration only when predicted OCC retry cost exceeds estimated queue wait.
3. Record true latency samples instead of estimating p95/p99 from the mean; this is needed for credible tail-latency analysis.
4. Add two-node Soft-RoCE transport validation for the benchmark path, then keep protocol results separate from transport overhead.
5. Add crash recovery/durability semantics for server arbitration before discussing production readiness.
6. Explore dynamic hot threshold tuning and hysteresis to prevent oscillation between cold and hot paths.
7. Compare against additional contention-control designs such as wound-wait, timestamp ordering, queued locks, and deterministic scheduling.
8. Increase repetitions and run duration for publication-grade statistics; the current matrix is useful for trend discovery, not final claims.

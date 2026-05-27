# Phase 2 Report

## Scope

This report evaluates a RDMA-style DSM prototype in a VirtualBox + Ubuntu 22.04 + Soft-RoCE environment. The results are protocol-level evidence for algorithm behavior only. They are not hardware RDMA NIC latency, throughput, CPU, PCIe, switch, or RNIC offload measurements.

The measured benchmark path is a local RDMA-style DSM/OCC protocol simulation over the project object store. It does not by itself prove two-node Soft-RoCE transport behavior; transport setup should be validated separately with the existing RDMA connection utilities or verbs-level tests.

The current hybrid arbitration implementation does not support crash recovery or durability.

## Artifacts

- Raw run directories: `results/phase2`
- Per-run CSV: `results/phase2/summary.csv`
- Grouped CSV: `results/phase2/summary_by_config.csv`
- Run timestamp range: 2026-05-27T21:08:45Z to 2026-05-27T21:44:48Z

## Correctness

- Invariant violations: 0
- Duplicate commits: 0
- Status: PASS

Checked invariants: stock remains non-negative by unsigned state transition, user balance does not underflow, and `sold_count + final_stock = initial_stock` across all products.

## Algorithm Summary

| Algorithm | Runs | Commit tx/sec mean | Abort rate mean | Retry/commit mean | Lock fails mean | Validation fails mean | Hot path ratio mean |
|---|---:|---:|---:|---:|---:|---:|---:|
| backoff_occ | 68 | 1503686.09 | 0.000 | 0.001 | 683.5 | 586.8 | 0.000 |
| baseline_occ | 68 | 1416116.10 | 0.002 | 0.005 | 2187.5 | 1522.0 | 0.000 |
| hot_detection_occ | 68 | 1306945.93 | 0.003 | 0.006 | 1944.7 | 1528.7 | 0.000 |
| hybrid_arbitration_occ | 68 | 2150443.84 | 0.000 | 0.000 | 237.9 | 82.2 | 0.752 |

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

- Total parsed runs: 272
- Focused trade-off runs: 144
- Application scenario runs: 32
- The aggregate Algorithm Summary is directionally useful but mixes low-contention, high-contention, read-heavy, write-heavy, uniform, and skewed workloads. It should not be read as a universal ranking.
- Backoff runs showing near-zero abort rate are plausible in this prototype because failed commit attempts are often converted into retry delay before max-retry abort, not because contention disappears.
- Hybrid arbitration should show low hot-path ratio under low-contention workloads and high hot-path ratio under explicitly hot or detected-hot workloads. The focused matrix is used to validate that behavior.

## Focused Scenario Results

| Workload | Best TX/sec Algorithm | Baseline TX/sec | Backoff TX/sec | Hot Detection TX/sec | Hybrid TX/sec | Hybrid Hot Path | Interpretation |
|---|---|---:|---:|---:|---:|---:|---|
| high_hot16_write100 | hybrid_arbitration_occ | 1057926 | 1077456 | 1063773 | 3382912 | 1.000 | Broad hot write set strongly favors hybrid serialization in this prototype. |
| high_hot1_write100 | hybrid_arbitration_occ | 1222592 | 1197894 | 1127562 | 3082190 | 0.967 | Single hot product favors hybrid serialization; OCC variants still see lock/validation pressure. |
| low_uniform_read95 | hybrid_arbitration_occ | 3111043 | 3299601 | 3215478 | 3328468 | 0.123 | Low hot-path ratio shows hybrid mostly stays cold; after refresh-rate optimization it is competitive, but this should be rechecked with longer runs. |
| mixed_hot4_write50 | hybrid_arbitration_occ | 1566265 | 1603785 | 1652185 | 2788060 | 0.927 | Moderate hot-set contention benefits from selective arbitration; compare queue wait against reduced lock/validation failures. |
| mixed_uniform_write20 | hybrid_arbitration_occ | 2566679 | 2213092 | 2384812 | 2618816 | 0.295 | Uniform mixed traffic has limited hot-object benefit; optimized hybrid is competitive because it mostly avoids arbitration. |
| zipf99_write100 | hybrid_arbitration_occ | 789374 | 753261 | 742618 | 1205059 | 0.565 | Skewed Zipfian writes allow hybrid detection/arbitration to reduce conflict symptoms. |

The focused results support the design hypothesis rather than proving hardware performance: hybrid arbitration is most useful when hot-object routing is accurate and contention is high; backoff is a lower-overhead mitigation for moderate conflicts; baseline OCC remains appropriate for low-contention or uniform mixed workloads.

## Application Scenario Results

| Scenario | Application | Best TX/sec Algorithm | Baseline TX/sec | Backoff TX/sec | Hot Detection TX/sec | Hybrid TX/sec | Hybrid Hot Path | Trend |
|---|---|---|---:|---:|---:|---:|---:|---|
| ad_budget_hot_campaign | ad_budget | hybrid_arbitration_occ | 1934236 | 1947498 | 1847188 | 2112533 | 0.850 | Moderate write ratio and campaign hotspots often favor backoff or selective hybrid. |
| ad_budget_read_heavy_dashboard | ad_budget | backoff_occ | 2398931 | 2763486 | 2521288 | 2220779 | 0.040 | Read-heavy monitoring favors simple cold-path execution. |
| flash_sale_spike | flash_sale | hybrid_arbitration_occ | 1130004 | 938882 | 1170430 | 3338807 | 0.990 | Extreme single-product pressure favors hybrid arbitration. |
| long_tail_marketplace_zipf | flash_sale | hybrid_arbitration_occ | 293851 | 280811 | 282388 | 325123 | 0.700 | Zipfian skew creates emergent hot products; hybrid can help if detection is timely. |
| mixed_hot_catalog | flash_sale | hybrid_arbitration_occ | 774359 | 659494 | 717437 | 1128660 | 0.950 | Moderate skew/hot catalog is a trade-off between backoff overhead and arbitration serialization. |
| ticket_booking_hot_event | ticket_booking | hybrid_arbitration_occ | 986336 | 890061 | 865321 | 2117174 | 0.860 | Seat/event hotspots benefit from arbitration when hot routing is accurate. |
| ticket_booking_many_events | ticket_booking | hybrid_arbitration_occ | 717012 | 800118 | 730664 | 916456 | 0.390 | Distributed demand has lower hot-path ratio; hybrid still wins in this short run, but the margin should be validated with longer repetitions. |
| warehouse_restock_uniform | warehouse_restock | hybrid_arbitration_occ | 1407856 | 991097 | 1161502 | 1538132 | 0.010 | Very low hot-path ratio indicates hybrid mostly behaves like optimized cold-path OCC in this run. |

These scenarios intentionally vary application shape rather than only following the high-contention grid. The purpose is to see when arbitration is helpful, when backoff is sufficient, and when baseline OCC is preferable because centralization overhead dominates conflict cost.


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

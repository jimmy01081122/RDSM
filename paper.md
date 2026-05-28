# Contention-aware Transaction Routing for RDMA-style DSM under Constrained Software-RDMA Prototyping

## Abstract

This project studies how an RDMA-style DSM transaction runtime should route transactions under changing hot-object contention when only constrained software-RDMA prototyping is available. The environment is virtualized Linux + Ubuntu 22.04 + Soft-RoCE/`rdma_rxe`; there is no hardware RDMA NIC. Therefore, all measurements are interpreted as transport diagnostics, protocol-level evidence, or prototype-relative comparisons, not as hardware RDMA latency or throughput.

The work is organized as a staged evidence chain. A preliminary Stage 0 records the earlier broad prototype and corrects its methodological overclaims. Phase 1 validates two-VM Soft-RoCE verbs functionality and establishes the trust boundary. Phase 2 builds a local RDMA-style DSM/OCC protocol benchmark. Phase 3 studies contention behavior, backoff, hot detection, and static hybrid arbitration. Phase 4 adds scalable arbitration queues and cleanup experiments for shared application metadata. Phase 5 adds bounded transaction latency sampling and an adaptive-routing prototype. Project-level two-node RDMA wrapper validation and two-node DSM transactions over verbs are left as explicit future Phase 6 and Phase 7 work.

## 1. Introduction

RDMA-based DSM systems often rely on one-sided READ/WRITE/atomic operations to reduce remote CPU involvement, but real deployments also depend on RNICs, PCIe, switches, congestion-control behavior, memory registration, and careful transport setup. This project does not have that hardware. The research question is therefore deliberately constrained:

```text
How should an RDMA-style DSM transaction runtime route transactions under changing hot-object contention when only constrained software-RDMA prototyping is available?
```

The contribution is not an absolute hardware RDMA speedup claim. The contribution is a staged methodology and prototype for studying contention-control behavior under a clearly bounded environment:

- Two-VM Soft-RoCE trust-boundary characterization.
- RDMA-style DSM/OCC local protocol prototype.
- OCC, backoff, hot-detection, and static-arbitration contention analysis.
- Per-object and per-shard arbitration queues.
- Latency sampler with bounded rotating-sample mode and debug-only full sampling.
- Adaptive routing prototype with completed calibration and selected reduced-matrix default.
- Explicit future plan for project-level two-node RDMA wrapper and DSM transaction validation.

## 2. Background and Motivation

DSM provides a shared-memory abstraction over distributed memory. RDMA-style DSM designs can map reads, writes, and lock acquisition onto one-sided verbs or atomics. OCC is a natural match: transactions optimistically read object versions, buffer writes, validate read sets, acquire locks or CAS-style ownership, update values, and release locks.

OCC works well under low contention because most transactions validate successfully. Under hot-object contention, transactions repeatedly fail lock acquisition or read-set validation. Backoff reduces synchronized retry storms but does not remove the underlying conflict. Arbitration serializes contested hot updates and can eliminate retry storms, but it introduces queueing, centralization, and potential head-of-line blocking.

The central systems question is when a runtime should stay optimistic and when it should route work through arbitration.

## 3. Environment and Methodology Boundaries

The experimental environment is virtualized Linux + Ubuntu 22.04 + Soft-RoCE/`rdma_rxe`. The project has no hardware RDMA NIC. As a result, the paper forbids claims about hardware RDMA performance, RNIC offload, PCIe behavior, switch behavior, RoCE congestion-control behavior, bare-metal cluster scalability, production-ready DSM behavior, crash recovery, or durability.

Evidence is separated by purpose:

| Evidence type | Purpose | Not for |
|---|---|---|
| Two-VM Soft-RoCE verbs validation | Transport functionality and diagnostic sanity: RC path, QP/GID/CQ metadata, READ/WRITE/SEND perftest behavior | Hardware RDMA performance, RNIC offload, DSM transaction throughput |
| Local DSM/OCC protocol benchmark | Algorithm behavior and contention-control comparison under identical prototype conditions | Distributed DSM-over-verbs throughput |
| Latency and adaptive-routing prototype | Prototype-relative comparison of routing, queueing, retries, and tail latency | Hardware RDMA p99 latency |

The local DSM/OCC prototype uses RDMA-style one-sided READ/WRITE/CAS abstractions for protocol development, but it is not a complete two-node verbs execution path. The two-VM Soft-RoCE validation includes one-sided operations such as RDMA READ and RDMA WRITE and message-style operations such as SEND latency tests. Therefore, the whole project should not be described as purely one-sided. The current project does not yet implement project-level two-node DSM transactions over RDMA verbs, and it does not yet include project-level remote atomic/CAS validation.

## 4. Pre-Phase: Preliminary Prototype and Problem Origin

Before the current staged evaluation, the previous project under `/home/node1/RDSM/prev_project` implemented a broad RDMA-style systems prototype. Its files and reports confirm a FaRM-like DSM prototype, a HERD-like key-value store, an RDMA verbs wrapper, a slab allocator / memory manager, an OCC transaction path, performance-monitoring utilities, OS-level analysis tools, and benchmark executables named `farm_benchmark`, `herd_benchmark`, and `os_analysis`.

This earlier prototype was valuable as engineering groundwork. It built the first C++17 module structure, implemented RDMA-style APIs and transaction abstractions, explored slab allocation and memory-registration ideas, created early benchmark and monitoring infrastructure, and exposed the central problem that motivates this project: OCC can be efficient under low contention but can collapse into retry and validation-failure storms when transactions repeatedly touch hot objects.

However, the earlier reports also over-interpreted several measurements. Numbers such as RDMA WRITE around 100-149 ns, RDMA READ around 150-200 ns, ATOMIC CAS around 200-300 ns, and HERD GET/PUT in tens of nanoseconds should now be treated as local prototype observations, local software-path measurements, or measurement artifacts unless they were measured from `post_send` to CQE completion across a real two-node verbs path. They are not hardware RDMA operation latency. Likewise, Soft-RoCE/`rdma_rxe` is useful for verbs compatibility and transport diagnostics, but it does not provide RNIC hardware offload and cannot by itself prove hardware kernel-bypass or RNIC-offload benefits.

Any old direct comparison between WSL2/Soft-RoCE numbers and Mellanox/ConnectX-like hardware RDMA should be treated only as qualitative context. The previous DSM prototype should also be described as RDMA-style local protocol evidence unless a measured transaction path truly crossed two nodes through the project RDMA verbs layer. The current project therefore keeps the earlier work as Stage 0 problem discovery, not as final performance evidence.

The transition is deliberate: the previous project provided breadth; the current project provides methodological control. It separates two-VM Soft-RoCE transport validation, local DSM/OCC protocol benchmarks, prototype-relative latency and adaptive-routing evidence, and future two-node DSM-over-verbs work. The earlier measurements are retained as historical prototype observations that helped identify the contention-routing question, but they are not used as hardware RDMA evidence.

## 5. Phase 1: Two-VM Soft-RoCE Feasibility and Trust Boundary

Phase 1 uses two VMs:

- Client: `node2`, `192.168.56.102`, `rxe0`
- Server: `node1`, `192.168.56.101`, `rxe0`

The evidence comes from the existing directories whose historical names include `phase3` and `phase3a`, especially `results/phase3_soft_roce_validation/` and `results/phase3/two_node_soft_roce_*`. In the final narrative, these artifacts are interpreted as Phase 1 evidence because the transport feasibility work came before the later local protocol experiments.

The validation uses external verbs tools:

- `ibv_rc_pingpong`
- `ib_read_bw`
- `ib_write_bw`
- `ib_read_lat`
- `ib_write_lat`
- `ib_send_lat`

These tools validate that the RC path works across the two VMs, QP/GID/CQ/transport-level behavior is observable, Soft-RoCE verbs functionality works across two VMs, and READ/WRITE/SEND transport diagnostics can be collected.

They do not validate hardware RDMA NIC performance, RNIC offload, PCIe/switch/congestion-control behavior, project-level DSM transaction throughput, or project-level remote CAS correctness.

Soft-RoCE is usable for verbs functionality validation and diagnostic transport sanity checks, but not for hardware performance claims. Because virtualized Soft-RoCE/`rdma_rxe` latency jitter is high, later phases use local DSM/OCC protocol benchmarks for algorithm development and prototype-relative comparison.

## 6. Phase 2: RDMA-style DSM/OCC Local Protocol Prototype

Phase 2 implements the local RDMA-style DSM/OCC substrate. It includes versioned objects, lock bits, object-specific data locks, RDMA-style READ/WRITE/CAS abstractions, read and write sets, validation, commit, retry, and abort logic.

This phase intentionally uses a local RDMA-style protocol benchmark rather than two-node DSM-over-verbs, because Phase 1 established that Soft-RoCE is useful for verbs functionality validation but not for absolute RDMA performance claims.

The purpose of Phase 2 is to create a controllable platform for later contention-control experiments. It is not an end-to-end distributed DSM benchmark.

## 7. Phase 3: Contention Behavior and Static Hybrid Arbitration

Phase 3 evaluates:

- `baseline_occ`
- `backoff_occ`
- hot detection as monitoring
- static hybrid arbitration

Hot-object contention appears through lock failures, validation failures, retries, and lower committed throughput. Backoff can reduce retry synchronization. Hot detection identifies contested objects. Static hybrid arbitration routes known-hot transactions through a serialized hot path while leaving cold transactions on OCC.

Aggregate averages are not universal rankings because they mix low-contention, high-contention, read-heavy, write-heavy, uniform, and skewed workloads. The useful signals are per-workload committed throughput, abort and retry behavior, lock/validation failures, hot-path ratio, correctness counters, and later latency percentiles.

This phase motivated Phase 4 scalable queues and Phase 5 tail-latency sampling because static arbitration can remove retry storms while creating queue wait.

## 8. Phase 4: Scalable Arbitration Queues and Cleanup

Phase 4 adds arbitration modes:

- `--arbitration-mode=global`
- `--arbitration-mode=per_object`
- `--arbitration-mode=per_shard`
- `--hot-shards=1|2|4|8|16|32`

It also records queue wait, queue length, and service time percentiles. The goal is to determine whether global arbitration over-serializes unrelated hot objects, and whether per-object or per-shard arbitration reduces unnecessary queueing for broad hot sets.

Phase 4 sanity checking found a hot/cold locking-discipline bug. The initial hot path used object-specific locks, while parts of the OCC cold path still used the old global mutation mutex. The implementation was fixed by using deterministic object-id lock ordering and object-specific data locks in both paths. This fix is important because otherwise hot/cold comparisons could reflect inconsistent synchronization rather than algorithm behavior.

Phase 4b adds `--sold-counter-mode=global|per_product`.

- `global` sold counter preserves the application-level shared metadata bottleneck.
- `per_product` sold counters isolate arbitration queue behavior by removing one shared metadata object from every transaction.

Phase 4b is cleanup/isolation validation, not a final performance claim. It demonstrates that per-object or per-shard arbitration only helps when the application data model does not force every transaction through another shared object.

## 9. Phase 5: Latency Sampling and Adaptive Routing

Phase 5 adds bounded transaction latency sampling. Latency is prototype-relative evidence only, not hardware RDMA latency.

The benchmark supports:

- `--latency-sampling=off|full|reservoir`
- `--latency-sample-size`
- `--latency-output`
- `--allow-dangerous-full-sampling`

The default sample size is 10,000. Full sampling is debug-only and guarded: it is rejected when `duration_sec > 2` or `threads > 2` unless explicitly overridden. The CLI mode is named `reservoir`, but the current implementation is a bounded rotating sample, not statistically uniform Algorithm R reservoir sampling. Therefore, final latency numbers are used as prototype-relative tail indicators under identical collection policy, not as unbiased latency-distribution estimates. Run summaries separate all-transaction latency, committed-only latency, aborted transaction latency, cold OCC latency, hot arbitration latency, retry percentiles, and sample count. Aborted transactions are not mixed into committed latency percentiles.

Phase 5 also adds a minimal `hybrid_adaptive_arbitration_occ` prototype. The routing rule compares estimated OCC retry cost with estimated arbitration queue cost:

```text
estimated_occ_cost_us = base_occ_latency_us + expected_retries * retry_penalty_us
estimated_arbitration_cost_us = queue_wait_estimate_us + service_time_estimate_us
```

A transaction should enter arbitration only when estimated OCC cost exceeds arbitration cost by the routing margin. Calibration has been completed, the selected default is `routing_margin_us=5`, `cost_window_ms=500`, `min_samples_before_adapt=100`, `adaptive_object_scope=shard`, and `hot_shards=8`, and this default is included in the reduced final matrix. The scripted phase-change approximation restarts benchmark processes between phases; it is useful as a low-risk approximation but not as evidence of continuous in-process adaptive state transitions.

## 10. Final Evaluation

### 10.1 Correctness

Every final result must report invariant violations and duplicate commits. A row is correctness-clean only when both are zero. Correctness-clean short runs can validate plumbing, but they are not automatically publication-grade performance evidence.

### 10.2 Adaptive Routing Calibration

Calibration uses a small matrix over `routing_margin_us=5,10,20` and `cost_window_ms=100,250,500` on `low_uniform_read95`, `mixed_hot4_write50`, and `high_hot16_write100`. The current calibration rows are correctness-clean and select `routing_margin_us=5`, `cost_window_ms=500`, `min_samples_before_adapt=100`, `adaptive_object_scope=shard`, and `hot_shards=8`.

The calibration also exposes a limitation: this minimal adaptive prototype is conservative. It keeps low-contention arbitration near zero, but hot-workload arbitration is a small nonzero fraction concentrated around cold-start/insufficient-sample behavior. This supports using the policy for a final focused comparison, but it does not by itself prove adaptive routing improves throughput or p99 latency.

### 10.3 Focused Synthetic Workloads

The reduced focused final matrix completed 324 synthetic rows: 6 synthetic workloads, 6 algorithm variants, thread counts 1/2/4, and 3 repetitions. All rows are correctness-clean with zero invariant violations and zero duplicate commits. These results are reduced focused evidence, not a publication-grade full evaluation.

Synthetic workloads are reported separately:

- `low_uniform_read95`
- `mixed_uniform_write20`
- `mixed_hot4_write50`
- `high_hot1_write100`
- `high_hot16_write100`
- `zipf99_write100`

Across low-contention synthetic workloads, OCC remains competitive. In `low_uniform_read95`, baseline/backoff OCC are near the top because almost all transactions can stay on the optimistic path and arbitration adds no useful serialization. `mixed_uniform_write20` shows a similar pattern: backoff improves over baseline by reducing timing-related retry pressure, while static arbitration variants carry extra hot-detection/arbitration overhead without a stable hot set to exploit. The adaptive prototype also keeps arbitration near zero in these cases, but its throughput is not better than backoff; this supports the claim that avoiding unnecessary arbitration is necessary but not sufficient for a win.

Under explicit hot-object pressure, static arbitration becomes useful. In `mixed_hot4_write50`, static per-object and per-shard arbitration increase committed throughput relative to baseline OCC and sharply reduce validation failures, at the cost of queue wait and higher prototype-relative p99 than backoff. In `high_hot1_write100` and `high_hot16_write100`, static arbitration is the strongest trend: hot transactions are serialized instead of repeatedly failing OCC validation, so baseline/backoff suffer much lower throughput and much higher validation-failure counts. Per-object and per-shard modes are preferable to global arbitration when the hot set is broader, but the exact winner depends on queueing and scheduling noise in the VM.

`zipf99_write100` is more ambiguous. Static arbitration variants are slightly ahead in throughput, but they also show substantial queue wait and p99 latency. Backoff remains competitive because it reduces retry storms with lower routing overhead. This workload is a warning against a universal ranking: skew helps arbitration only if hot detection and queue placement are timely enough to offset queueing.

The adaptive prototype does not yet outperform static arbitration in the final matrix. It is conservative and often routes few transactions to arbitration after cold-start and insufficient-sample periods, so it preserves correctness but can fail to capture hot-workload gains. Its value in the current project is methodological: it exposes the metrics needed for routing decisions, not a mature policy claim.

### 10.4 Application-like Workloads

The reduced focused final matrix completed 216 application-like rows: 4 application-like workloads, 6 algorithm variants, thread counts 1/2/4, and 3 repetitions. All rows are correctness-clean with zero invariant violations and zero duplicate commits.

Application-like workloads are reported in a separate section:

- `flash_sale_spike`
- `ticket_booking_hot_event`
- `ad_budget_read_heavy_dashboard`
- `long_tail_marketplace_zipf`

Do not combine synthetic and application-like workloads into one universal ranking table.

The application-like workloads reinforce the same shape-dependent conclusion. `flash_sale_spike` and `ticket_booking_hot_event` behave like hot-object workloads: static arbitration reduces retry/validation storms and outperforms baseline OCC and backoff in committed throughput, while queue wait becomes the price paid for serialization. `ad_budget_read_heavy_dashboard` is read-heavy, so backoff and baseline OCC remain preferable; forcing many transactions through arbitration is unnecessary even if a campaign-like hot set exists. `long_tail_marketplace_zipf` sits between these extremes: static arbitration can help slightly, but the advantage is small and p99/queue wait must be reported alongside throughput.

### 10.5 Sold Counter Bottleneck Study

The controlled sold-counter comparison completed 48 rows in `results/final_sold_counter_comparison/`: 2 workloads, 2 sold-counter modes, 2 algorithms, thread counts 2/4, and 3 repetitions. All rows are correctness-clean with zero invariant violations and zero duplicate commits.

The result should be read as a data-model bottleneck study, not as a universal algorithm ranking. `global` sold counter represents an application-level shared metadata object that every successful write touches. `per_product` sold counters remove that extra shared object and better isolate arbitration queue behavior. In `high_hot16_write100`, per-product counters improve throughput for both static and adaptive per-shard variants: the per-product/global throughput ratio is about 1.40 for static at 2 threads and about 1.10 at 4 threads, while adaptive improves by about 1.20 and 1.19 at 2 and 4 threads. This supports the interpretation that the global metadata object can mask the benefit of sharded arbitration.

In `mixed_hot4_write50`, the trend is less uniform. Per-product improves static per-shard throughput at 2 threads, but at 4 threads it slightly underperforms global in this short VM run; adaptive also improves at 2 threads but regresses at 4 threads. Therefore the comparison should not be used to claim that per-product is always faster. The defensible conclusion is narrower: per-object/per-shard arbitration only helps when the data model does not force every transaction through a separate shared object, and the effect must be interpreted per workload and thread count.

### 10.6 Phase-change Approximation

The current phase-change script is a multi-process scripted approximation. It does not prove continuous in-process adaptive reaction. The formal short approximation uses the selected calibration default and remains correctness-clean, but it should be interpreted only as a low-risk approximation of phase changes across separate benchmark processes.

### 10.7 Latency Sampling Overhead

Latency overhead must be disclosed with any latency result. Full sampling is not acceptable for final runs. The bounded rotating sample keeps memory bounded but still introduces measurable overhead in the VM environment. Because it is not statistically uniform reservoir sampling, final text should avoid claims that p95/p99 are unbiased estimates of the full latency distribution.

## 11. Discussion

OCC is appropriate when contention is low. Backoff is useful when conflicts are moderate and timing-related. Static arbitration helps when hot objects are stable and routing is accurate. Per-object and per-shard arbitration reduce artificial over-serialization compared with global arbitration only when the application data model does not add another shared bottleneck. Adaptive routing is promising only if calibration and final matrix data show that it avoids unnecessary arbitration and p99 regression.

The current reduced final matrix is correctness-clean and provides a much stronger basis than the earlier smoke/discovery rows. However, it remains reduced: duration is 10 seconds per run, repetitions are 3, and all results are from the local prototype under virtualized Linux + Soft-RoCE constraints. It should be described as reduced focused final evidence, not as a full publication-grade evaluation.

## 12. Limitations

The project has no hardware RDMA NIC, no bare-metal cluster, no RNIC offload measurements, no PCIe or switch measurements, no production durability, and no crash recovery. The local DSM/OCC benchmark is not a two-node verbs DSM transaction benchmark. The Phase 1 transport validation does not include project-level remote atomic/CAS correctness. Short smoke/discovery rows are useful for debugging and sanity checks but not final performance claims. The current latency `reservoir` mode is implemented as a bounded rotating sample, so tail-latency conclusions must be described as prototype-relative indicators rather than unbiased latency-distribution estimates.

## 13. Future Work

### 13.1 Phase 6: Project-level Two-node RDMA Wrapper Validation

Future Phase 6 should validate the project RDMA wrapper across two VMs before attempting DSM transactions. It should implement and measure RDMA connection setup, PD/CQ/QP setup, memory-region registration, remote address/rkey exchange, RDMA WRITE validation, RDMA READ validation, RDMA CAS validation, CQ completion validation, error handling, and timeout behavior.

Future tests should include WRITE 8B/64B/4KB, READ 8B/64B/4KB, and CAS 8B. The allowed claim would be only that the project RDMA wrapper can execute READ/WRITE/CAS over two-node Soft-RoCE. It would still not justify hardware RDMA performance, DSM transaction throughput, RNIC offload, or real RDMA p99 latency claims.

### 13.2 Phase 7: Two-node DSM Transaction over RDMA Verbs

Future Phase 7 should begin only after Phase 6 succeeds. The minimal future transaction should perform a single-object DSM/OCC path: RDMA READ object/version, RDMA CAS lock, RDMA WRITE update, RDMA WRITE unlock/version, and final value/version verification.

Future expansion can include two-object transactions, read-only transactions, write-heavy transactions, multi-client conflict tests, and simple OCC validation. The allowed claim would be only that a minimal DSM/OCC transaction path can run over two-node Soft-RoCE. It would still not imply hardware RDMA performance, production DSM, cluster scalability, or durability.

## 14. Conclusion

This project demonstrates a bounded way to study contention-aware transaction routing for RDMA-style DSM without RDMA hardware. Stage 0 explains how a broad earlier prototype exposed the problem while also motivating stricter evidence boundaries. Phase 1 establishes Soft-RoCE verbs feasibility and its trust boundary. Later phases use local protocol benchmarks to study OCC, backoff, static arbitration, scalable queues, bounded latency sampling, and adaptive routing. The final contribution is a careful protocol-level evaluation path, not a hardware RDMA performance claim.

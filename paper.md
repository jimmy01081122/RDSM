# Contention-aware Transaction Routing for RDMA-style DSM under Constrained Software-RDMA Prototyping

## Abstract

This project studies how an RDMA-style DSM transaction runtime should route transactions under changing hot-object contention when only constrained software-RDMA prototyping is available. The environment is virtualized Linux + Ubuntu 22.04 + Soft-RoCE/`rdma_rxe`; there is no hardware RDMA NIC. Therefore, all measurements are interpreted as transport diagnostics, protocol-level evidence, or prototype-relative comparisons, not as hardware RDMA latency or throughput.

The work is organized as a staged evidence chain. Phase 1 validates two-VM Soft-RoCE verbs functionality and establishes the trust boundary. Phase 2 builds a local RDMA-style DSM/OCC protocol benchmark. Phase 3 studies contention behavior, backoff, hot detection, and static hybrid arbitration. Phase 4 adds scalable arbitration queues and cleanup experiments for shared application metadata. Phase 5 adds transaction latency sampling and an adaptive-routing prototype. Project-level two-node RDMA wrapper validation and two-node DSM transactions over verbs are left as explicit future Phase 6 and Phase 7 work.

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
- Latency sampler with bounded reservoir mode and debug-only full sampling.
- Adaptive routing prototype and calibration plan.
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

## 4. Phase 1: Two-VM Soft-RoCE Feasibility and Trust Boundary

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

## 5. Phase 2: RDMA-style DSM/OCC Local Protocol Prototype

Phase 2 implements the local RDMA-style DSM/OCC substrate. It includes versioned objects, lock bits, object-specific data locks, RDMA-style READ/WRITE/CAS abstractions, read and write sets, validation, commit, retry, and abort logic.

This phase intentionally uses a local RDMA-style protocol benchmark rather than two-node DSM-over-verbs, because Phase 1 established that Soft-RoCE is useful for verbs functionality validation but not for absolute RDMA performance claims.

The purpose of Phase 2 is to create a controllable platform for later contention-control experiments. It is not an end-to-end distributed DSM benchmark.

## 6. Phase 3: Contention Behavior and Static Hybrid Arbitration

Phase 3 evaluates:

- `baseline_occ`
- `backoff_occ`
- hot detection as monitoring
- static hybrid arbitration

Hot-object contention appears through lock failures, validation failures, retries, and lower committed throughput. Backoff can reduce retry synchronization. Hot detection identifies contested objects. Static hybrid arbitration routes known-hot transactions through a serialized hot path while leaving cold transactions on OCC.

Aggregate averages are not universal rankings because they mix low-contention, high-contention, read-heavy, write-heavy, uniform, and skewed workloads. The useful signals are per-workload committed throughput, abort and retry behavior, lock/validation failures, hot-path ratio, correctness counters, and later latency percentiles.

This phase motivated Phase 4 scalable queues and Phase 5 tail-latency sampling because static arbitration can remove retry storms while creating queue wait.

## 7. Phase 4: Scalable Arbitration Queues and Cleanup

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

## 8. Phase 5: Latency Sampling and Adaptive Routing

Phase 5 adds true transaction latency sampling. Latency is prototype-relative evidence only, not hardware RDMA latency.

The benchmark supports:

- `--latency-sampling=off|full|reservoir`
- `--latency-sample-size`
- `--latency-output`
- `--allow-dangerous-full-sampling`

The default sample size is 10,000. Full sampling is debug-only and guarded: it is rejected when `duration_sec > 2` or `threads > 2` unless explicitly overridden. Reservoir sampling is the only acceptable mode for final latency analysis. Run summaries separate all-transaction latency, committed-only latency, aborted transaction latency, cold OCC latency, hot arbitration latency, retry percentiles, and sample count. Aborted transactions are not mixed into committed latency percentiles.

Phase 5 also adds a minimal `hybrid_adaptive_arbitration_occ` prototype. The routing rule compares estimated OCC retry cost with estimated arbitration queue cost:

```text
estimated_occ_cost_us = base_occ_latency_us + expected_retries * retry_penalty_us
estimated_arbitration_cost_us = queue_wait_estimate_us + service_time_estimate_us
```

A transaction should enter arbitration only when estimated OCC cost exceeds arbitration cost by the routing margin. Current adaptive-routing evidence is smoke-level and calibration-level unless final matrix results are explicitly generated. The scripted phase-change approximation restarts benchmark processes between phases; it is useful as a low-risk approximation but not as evidence of continuous in-process adaptive state transitions.

## 9. Final Evaluation

### 9.1 Correctness

Every final result must report invariant violations and duplicate commits. A row is correctness-clean only when both are zero. Correctness-clean short runs can validate plumbing, but they are not automatically publication-grade performance evidence.

### 9.2 Adaptive Routing Calibration

Calibration uses a small matrix over `routing_margin_us=5,10,20` and `cost_window_ms=100,250,500` on `low_uniform_read95`, `mixed_hot4_write50`, and `high_hot16_write100`. The current calibration rows are correctness-clean and select `routing_margin_us=5`, `cost_window_ms=500`, `min_samples_before_adapt=100`, `adaptive_object_scope=shard`, and `hot_shards=8`.

The calibration also exposes a limitation: this minimal adaptive prototype is conservative. It keeps low-contention arbitration near zero, but hot-workload arbitration is a small nonzero fraction concentrated around cold-start/insufficient-sample behavior. This supports using the policy for a final focused comparison, but it does not by itself prove adaptive routing improves throughput or p99 latency.

### 9.3 Focused Synthetic Workloads

Synthetic workloads should be reported separately:

- `low_uniform_read95`
- `mixed_uniform_write20`
- `mixed_hot4_write50`
- `high_hot1_write100`
- `high_hot16_write100`
- `zipf99_write100`

### 9.4 Application-like Workloads

Application-like workloads should be reported in a separate section:

- `flash_sale_spike`
- `ticket_booking_hot_event`
- `ad_budget_read_heavy_dashboard`
- `long_tail_marketplace_zipf`

Do not combine synthetic and application-like workloads into one universal ranking table.

### 9.5 Sold Counter Bottleneck Study

The controlled sold-counter comparison should compare `global` and `per_product` only for selected hot workloads. The interpretation is a data-model lesson: shared metadata can reintroduce serialization even when the arbitration queue itself is per-object or per-shard.

### 9.6 Phase-change Approximation

The current phase-change script is a multi-process scripted approximation. It does not prove continuous in-process adaptive reaction. The formal short approximation uses the selected calibration default and remains correctness-clean, but it should be interpreted only as a low-risk approximation of phase changes across separate benchmark processes.

### 9.7 Latency Sampling Overhead

Latency overhead must be disclosed with any latency result. Full sampling is not acceptable for final runs. Reservoir sampling keeps memory bounded but still introduces measurable overhead in the VM environment.

## 10. Discussion

OCC is appropriate when contention is low. Backoff is useful when conflicts are moderate and timing-related. Static arbitration helps when hot objects are stable and routing is accurate. Per-object and per-shard arbitration reduce artificial over-serialization compared with global arbitration only when the application data model does not add another shared bottleneck. Adaptive routing is promising only if calibration and final matrix data show that it avoids unnecessary arbitration and p99 regression.

## 11. Limitations

The project has no hardware RDMA NIC, no bare-metal cluster, no RNIC offload measurements, no PCIe or switch measurements, no production durability, and no crash recovery. The local DSM/OCC benchmark is not a two-node verbs DSM transaction benchmark. The Phase 1 transport validation does not include project-level remote atomic/CAS correctness. Short smoke/discovery rows are useful for debugging and sanity checks but not final performance claims.

## 12. Future Work

### 12.1 Phase 6: Project-level Two-node RDMA Wrapper Validation

Future Phase 6 should validate the project RDMA wrapper across two VMs before attempting DSM transactions. It should implement and measure RDMA connection setup, PD/CQ/QP setup, memory-region registration, remote address/rkey exchange, RDMA WRITE validation, RDMA READ validation, RDMA CAS validation, CQ completion validation, error handling, and timeout behavior.

Future tests should include WRITE 8B/64B/4KB, READ 8B/64B/4KB, and CAS 8B. The allowed claim would be only that the project RDMA wrapper can execute READ/WRITE/CAS over two-node Soft-RoCE. It would still not justify hardware RDMA performance, DSM transaction throughput, RNIC offload, or real RDMA p99 latency claims.

### 12.2 Phase 7: Two-node DSM Transaction over RDMA Verbs

Future Phase 7 should begin only after Phase 6 succeeds. The minimal future transaction should perform a single-object DSM/OCC path: RDMA READ object/version, RDMA CAS lock, RDMA WRITE update, RDMA WRITE unlock/version, and final value/version verification.

Future expansion can include two-object transactions, read-only transactions, write-heavy transactions, multi-client conflict tests, and simple OCC validation. The allowed claim would be only that a minimal DSM/OCC transaction path can run over two-node Soft-RoCE. It would still not imply hardware RDMA performance, production DSM, cluster scalability, or durability.

## 13. Conclusion

This project demonstrates a bounded way to study contention-aware transaction routing for RDMA-style DSM without RDMA hardware. Phase 1 establishes Soft-RoCE verbs feasibility and its trust boundary. Later phases use local protocol benchmarks to study OCC, backoff, static arbitration, scalable queues, latency sampling, and adaptive routing. The final contribution is a careful protocol-level evaluation path, not a hardware RDMA performance claim.

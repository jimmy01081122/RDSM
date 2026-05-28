# Contention-aware Transaction Routing for RDMA-style DSM under Constrained Software-RDMA Prototyping

## Abstract

This work studies RDMA-style DSM transaction protocols under hardware-constrained conditions. The environment does not provide real RDMA NICs; therefore, the quantitative results are interpreted as protocol-level and rapid-validation evidence rather than hardware RDMA performance. The project evaluates baseline OCC, backoff OCC, hot-object detection, static hybrid arbitration, scalable arbitration queues, and planned adaptive routing. The central finding is not absolute RDMA speed, but when and why arbitration is useful for hot-object contention and where its queueing costs become visible.

## 1. Introduction

Real RDMA DSM systems depend on RNICs, PCIe, switches, and hardware offload. This project lacks that hardware and therefore uses local protocol simulation plus Soft-RoCE validation to develop and test ideas quickly. The constrained research question is: how should an RDMA-style DSM transaction runtime react to hot-object contention?

The paper makes no claims about absolute hardware RDMA latency, throughput, RNIC offload, PCIe behavior, switch behavior, congestion control, or bare-metal cluster scalability. Instead, it contributes a staged methodology: local protocol prototype, contention analysis, scalable arbitration design, prototype-relative latency/queue measurement, adaptive-routing design, and separate two-node Soft-RoCE transport validation.

## 2. Background and Motivation

DSM provides a shared-memory abstraction over distributed memory. RDMA-style DSM designs often use one-sided reads, writes, and atomics to reduce remote CPU involvement. OCC fits this model because transactions can optimistically read object versions, buffer writes, validate read sets, acquire locks or CAS-style ownership, write committed values, and release locks.

OCC performs well under low contention because most transactions validate successfully. Under hot-object contention, many transactions repeatedly fail lock acquisition or read-set validation. Backoff can reduce synchronized retry storms, but it does not remove the underlying conflict. Arbitration can eliminate hot-object retry storms by serializing contested updates, but it introduces queue wait, centralization, and possible head-of-line blocking.

## 3. Environment Constraints and Methodology

The experiments run in a constrained virtualized Linux + Ubuntu 22.04 + Soft-RoCE/`rdma_rxe` environment. The local protocol benchmark is not equivalent to a two-node hardware RDMA deployment. Soft-RoCE transport validation is reported separately from protocol-level DSM/OCC results. Absolute latency and throughput are not hardware claims.

## 4. Phase 1: RDMA-style DSM Prototype

Phase 1 implemented the basic DSM/OCC substrate: versioned objects, lock bits, RDMA-style read/write/CAS abstraction, and an OCC transaction path. Its purpose was to establish a controllable baseline for transaction mechanics and correctness. The result supports functional feasibility of the protocol mechanics and enables later contention experiments.

## 5. Phase 2: Contention Behavior and Hybrid Arbitration

Phase 2 implemented `baseline_occ`, `backoff_occ`, `hot_detection_occ`, and `hybrid_arbitration_occ`. The evaluation showed that hot-object contention manifests as lock failures, validation failures, retries, and lower committed throughput. Hybrid arbitration helps when hot routing is accurate and contention is high; backoff can be sufficient under moderate or read-heavy workloads. Mixed aggregate averages are not treated as universal rankings.

The most reliable Phase 2 signal is the combination of committed tx/sec, retry per commit, lock/validation failures, hot-path ratio, and correctness counters.

## 6. Phase 3: Soft-RoCE Transport Validation and Its Limits

Phase 3 validates two-node Soft-RoCE verbs functionality between node2 and node1 using external verbs tools, including `ibv_rc_pingpong`, RDMA WRITE/READ bandwidth, and RDMA WRITE/READ/SEND latency sweeps. It confirms that RC QP setup, GID exchange, CQ completion, and transport-level operations work across two Linux VMs. It does not support hardware RDMA performance claims or stable latency claims.

High latency jitter in the virtualized Soft-RoCE/`rdma_rxe` environment justifies keeping transport validation separate from protocol evaluation.

Project-level `two_node_rdma_validation` is deferred because the current wrapper would require non-trivial RDMA CM, memory-region exchange, and CQ validation work. This is recorded as future work and should not block latency sampling or adaptive-routing research.

## 7. Phase 4: Scalable Arbitration Queues

Phase 4 adds arbitration modes behind `--arbitration-mode=global|per_object|per_shard` and `--hot-shards=1|2|4|8|16|32`. Global arbitration preserves the old coarse behavior. Per-object arbitration serializes by hot product. Per-shard arbitration maps hot products onto a bounded number of shard queues.

The implementation now records queue-wait, queue-length, and service-time samples for hot arbitration. The initial short matrix in `results/phase4_arbitration/` is a smoke/discovery dataset only; publication claims require longer duration and repetitions.

The intended evaluation questions are whether global arbitration over-serializes unrelated hot objects, whether per-object/per-shard modes reduce queue wait under broad hot sets, where shard-count improvements saturate, and whether arbitration hurts low-contention or read-heavy workloads.

During Phase 4 sanity checking, the initial queue-based arbitration prototype exposed an important synchronization issue: hot arbitration and cold OCC initially used inconsistent data-locking disciplines. The hot path used object-specific locks, while parts of the OCC path still relied on the old global mutation mutex. The implementation was fixed so both OCC and arbitration acquire object-specific data locks in deterministic object-id order. This makes hot/cold path comparisons meaningful and avoids path-specific synchronization artifacts.

## 7.1 Phase 4b: Cleanup and Isolation Validation

Phase 4b is a cleanup/isolation validation step, not a new performance claim. It adds `--sold-counter-mode=global|per_product`. The global mode preserves the original shared `sold_count` object and represents an application-level global metadata bottleneck. The per-product mode uses one sold counter per product so that broad hot-product workloads can better isolate arbitration queue behavior.

This distinction matters because per-object or per-shard arbitration can only reduce unrelated-object serialization when the application data model does not reintroduce a shared object in every transaction. The final paper should discuss this as a systems lesson rather than presenting the short Phase 4b discovery rows as final performance evidence.

## 8. Phase 5: Latency Sampling and Adaptive Routing

The next planned phase is true transaction latency sampling and adaptive routing. Tail latency matters because a design can improve committed throughput while worsening p95 or p99 latency. Hybrid arbitration has exactly this risk: it can remove retry storms while introducing queue wait.

Latency should be treated as prototype-relative evidence, not hardware RDMA latency. It is still useful when comparing global queues, per-object queues, per-shard queues, static arbitration, and adaptive routing under identical experimental conditions.

Adaptive routing should compare estimated OCC retry cost against estimated arbitration queue cost:

```text
estimated_occ_cost_us = base_occ_latency_us + expected_retries * retry_penalty_us
estimated_arbitration_cost_us = queue_wait_estimate_us + service_time_estimate_us
```

Transactions should enter arbitration only when the estimated OCC cost exceeds arbitration cost by a routing margin.

## 9. Evaluation

The final evaluation should include correctness invariants, focused synthetic contention workloads, application-like workloads, arbitration queue comparison, adaptive routing under steady workloads, adaptive routing under phase-change workloads, two-node Soft-RoCE validation, and statistical reporting.

Focused synthetic workloads and application-like workloads should be reported in separate sections or figures. Do not use a combined universal ranking table across unrelated workload families.

For final benchmark configurations, report mean, standard deviation, 95% confidence interval, repetition count, warmup duration, and measurement duration. Do not rank algorithms from one short run.

## 10. Discussion

OCC is enough under low contention. Backoff is often enough when conflicts are moderate and retry timing is the primary issue. Static arbitration helps when hot objects are clear and stable. Adaptive routing becomes necessary when workload phases change or when static hot detection sends too much traffic through queues.

Soft-RoCE, `rdma_rxe`, and virtualization limit performance claims, but they do not invalidate prototype-relative protocol comparisons when the boundary is stated clearly.

## 11. Limitations

The project has no hardware RNIC, no bare-metal cluster, no production durability, no crash recovery, and high Soft-RoCE latency jitter. Local protocol results and transport validation results are separated. Additional protocols such as queued locks, timestamp ordering, wound-wait, and deterministic scheduling should remain appendix-only unless implemented and evaluated rigorously.

## 12. Conclusion

The project demonstrates a staged way to study RDMA-style DSM transaction behavior without RDMA hardware. The main finding is about contention-aware routing, not hardware RDMA speed. Per-object/per-shard arbitration and future adaptive routing make the hybrid approach more defensible than coarse global serialization. Two-node Soft-RoCE validation closes the transport-functionality gap without changing the hardware-performance limitation.

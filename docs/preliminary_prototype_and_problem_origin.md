# Preliminary Prototype and Problem Origin

## 1. Previous Prototype Scope

The previous project under `/home/node1/RDSM/prev_project` implemented a broad RDMA-style systems prototype. Its source tree contains a FaRM-like DSM prototype, a HERD-like key-value store, an RDMA verbs wrapper, a slab-style memory manager, an OCC transaction path, performance-monitoring utilities, and OS-level analysis code. The benchmark executables described by the old CMake file are `farm_benchmark`, `herd_benchmark`, and `os_analysis`.

This earlier work should be interpreted as an engineering prototype and problem-discovery stage. It demonstrated that the constrained development environment could host a broad RDMA-style codebase and helped identify which transaction-control questions were worth studying next.

## 2. Useful Engineering Contributions

The previous prototype was useful because it built an initial C++17 module structure, wrapped RDMA-style APIs, implemented slab allocation and memory-management ideas, introduced OCC transaction abstractions, created HERD/FaRM-inspired benchmark paths, and added performance-monitoring and OS-analysis scaffolding. It also exposed a key systems problem: optimistic concurrency can work well in low contention but can degrade sharply when many transactions repeatedly touch the same hot objects.

## 3. Overclaims and Corrections

The old reports include nanosecond-scale numbers such as RDMA WRITE around 100-149 ns, RDMA READ around 150-200 ns, ATOMIC CAS around 200-300 ns, and HERD GET/PUT around tens of nanoseconds. Under the current methodology, those values are retained only as historical prototype observations. They should be treated as local software-path costs or measurement artifacts unless they were measured from `post_send` to CQE completion across a real two-node verbs path. They are not hardware RDMA operation latency.

Soft-RoCE/`rdma_rxe` remains useful for verbs compatibility and transport diagnostics, but it does not provide RNIC hardware offload. It therefore cannot prove hardware kernel-bypass or RNIC-offload benefits. Any old direct comparison between WSL2/Soft-RoCE numbers and Mellanox/ConnectX-like hardware RDMA should be treated only as qualitative context, not as evidence.

The old DSM prototype should also not be described as an end-to-end distributed RDMA DSM benchmark unless the measured transaction path truly crossed two nodes through the project RDMA verbs layer. In the current project, those older results are not used as proof of hardware RDMA performance or two-node DSM-over-verbs throughput.

## 4. Lessons Learned

The previous project provided breadth; the current project provides methodological control. The old work showed that HERD/FaRM-inspired components could be prototyped locally, but it did not sufficiently separate transport diagnostics from protocol benchmarks, did not maintain a strict Soft-RoCE trust boundary, and did not provide bounded transaction-latency sampling suitable for reduced final evaluation.

## 5. Transition to Current RDSM Project

The current project narrows the research question to contention-aware transaction routing for RDMA-style DSM under constrained software-RDMA prototyping. It separates two-VM Soft-RoCE verbs validation, local DSM/OCC protocol evidence, prototype-relative latency/adaptive-routing evidence, and future two-node DSM-over-verbs work. The earlier measurements are retained as historical observations that helped motivate the work, but they are not used as hardware RDMA evidence.

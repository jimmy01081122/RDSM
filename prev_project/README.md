# Previous Project: Preliminary RDMA-style Prototype（校正版）

本目錄保存目前 RDSM 專案的 Stage 0：Preliminary Prototype and Problem Origin。

舊專案曾探索 FaRM-like DSM、HERD-like key-value store、RDMA verbs wrapper、slab memory manager、OCC transaction path、performance monitor 與 OS-level analysis。它的價值在於建立工程雛形並暴露 hot-object contention 下 OCC retry/validation failure 的問題。

## Claim Boundary

舊版報告曾出現 RDMA WRITE/READ/ATOMIC CAS 納秒級 latency、HERD GET/PUT tens of ns、Soft-RoCE 類似硬體 offload、或與真實 RDMA NIC 直接比較的敘述。這些敘述已不再作為研究結論。

本目錄中的舊數據只能視為：

- historical prototype observations
- local software-path measurements
- Soft-RoCE compatibility/diagnostic observations
- 後續 RDSM 研究問題的來源

不得解讀為：

- hardware RDMA latency/throughput
- RNIC offload benefit
- Mellanox/ConnectX-like performance
- project-level two-node DSM-over-verbs throughput
- production DSM correctness/performance

## Implemented Prototype Components

```text
prev_project/
├── include/
│   ├── rdma_wrapper.h       # RDMA Verbs API wrapper prototype
│   ├── memory_manager.h     # Slab-style memory manager
│   ├── dsm_transaction.h    # OCC transaction abstraction
│   ├── perf_monitor.h       # Prototype performance monitor
│   └── herd_kv_store.h      # HERD-inspired KV prototype
├── src/
│   ├── rdma_wrapper.cpp
│   ├── memory_manager.cpp
│   ├── dsm_transaction.cpp
│   ├── perf_monitor.cpp
│   └── herd_kv_store.cpp
├── experiments/
│   ├── farm_benchmark.cpp
│   ├── herd_benchmark.cpp
│   └── os_analysis.cpp
└── docs/
    ├── PROJECT_REPORT.md
    ├── FINAL_SUMMARY.md
    ├── COMPLETION_REPORT.md
    └── ENV.md
```

## Relationship to Current RDSM

The previous project provided breadth; the current RDSM project provides methodological control.

目前 RDSM 專案將這個舊原型重新解讀為 Stage 0，並在後續 phases 中分離：

- two-VM Soft-RoCE verbs feasibility and trust boundary
- local RDMA-style DSM/OCC protocol benchmark
- contention-aware arbitration and adaptive routing
- future project-level two-node RDMA wrapper validation
- future two-node DSM transaction over verbs

請以目前 repo 根目錄的 `paper.md`、`paper_zh.md`、`HANDOFF.md` 與 `READING_GUIDE.md` 作為最終解讀依據。

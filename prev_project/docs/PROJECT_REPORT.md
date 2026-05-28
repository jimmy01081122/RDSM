# FaRM DSM 與 HERD Key-Value Store 原型研究報告（校正版）

## 校正聲明

本文件是舊專案報告的校正版。舊版文字曾將 Soft-RoCE/WSL2 或本地 prototype 數字描述成近似硬體 RDMA 效能，這在目前 RDSM 專案的方法論下是不成立的。

本文件中的所有舊數據只能作為：

- 歷史原型觀察
- 本地軟體路徑成本
- Soft-RoCE 相容性或診斷觀察
- 後續研究問題的來源

不得解讀為硬體 RDMA latency/throughput、RNIC offload 效益、真實 ConnectX/Mellanox NIC 表現、或完整 two-node DSM-over-verbs 交易效能。

## 1. 專案背景與目標

舊專案的目標是建立一個 RDMA-style 系統原型，用來探索 FaRM-like DSM、HERD-like key-value store、RDMA verbs wrapper、slab memory manager、OCC transaction path 與 OS-level analysis tool。這個原型的價值在於建立系統雛形與工程基礎，而不是提供硬體 RDMA 效能結論。

## 2. 系統架構

舊專案包含下列元件：

- `rdma_wrapper.*`：RDMA Verbs API wrapper，包含裝置初始化、PD/CQ/QP、記憶體註冊與 READ/WRITE/ATOMIC 介面。
- `memory_manager.*`：slab-style memory manager，用於探索預先分配與記憶體註冊管理。
- `dsm_transaction.*`：OCC transaction abstraction，包含讀寫集、驗證、提交與 abort 流程。
- `herd_kv_store.*`：HERD-inspired key-value prototype，包含 4-entry bucket hash table、request/response abstraction 與 benchmark helpers。
- `perf_monitor.*`：prototype-level latency/throughput/OS metric collection。
- `experiments/farm_benchmark.cpp`、`herd_benchmark.cpp`、`os_analysis.cpp`：原型測試與 OS 分析入口。

## 3. 舊數據的正確解讀

舊版報告中的數字，例如 RDMA WRITE 約 100-149 ns、RDMA READ 約 150-200 ns、ATOMIC CAS 約 200-300 ns、HERD GET/PUT 數十 ns，不應被視為硬體 RDMA 操作延遲。除非測量明確從 `post_send` 到 CQE completion 且跨越真實 two-node verbs path，否則這些數字只能視為本地 prototype 或量測方法造成的觀察。

Soft-RoCE/`rdma_rxe` 對此專案的正確用途是 verbs compatibility 與 transport diagnostic validation。它不提供 RNIC hardware offload，也不能證明 kernel bypass 或 offload 的硬體效益。

## 4. 舊專案的貢獻

舊專案仍然有重要價值：

- 建立 RDMA-style C++17 codebase。
- 實作 transaction abstraction 與 OCC flow。
- 建立 slab allocator 與 memory-management scaffold。
- 實作 HERD/FaRM-inspired prototype。
- 暴露 hot-object contention 下 OCC retry/validation failure 的問題。
- 建立後續 RDSM 專案的方法論反省基礎。

## 5. 與目前 RDSM 專案的關係

目前 RDSM 專案將舊專案重新定位為 Stage 0：Preliminary Prototype and Problem Origin。舊專案提供 breadth；目前專案提供 methodology control。現在的研究分離：

- two-VM Soft-RoCE verbs transport validation
- local DSM/OCC protocol benchmark
- prototype-relative latency/adaptive-routing evidence
- future two-node RDMA wrapper validation
- future two-node DSM transaction over verbs

因此，舊專案的結論應寫成：它是一個廣泛的 RDMA-style preliminary prototype，成功暴露 contention-control 問題，但不提供硬體 RDMA 或 end-to-end distributed DSM performance 證據。

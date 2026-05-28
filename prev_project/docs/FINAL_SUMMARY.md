# Preliminary Prototype Final Summary（校正版）

## 定位

本目錄是目前 RDSM 專案的 Stage 0：Preliminary Prototype and Problem Origin。它記錄舊專案曾實作的 RDMA-style 系統雛形，以及後續研究問題如何形成。

## 已實作元件

| 元件 | 狀態 | 正確解讀 |
|---|---|---|
| RDMA verbs wrapper | 完成原型 | verbs API wrapper 與本地/Soft-RoCE 相容性探索，不等於硬體 RDMA 效能證明。 |
| Slab memory manager | 完成原型 | memory-management scaffold，可作為後續設計參考。 |
| OCC transaction path | 完成原型 | 本地 RDMA-style transaction abstraction，暴露 hot contention 問題。 |
| HERD-like KV store | 完成原型 | HERD-inspired local prototype，不代表重現 SIGCOMM'14 硬體效能。 |
| Performance monitor | 完成原型 | 可收集 prototype metrics，但舊 latency 數字需重新解讀。 |
| OS analysis tool | 完成原型 | 可協助理解系統路徑，但不可推出 RNIC offload 結論。 |

## 舊數據校正

舊文件中的下列敘述已不作為正式效能主張：

- RDMA WRITE latency 100-149 ns
- RDMA READ latency 150-200 ns
- ATOMIC CAS latency 200-300 ns
- HERD GET latency 約 30 ns
- HERD PUT latency 約 40 ns
- Soft-RoCE 證明 kernel bypass / RNIC offload
- WSL2/Soft-RoCE 可直接和真實 RDMA NIC 效能比較

這些數字只可作為歷史 prototype observation、local software-path measurement 或 measurement artifact。若沒有真實 two-node verbs path 的 `post_send` 到 CQE completion 測量，不可稱為硬體 RDMA operation latency。

## 對目前專案的貢獻

舊專案最重要的價值不是數字，而是問題發現：

1. OCC 在低競爭下合理，但在 hot-object contention 下容易產生 retry storm。
2. 單純 backoff 只能緩解衝突時間分布，不能消除共享物件的衝突根源。
3. hybrid arbitration 是值得深入研究的方向，但必須分清楚 transport validation 與 protocol benchmark。
4. Soft-RoCE 是相容性與診斷工具，不是硬體 RDMA performance substitute。

## 與目前 RDSM 的銜接

目前 RDSM 專案修正舊專案的方法論問題，並形成以下證據鏈：

- Phase 1：two-VM Soft-RoCE verbs feasibility and trust boundary。
- Phase 2：local RDMA-style DSM/OCC protocol prototype。
- Phase 3：contention behavior and static hybrid arbitration。
- Phase 4：scalable arbitration queues and cleanup。
- Phase 5：bounded latency sampling and adaptive routing。
- Future Phase 6/7：project-level two-node RDMA wrapper 與 DSM transaction over verbs。

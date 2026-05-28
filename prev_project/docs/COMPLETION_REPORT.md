# Preliminary Prototype Completion Report（校正版）

## 完成內容

舊專案完成了一個 RDMA-style systems prototype，包含：

- RDMA Verbs wrapper
- Slab memory manager
- OCC transaction abstraction
- HERD-like key-value prototype
- FaRM-like DSM prototype
- Performance monitor
- OS analysis utilities
- `farm_benchmark`、`herd_benchmark`、`os_analysis` 三個實驗入口

## 方法論校正

舊版完成報告曾把 Soft-RoCE/WSL2 或本地 prototype 數字寫成近似硬體 RDMA 成果。這些敘述已撤回。正確說法如下：

- Soft-RoCE/`rdma_rxe` 可以驗證 verbs 相容性與 transport path，但沒有 RNIC offload。
- 舊專案沒有提供可用於主張硬體 RDMA latency/throughput 的證據。
- 舊專案沒有提供 project-level two-node DSM-over-verbs transaction throughput。
- 舊 latency 數字只能保留為 historical prototype observations。

## 仍然成立的成果

舊專案仍可視為成功的 preliminary prototype，因為它：

1. 建立 RDMA-style codebase。
2. 實作 transaction path 與 memory manager。
3. 建立早期 benchmark / monitor infrastructure。
4. 暴露 hot-object contention 下 OCC retry/abort 行為。
5. 促成目前 RDSM 專案的 evidence-boundary 設計。

## 不再使用的主張

以下主張不得再引用為研究結論：

- RDMA latency 小於 300 ns。
- HERD GET/PUT 達 tens of ns 且代表 distributed KV latency。
- Soft-RoCE 可證明硬體 kernel bypass 或 RNIC offload。
- WSL2/Soft-RoCE 數字可直接比較真實 ConnectX/Mellanox NIC。
- 舊 FaRM DSM prototype 已證明 end-to-end distributed RDMA DSM 效能。

## 最終結論

舊專案是目前 RDSM 的 Stage 0。它提供廣度與問題發現；目前 RDSM 專案提供更嚴格的方法論、階段化證據鏈與受限 claim boundary。

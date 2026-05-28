# RDSM 專案閱讀指引

最後更新：2026-05-28 UTC

本文件是閱讀 RDSM 專案時的中文導覽。它的目的不是取代 `paper.md`，而是幫助讀者快速建立正確脈絡，避免把 Soft-RoCE 原型數據誤讀成硬體 RDMA 成果。

## 1. 一句話定位

RDSM 研究的是：

```text
在沒有硬體 RDMA NIC、只能使用 virtualized Linux + Ubuntu 22.04 + Soft-RoCE/rdma_rxe 的條件下，
如何以受控原型研究 RDMA-style DSM transaction runtime 在 hot-object contention 下的 transaction routing 策略。
```

本專案的核心貢獻是方法論與原型實驗鏈，不是硬體 RDMA 效能宣稱。

## 2. 必讀 Claim Boundary

本專案不得宣稱：

- hardware RDMA latency / throughput
- RNIC offload benefit
- PCIe、switch、RoCE congestion-control 行為
- bare-metal cluster scalability
- production-ready DSM
- project-level two-node DSM-over-verbs throughput
- project-level remote CAS correctness

本專案可以宣稱：

- Soft-RoCE verbs transport diagnostic evidence
- local RDMA-style DSM/OCC protocol evidence
- prototype-relative contention-control comparison
- reduced focused final matrix 下的相對趨勢
- Future Phase 6/7 的明確工作邊界

## 3. 建議閱讀順序

1. `READING_GUIDE.md`：先掌握閱讀順序與主張邊界。
2. `README.md`：看建置方式、專案結構、主要 artifacts。
3. `paper.md`：目前英文最終研究報告，最完整、最權威。
4. `paper_zh.md`：`paper.md` 的中文版本，便於中文閱讀與口頭報告。
5. `HANDOFF.md`：看目前資料集、重現指令、已知限制與下一步。
6. `PROJECT_PLAN_STATUS.md`：看各研究項目是否完成。
7. `results/final_project_convergence_summary.md`：看目前 cycle 是否已收束。
8. `results/final_audit_bug_report.md`：看審查出的程式問題與 patch plan。
9. `prev_project/docs/*.md`：只作為 Stage 0 歷史原型資料，請讀校正版，不要沿用舊 overclaim。

## 4. Phase 結構

| 階段 | 名稱 | 重點 |
|---|---|---|
| Stage 0 | Preliminary Prototype and Problem Origin | 舊專案提供工程基礎與問題發現，但舊 Soft-RoCE/WSL2 數字只作歷史觀察。 |
| Phase 1 | Two-VM Soft-RoCE Feasibility and Trust Boundary | 用 perftest/verbs 工具驗證 node2 -> node1 的 Soft-RoCE transport path。 |
| Phase 2 | RDMA-style DSM/OCC Local Protocol Prototype | 建立本地 DSM/OCC protocol benchmark。 |
| Phase 3 | Contention Behavior and Static Hybrid Arbitration | 比較 baseline OCC、backoff、hot detection、static arbitration。 |
| Phase 4 | Scalable Arbitration Queues and Cleanup | 加入 global/per-object/per-shard queues 與 sold-counter isolation。 |
| Phase 5 | Bounded Latency Sampling and Adaptive Routing | 加入 bounded latency sampling、adaptive routing、calibration 與 reduced final matrix。 |
| Future Phase 6 | Project-level Two-node RDMA Wrapper Validation | 未來才驗證 project wrapper 的 READ/WRITE/CAS。 |
| Future Phase 7 | Two-node DSM Transaction over RDMA Verbs | 未來才做真正 two-node DSM/OCC transaction over verbs。 |

## 5. 重要 artifacts

- `paper.md`：英文最終報告。
- `paper_zh.md`：中文最終報告。
- `README.md`：建置與結構說明。
- `HANDOFF.md`：交接與重現指令。
- `PROJECT_PLAN_STATUS.md`：項目確認表。
- `results/final_focused_matrix/`：reduced final focused matrix，540 rows。
- `results/final_sold_counter_comparison/`：global vs per-product sold-counter comparison，48 rows。
- `results/final_audit_bug_report.md`：audit bug confirmation report and patch plan。
- `results/final_project_convergence_summary.md`：目前 cycle 收束摘要。
- `docs/preliminary_prototype_and_problem_origin.md`：Stage 0 詳述。

## 6. 如何解讀 final matrix

final focused matrix 是 reduced focused evidence，不是 publication-grade full evaluation。它的用途是比較相同 prototype 條件下不同 workload shape 的相對趨勢：

- 低競爭 / read-heavy：baseline OCC 或 backoff 往往仍然有競爭力。
- uniform mixed write：backoff 可減少 retry storm，arbitration 未必划算。
- hot-object write：static arbitration 通常可減少 validation failure 並提高 throughput，但會引入 queue wait。
- Zipfian skew：arbitration 是否有利取決於 hot detection 與 queueing overhead。
- application-like workloads：flash sale / ticket booking 較像 hot-object；ad budget dashboard 較 read-heavy；long-tail marketplace 介於兩者之間。

不要把所有 workload 混在一起做 universal ranking。

## 7. 如何解讀 sold-counter comparison

`results/final_sold_counter_comparison/` 比較：

- `global` sold counter
- `per_product` sold counter

它的目的不是證明某演算法絕對較快，而是說明 application data model 可能重新引入 shared metadata bottleneck。若每筆 write 都碰同一個 global `sold_count`，per-object/per-shard arbitration 的好處可能被另一個共享物件掩蓋。

## 8. Latency sampling 注意事項

CLI mode 名為 `reservoir`，但目前實作是 bounded rotating sample，不是統計上 unbiased 的 Algorithm R reservoir sampling。

因此：

- 可用來做相同 collection policy 下的 prototype-relative tail indicator。
- 不可宣稱 p95/p99 是完整交易延遲分布的 unbiased estimate。
- full sampling 只允許短時間 debug，不應用於 final runs。

## 9. Audit bug 閱讀方式

請閱讀 `results/final_audit_bug_report.md`。目前已確認幾個 code-level 限制：

- `duplicate_commit_count` / `hot_cold_interference_count` 是 dead counters。
- `attempted_tx` 在 OCC retry path 中混合 logical transaction 與 retry attempt。
- `reservoir` sampling 不是 Algorithm R。
- OCC lock acquisition failure 可能留下 phantom lock bit。
- legacy `latency_us_p95/p99` 是估算欄位，不應作 final latency evidence。
- Zipfian distribution 每次 order 重建，可能影響 skewed workload overhead。
- legacy server arbitrator abort counter 有 double-count 風險。

目前未套用 invasive code fixes，除非未來明確設定 `APPLY_CODE_FIXES=1` 或另行授權。

## 10. 舊專案 prev_project 的閱讀方式

`prev_project/` 已納入 Stage 0。請只用它回答：

- 舊原型實作了哪些元件？
- 它如何暴露 OCC hot contention 問題？
- 它如何促成目前 RDSM 的方法論修正？

不要用它回答：

- 硬體 RDMA latency 是多少？
- RNIC offload 有多少效益？
- DSM-over-verbs transaction throughput 是多少？
- HERD/FaRM 原論文效能是否被重現？

## 11. 最短讀法

如果時間很少，讀：

1. `README.md`
2. `paper_zh.md`
3. `results/final_project_convergence_summary.md`
4. `results/final_audit_bug_report.md`
5. `HANDOFF.md` 的 Current Dataset 與 Reproduction Commands

這樣可以掌握目前專案的研究問題、完成狀態、證據邊界與剩餘風險。

# RDSM 研究計畫項目確認文件

最後更新：2026-05-28 UTC

本文件用來維護本專案的研究計畫項目、完成狀態、證據位置與後續動作。狀態定義如下：

- `完成`：已有實作或文件，且有對應 artifact。
- `部分完成`：已有 prototype、smoke、或初步文件，但尚未達到 final evaluation 等級。
- `待執行`：已規劃，尚未完成必要實驗或彙整。
- `未開始`：尚未實作或尚未產生 artifact。
- `未來工作`：明確不屬於目前 cycle，避免混入目前結論。

## Claim Boundary

| 項目 | 狀態 | 確認 |
|---|---|---|
| 不宣稱 hardware RDMA latency/throughput | 完成 | `paper.md`、`HANDOFF.md` 已明確限制。 |
| 不宣稱 RNIC offload、PCIe、switch、RoCE congestion-control | 完成 | `paper.md` methodology boundary 已列出 forbidden claims。 |
| 不宣稱 project-level two-node DSM transaction throughput | 完成 | Phase 6/7 被列為 future work。 |
| 不宣稱 project-level remote CAS correctness | 完成 | Phase 1 僅驗證 external verbs tools；remote CAS 留到 Future Phase 6。 |
| 區分 transport diagnostics 與 local protocol benchmark | 完成 | `paper.md` 第 3、4、5 節。 |

## Phase Status

| Phase | 研究項目 | 狀態 | 證據 / Artifact | 後續動作 |
|---|---|---|---|---|
| Stage 0 | Preliminary prototype and problem origin | 完成 | `prev_project/`、`docs/preliminary_prototype_and_problem_origin.md`、`paper.md`、`HANDOFF.md` | 舊 Soft-RoCE/WSL2 latency 數字只保留為歷史 prototype observation，不作硬體 RDMA 證據。 |
| Phase 1 | Two-VM Soft-RoCE feasibility and trust boundary | 完成 | `results/phase3_soft_roce_validation/summary.md`、`results/phase3/two_node_soft_roce_summary.csv` | 舊目錄名仍含 `phase3`，論文中需說明 final paper 視為 Phase 1 evidence。 |
| Phase 1 | `ibv_rc_pingpong` validation | 完成 | `results/phase3/two_node_soft_roce_20260528_phase3a_layer1/` | 無需重跑，除非節點環境改變。 |
| Phase 1 | `ib_read_bw` validation | 完成 | `results/phase3/two_node_soft_roce_20260528_phase3a_layer1/` | 無需重跑，除非節點環境改變。 |
| Phase 1 | `ib_write_bw`, `ib_read_lat`, `ib_write_lat`, `ib_send_lat` validation | 完成 | `results/phase3_soft_roce_validation/summary.md` | 保持為 transport diagnostics。 |
| Phase 2 | RDMA-style DSM/OCC local protocol prototype | 完成 | `src/dsm_object.*`、`src/occ_engine.*`、`experiments/phase2_dsm_benchmark.cpp` | 文件需持續強調 local protocol benchmark，不是 two-node DSM-over-verbs。 |
| Phase 2 | Versioned objects, lock bits, read/write sets, validation, commit/retry/abort | 完成 | 同上 | 後續若改 transaction path，需同步更新 correctness checks。 |
| Phase 3 | Baseline OCC | 完成 | `phase2_dsm_benchmark`、Phase 2/3 result CSV | Final matrix 需重跑 reduced publication-style rows。 |
| Phase 3 | Backoff OCC | 完成 | `phase2_dsm_benchmark` | Final matrix 需重跑 reduced publication-style rows。 |
| Phase 3 | Hot detection as monitoring | 完成 | `hot_detection_occ` / hot counters | Main paper 中作 sanity/appendix，避免當主演算法排名。 |
| Phase 3 | Static hybrid arbitration | 完成 | `hybrid_arbitration_occ` | Final matrix 需包含 global/per_object/per_shard_8。 |
| Phase 4 | Scalable arbitration queues: global/per_object/per_shard | 完成 | `results/phase4_arbitration/discovery_summary.md` | 目前是 discovery/smoke，不作 final ranking。 |
| Phase 4 | Queue wait, queue length, service time metrics | 完成 | `results/phase4_arbitration/sanity_check.md`、parser CSV fields | Final matrix 需保留這些欄位。 |
| Phase 4 | Hot/cold locking-discipline bug fix | 完成 | `src/occ_engine.cpp`、`experiments/phase2_dsm_benchmark.cpp`、`paper.md` | 已修正為 deterministic object-id lock ordering。 |
| Phase 4b | `sold_counter_mode=global|per_product` | 完成 | `results/phase4b_cleanup/phase4b_cleanup_summary.md` | Final controlled comparison 仍需用較長 reduced settings 重跑。 |
| Phase 4b | Phase 4b artifact verification | 完成 | `results/phase4b_cleanup/verification_summary.md` | 已通過 correctness/metadata/metric checks。 |
| Phase 5 | Transaction latency sampler | 完成 | `include/latency_sampler.h`、`src/latency_sampler.cpp` | CLI `reservoir` 目前是 bounded rotating sample；不可宣稱 unbiased Algorithm R reservoir sampling。 |
| Phase 5 | Latency overhead smoke | 完成 | `results/phase5_latency_sampling/latency_overhead_summary.md` | Final sample size 已定為 10000；若之後改變才需重跑 overhead check。 |
| Phase 5 | Full sampling guard | 完成 | CLI `--allow-dangerous-full-sampling` | Full sampling 僅 debug，不納入 final matrix。 |
| Phase 5 | Adaptive routing prototype | 完成 | `hybrid_adaptive_arbitration_occ`、`results/phase5_adaptive_routing/adaptive_smoke_summary.md` | 已完成 calibration/default selection 並納入 reduced final matrix；效能主張需依 final matrix 分析。 |
| Phase 5 | Adaptive routing calibration | 完成 | `results/phase5_adaptive_routing/calibration_summary.md`、`.csv` | 54 runs correctness-clean；selected default 為 `routing_margin_us=5`, `cost_window_ms=500`, `adaptive_object_scope=shard`, `hot_shards=8`。 |
| Phase 5 | Formal phase-change approximation | 完成 | `results/phase5_adaptive_routing/phase_change_summary.md`、`.csv` | 18 rows correctness-clean；仍只是 multi-process approximation，不是 continuous in-process adaptation。 |
| Final | Reduced focused final matrix | 完成 | `results/final_focused_matrix/summary.csv`、`summary_by_config.csv`、`final_summary.md`、`statistical_report.md`、`run_metadata.json` | 540 rows correctness-clean；這是 reduced focused final matrix，不是 publication-grade full evaluation。 |
| Final | Global vs per-product controlled comparison | 完成 | `results/final_sold_counter_comparison/summary.csv`、`summary_by_config.csv`、`sold_counter_comparison_summary.md`、`run_metadata.json` | 48 rows correctness-clean；只作 shared metadata bottleneck study。 |
| Final | Statistical report | 完成 | `results/final_focused_matrix/statistical_report.md` | 已含 mean/stddev/95% CI/repetitions/duration/sampling policy。 |
| Final | Final `paper.md` convergence | 完成 | `paper.md` | 已納入 Stage 0、final matrix trend analysis、sold-counter comparison、claim boundary。 |
| Final | Chinese `report.md` convergence | 部分完成 | `report.md` | 已加 stale warning；若需要中文最終論文，需依 `paper.md` 重新整合。 |
| Final | Final `HANDOFF.md` convergence | 完成 | `HANDOFF.md` | 已更新 final matrix rows、correctness 與重現指令。 |
| Final | Project convergence summary | 完成 | `results/final_project_convergence_summary.md` | 用於確認目前 cycle 收束狀態。 |

## Future Work Status

| Future Phase | 項目 | 狀態 | 邊界 |
|---|---|---|---|
| Future Phase 6 | Project-level two-node RDMA wrapper validation | 未來工作 | 需 RDMA connection, PD/CQ/QP, MR registration, rkey exchange, READ/WRITE/CAS, CQ completion。 |
| Future Phase 7 | Two-node DSM transaction over RDMA verbs | 未來工作 | 需 Phase 6 先成功；目前不可宣稱 project-level DSM-over-verbs throughput。 |

## Current Open Decisions

| 決策 | 建議 | 狀態 |
|---|---|---|
| Adaptive routing default | `routing_margin_us=5`, `cost_window_ms=500`, `adaptive_object_scope=shard`, `hot_shards=8` | 已定且已用於 reduced final matrix；目前只支持 prototype-relative 分析，不支持 production policy claim |
| Final latency sample size | `10000` | 已定 |
| Final matrix duration/repetitions | 已執行 `duration_sec=10`, `repetitions=3` | 已完成 |
| Main thread counts | `1,2,4` | 已定 |
| Sold-counter policy | Main arbitration-isolation 使用 `per_product`; `global` 僅作 controlled bottleneck comparison | 已定 |
| Final report language | 目前最終研究報告為英文 `paper.md`；`report.md` 是中文舊快照 | 已定；若要中文最終論文，另行翻整 |

## Maintenance Rule

每次完成新實驗或更新結論時，需同步更新：

- 本文件狀態列。
- `HANDOFF.md` 的 Current Dataset / Next Research Steps。
- `paper.md` 的 evaluation 或 limitation。
- 對應 `results/*/summary.md` 與 CSV。

# 受限軟體 RDMA 原型環境下的 RDMA-style DSM 競爭感知交易路由

## 摘要

本專案研究在受限軟體 RDMA 原型環境下，RDMA-style DSM transaction runtime 應如何面對變動的 hot-object contention 並選擇交易路由策略。實驗環境為 virtualized Linux + Ubuntu 22.04 + Soft-RoCE/`rdma_rxe`，沒有硬體 RDMA NIC。因此，本文所有數據只能解讀為 transport diagnostics、protocol-level evidence 或 prototype-relative comparison，不能解讀為硬體 RDMA latency 或 throughput。

本文採用階段式證據鏈。Stage 0 記錄早期 broad prototype，並修正其方法論上的過度宣稱。Phase 1 驗證 two-VM Soft-RoCE verbs functionality 並建立 trust boundary。Phase 2 建立 local RDMA-style DSM/OCC protocol benchmark。Phase 3 研究 contention behavior、backoff、hot detection 與 static hybrid arbitration。Phase 4 加入 scalable arbitration queues 與 shared application metadata cleanup。Phase 5 加入 bounded transaction latency sampling 與 adaptive-routing prototype。Project-level two-node RDMA wrapper validation 與 two-node DSM transactions over verbs 被明確保留為 Future Phase 6 與 Phase 7。

## 1. 緒論

RDMA-based DSM 系統通常使用 one-sided READ/WRITE/atomic operations 來降低遠端 CPU 參與。但真實部署效能也受 RNIC、PCIe、switch、congestion control、memory registration 與 transport setup 影響。本專案沒有硬體 RDMA NIC，因此研究問題被刻意限制為：

```text
在只能使用 constrained software-RDMA prototyping 的情況下，
RDMA-style DSM transaction runtime 應如何在 hot-object contention 改變時選擇 transaction routing？
```

本文貢獻不是硬體 RDMA speedup claim，而是：

- 建立 two-VM Soft-RoCE trust-boundary characterization。
- 建立 RDMA-style DSM/OCC local protocol prototype。
- 分析 OCC、backoff、hot detection 與 static arbitration 的 contention behavior。
- 加入 per-object/per-shard arbitration queues。
- 加入 bounded rotating-sample latency sampler 與 debug-only full sampling。
- 完成 adaptive routing prototype、calibration 與 reduced final matrix evaluation。
- 明確規劃 Future Phase 6/7，但不把它們混入目前結論。

## 2. 背景與動機

DSM 提供 distributed memory 上的 shared-memory abstraction。RDMA-style DSM 可將 read、write 與 lock acquisition 對應到 one-sided verbs 或 atomic operations。OCC 很自然地適合這類設計：transaction optimistic read object versions、buffer writes、validate read set、acquire locks 或 CAS-style ownership、更新資料後釋放 lock。

OCC 在低競爭下表現良好，因為多數 transaction 都能順利 validate。但在 hot-object contention 下，transaction 會反覆 lock fail 或 validation fail。Backoff 能降低同步 retry storm，但不能移除衝突根源。Arbitration 可序列化 contested hot updates 並減少 retry storm，但會引入 queueing、centralization 與 head-of-line blocking。

核心系統問題因此是：runtime 何時該維持 optimistic path，何時該把工作導向 arbitration path？

## 3. 環境與方法論邊界

本專案環境為 virtualized Linux + Ubuntu 22.04 + Soft-RoCE/`rdma_rxe`。沒有硬體 RDMA NIC。因此本文禁止宣稱 hardware RDMA performance、RNIC offload、PCIe behavior、switch behavior、RoCE congestion-control behavior、bare-metal cluster scalability、production-ready DSM behavior、crash recovery 或 durability。

證據依用途分離：

| 證據類型 | 用途 | 不可用於 |
|---|---|---|
| Two-VM Soft-RoCE verbs validation | Transport functionality 與 diagnostic sanity，例如 RC path、QP/GID/CQ metadata、READ/WRITE/SEND perftest behavior | Hardware RDMA performance、RNIC offload、DSM transaction throughput |
| Local DSM/OCC protocol benchmark | 在相同 prototype 條件下比較 algorithm behavior 與 contention-control | Distributed DSM-over-verbs throughput |
| Latency / adaptive-routing prototype | 比較 routing、queueing、retry 與 tail latency 的 prototype-relative behavior | Hardware RDMA p99 latency |

Local DSM/OCC prototype 使用 RDMA-style one-sided READ/WRITE/CAS abstraction 進行 protocol development，但它不是完整 two-node verbs execution path。Phase 1 的 Soft-RoCE validation 包含 RDMA READ/WRITE 以及 SEND latency tests，因此整個專案也不能被描述成純 one-sided。Project-level two-node DSM transaction over RDMA verbs 與 project-level remote atomic/CAS validation 尚未完成。

## 4. Pre-Phase：初步原型與問題來源

在目前分階段評估前，舊專案位於 `/home/node1/RDSM/prev_project`，曾實作 broad RDMA-style systems prototype。檔案與報告確認它包含 FaRM-like DSM prototype、HERD-like key-value store、RDMA verbs wrapper、slab allocator / memory manager、OCC transaction path、performance monitoring utilities、OS-level analysis tools，以及 `farm_benchmark`、`herd_benchmark`、`os_analysis` 三個 benchmark executables。

這個早期 prototype 的價值在於工程 groundwork。它建立 C++17 module structure，實作 RDMA-style API 與 transaction abstractions，探索 slab allocation 與 memory-registration ideas，建立早期 benchmark/monitoring infrastructure，並暴露目前專案的核心問題：OCC 在低競爭時有效，但在 hot-object contention 下會陷入 retry 與 validation-failure storm。

然而，舊報告也有方法論上的過度宣稱。例如 RDMA WRITE 100-149 ns、RDMA READ 150-200 ns、ATOMIC CAS 200-300 ns、HERD GET/PUT tens of ns 等數字，現在只能視為 local prototype observations、local software-path measurements 或 measurement artifacts。除非它們是在 real two-node verbs path 上從 `post_send` 到 CQE completion 測得，否則不能稱為 hardware RDMA operation latency。

同樣地，Soft-RoCE/`rdma_rxe` 可用於 verbs compatibility 與 transport diagnostics，但不提供 RNIC hardware offload，也不能證明硬體 kernel-bypass 或 offload benefits。任何 WSL2/Soft-RoCE 與 Mellanox/ConnectX-like hardware RDMA 的直接比較，都只能作 qualitative context。

因此，舊專案應被定位為 Stage 0 problem discovery，而非 final performance evidence。舊專案提供 breadth；目前 RDSM 專案提供 methodology control。

## 5. Phase 1：Two-VM Soft-RoCE 可行性與 Trust Boundary

Phase 1 使用兩台 VM：

- Client：`node2`，`192.168.56.102`，`rxe0`
- Server：`node1`，`192.168.56.101`，`rxe0`

證據來自歷史名稱含 `phase3` 與 `phase3a` 的 artifacts，尤其是 `results/phase3_soft_roce_validation/` 與 `results/phase3/two_node_soft_roce_*`。在最終敘事中，這些被解讀為 Phase 1 evidence，因為 transport feasibility 先於後續 local protocol experiments。

驗證工具包含：

- `ibv_rc_pingpong`
- `ib_read_bw`
- `ib_write_bw`
- `ib_read_lat`
- `ib_write_lat`
- `ib_send_lat`

這些工具驗證 RC path 可跨兩台 VM 運作，並可觀察 QP/GID/CQ/transport-level metadata。但它們不驗證 hardware RDMA NIC performance、RNIC offload、PCIe/switch/congestion-control behavior、project-level DSM transaction throughput 或 project-level remote CAS correctness。

## 6. Phase 2：RDMA-style DSM/OCC Local Protocol Prototype

Phase 2 實作 local RDMA-style DSM/OCC substrate，包含 versioned objects、lock bits、object-specific data locks、RDMA-style READ/WRITE/CAS abstractions、read/write sets、validation、commit、retry 與 abort logic。

此階段刻意使用 local RDMA-style protocol benchmark，而不是 two-node DSM-over-verbs。原因是 Phase 1 已確立 Soft-RoCE 適合作為 verbs functionality validation，但不適合作為 absolute RDMA performance evidence。

Phase 2 的目的在於建立可控平台，供後續 contention-control experiments 使用。

## 7. Phase 3：Contention Behavior 與 Static Hybrid Arbitration

Phase 3 評估：

- `baseline_occ`
- `backoff_occ`
- hot detection as monitoring
- static hybrid arbitration

Hot-object contention 會反映在 lock failures、validation failures、retries 與 lower committed throughput。Backoff 可減少 retry synchronization。Hot detection 可識別 contested objects。Static hybrid arbitration 將 known-hot transactions 導向 serialized hot path，同時讓 cold transactions 留在 OCC path。

Aggregate averages 不應被視為 universal ranking，因為它混合 low-contention、high-contention、read-heavy、write-heavy、uniform 與 skewed workloads。真正重要的是 per-workload committed throughput、abort/retry behavior、lock/validation failures、hot-path ratio、correctness counters 與 latency percentiles。

## 8. Phase 4：Scalable Arbitration Queues and Cleanup

Phase 4 加入：

- `--arbitration-mode=global`
- `--arbitration-mode=per_object`
- `--arbitration-mode=per_shard`
- `--hot-shards=1|2|4|8|16|32`

此階段也記錄 queue wait、queue length 與 service time percentiles，用於判斷 global arbitration 是否過度序列化 unrelated hot objects，以及 per-object/per-shard 是否能減少 broad hot set 下的不必要 queueing。

Phase 4 sanity checking 發現 hot/cold locking-discipline bug。修正方式是讓 hot path 與 OCC cold path 都使用 deterministic object-id lock ordering 與 object-specific data locks。否則比較結果可能反映 synchronization inconsistency，而非 algorithm behavior。

Phase 4b 加入 `--sold-counter-mode=global|per_product`：

- `global` 保留 application-level shared metadata bottleneck。
- `per_product` 移除每筆交易都碰同一個 metadata object 的干擾，以隔離 arbitration queue behavior。

## 9. Phase 5：Bounded Latency Sampling 與 Adaptive Routing

Phase 5 加入 bounded transaction latency sampling。Latency 仍然只是 prototype-relative evidence，不是 hardware RDMA latency。

CLI 支援：

- `--latency-sampling=off|full|reservoir`
- `--latency-sample-size`
- `--latency-output`
- `--allow-dangerous-full-sampling`

預設 sample size 是 10,000。Full sampling 僅限 debug，且在 `duration_sec > 2` 或 `threads > 2` 時會被拒絕，除非明確 override。CLI mode 雖名為 `reservoir`，但目前實作是 bounded rotating sample，不是 statistically uniform Algorithm R reservoir sampling。因此 final latency numbers 只能作為相同 collection policy 下的 prototype-relative tail indicators。

Phase 5 也加入 minimal `hybrid_adaptive_arbitration_occ` prototype。Routing rule 比較 estimated OCC retry cost 與 estimated arbitration queue cost：

```text
estimated_occ_cost_us = base_occ_latency_us + expected_retries * retry_penalty_us
estimated_arbitration_cost_us = queue_wait_estimate_us + service_time_estimate_us
```

Calibration 已完成，選定 default 為 `routing_margin_us=5`、`cost_window_ms=500`、`min_samples_before_adapt=100`、`adaptive_object_scope=shard`、`hot_shards=8`，且已納入 reduced final matrix。Phase-change approximation 仍是多 process 近似，不能證明 continuous in-process adaptation。

## 10. Final Evaluation

### 10.1 Correctness

Final results 必須報告 invariant violations 與 duplicate commits。只有兩者皆為 0 才可稱為 correctness-clean。所有 final focused matrix 與 sold-counter comparison rows 均為 correctness-clean。

### 10.2 Adaptive Routing Calibration

Calibration 使用 `routing_margin_us=5,10,20` 與 `cost_window_ms=100,250,500`，workloads 為 `low_uniform_read95`、`mixed_hot4_write50`、`high_hot16_write100`。54 rows correctness-clean，選定 default 為 `routing_margin_us=5`、`cost_window_ms=500`、`adaptive_object_scope=shard`、`hot_shards=8`。

Calibration 顯示目前 adaptive prototype 偏保守：它能在低競爭下避免不必要 arbitration，但在 hot workload 下不一定能充分捕捉 static arbitration 的 gains。

### 10.3 Synthetic Workloads

Reduced focused final matrix 完成 324 synthetic rows：6 workloads、6 algorithms、threads 1/2/4、3 repetitions。所有 rows correctness-clean。

Synthetic workloads：

- `low_uniform_read95`
- `mixed_uniform_write20`
- `mixed_hot4_write50`
- `high_hot1_write100`
- `high_hot16_write100`
- `zipf99_write100`

低競爭 workload 中，OCC 與 backoff 仍具競爭力。`low_uniform_read95` 與 `mixed_uniform_write20` 中，arbitration 沒有穩定 hot set 可利用，額外 routing/queueing overhead 反而可能不划算。

在 explicit hot-object pressure 下，static arbitration 趨勢最明顯。`mixed_hot4_write50`、`high_hot1_write100`、`high_hot16_write100` 中，static per-object/per-shard arbitration 通常可提高 committed throughput 並降低 validation failures，但代價是 queue wait 與 prototype-relative p99 變化。

`zipf99_write100` 較曖昧。Static arbitration throughput 略有優勢，但 queue wait 與 p99 也上升；backoff 因 overhead 較低仍可保持競爭力。因此不應建立跨 workload 的 universal ranking。

Adaptive prototype 在 final matrix 中尚未勝過 static arbitration。它目前較適合作為 routing metrics 與 policy framework 的 proof of concept，而非成熟 production policy。

### 10.4 Application-like Workloads

Reduced focused final matrix 完成 216 application-like rows：4 workloads、6 algorithms、threads 1/2/4、3 repetitions。所有 rows correctness-clean。

Application-like workloads：

- `flash_sale_spike`
- `ticket_booking_hot_event`
- `ad_budget_read_heavy_dashboard`
- `long_tail_marketplace_zipf`

`flash_sale_spike` 與 `ticket_booking_hot_event` 近似 hot-object workload，static arbitration 能減少 retry/validation storm。`ad_budget_read_heavy_dashboard` 是 read-heavy，backoff/baseline 更合適。`long_tail_marketplace_zipf` 介於兩者之間，static arbitration 可稍有幫助，但必須同時報告 p99 與 queue wait。

### 10.5 Sold Counter Bottleneck Study

Controlled sold-counter comparison 完成 48 rows，位於 `results/final_sold_counter_comparison/`。設定包含 2 workloads、2 sold-counter modes、2 algorithms、threads 2/4、3 repetitions。所有 rows correctness-clean。

此比較應視為 data-model bottleneck study，而不是 universal algorithm ranking。`global` sold counter 代表 application-level shared metadata object；`per_product` sold counters 則移除每筆 successful write 都碰同一個 metadata object 的干擾。

在 `high_hot16_write100` 中，per-product 對 static 與 adaptive per-shard variants 都提高 throughput，說明 global metadata object 可能掩蓋 sharded arbitration 的好處。在 `mixed_hot4_write50` 中，趨勢較不一致，表示結論必須依 workload 與 thread count 解讀。

### 10.6 Phase-change Approximation

目前 phase-change script 是 multi-process approximation，會在不同 phase 間重啟 benchmark。它可作 low-risk approximation，但不能證明 continuous in-process adaptive reaction。

### 10.7 Latency Sampling Overhead

任何 latency result 都必須揭露 measurement overhead。Full sampling 不適合作 final runs。Bounded rotating sample 可限制記憶體，但仍有 overhead；且它不是 unbiased reservoir sampling。

## 11. 討論

OCC 適合低競爭。Backoff 適合中度衝突且 timing-related retry 較明顯的場景。Static arbitration 適合 hot objects 穩定且 routing 準確的場景。Per-object/per-shard arbitration 只有在 application data model 不另行引入 shared bottleneck 時，才能降低 global arbitration 的過度序列化。

Adaptive routing 仍有潛力，但目前 final matrix 顯示它尚未成熟到能穩定勝過 static arbitration。它的價值在於建立 routing decision metrics 與校準流程。

## 12. 限制

本專案沒有 hardware RDMA NIC、沒有 bare-metal cluster、沒有 RNIC offload measurement、沒有 PCIe/switch measurement、沒有 crash recovery、沒有 durability。Local DSM/OCC benchmark 不是 two-node verbs DSM transaction benchmark。Phase 1 transport validation 不包含 project-level remote atomic/CAS correctness。

另外，目前 `reservoir` CLI mode 是 bounded rotating sample，因此 tail-latency conclusion 只能被描述為 prototype-relative indicators。

## 13. 未來工作

### 13.1 Future Phase 6：Project-level Two-node RDMA Wrapper Validation

Phase 6 應驗證 project RDMA wrapper 是否能跨兩台 VM 執行：

- connection setup
- PD/CQ/QP setup
- memory-region registration
- remote address/rkey exchange
- RDMA WRITE/READ validation
- RDMA CAS validation
- CQ completion validation
- error handling and timeout

即便完成，也只能宣稱 project wrapper 能在 two-node Soft-RoCE 上執行 READ/WRITE/CAS，仍不能宣稱硬體 RDMA 效能。

### 13.2 Future Phase 7：Two-node DSM Transaction over RDMA Verbs

Phase 7 必須在 Phase 6 成功後才開始。最小 transaction 應包含：

- RDMA READ object/version
- RDMA CAS lock
- RDMA WRITE update
- RDMA WRITE unlock/version
- final value/version verification

可允許的 claim 僅是 minimal DSM/OCC transaction path 可在 two-node Soft-RoCE 上運作，不代表 hardware RDMA performance、production DSM、cluster scalability 或 durability。

## 14. 結論

本專案展示如何在沒有 RDMA 硬體的限制下，以受控方式研究 RDMA-style DSM 的 contention-aware transaction routing。Stage 0 說明早期 broad prototype 如何暴露問題並促成方法論修正。Phase 1 建立 Soft-RoCE verbs feasibility 與 trust boundary。後續 phases 以 local protocol benchmark 研究 OCC、backoff、static arbitration、scalable queues、bounded latency sampling 與 adaptive routing。

最終貢獻是一條清楚的 protocol-level evaluation path，而不是硬體 RDMA performance claim。

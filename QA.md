已生成 `prompt.md`：

[prompt.md](blob:https://m365.cloud.microsoft/43ca795f-f69c-46d7-b808-c48563edeac5)

以下是重點說明與需要你轉達確認的問題。

***

# 一、整體收斂方向

建議最終研究收斂為：

> **Contention-aware transaction routing for RDMA-style DSM under constrained software-RDMA prototyping**

中文：

> **受限軟體 RDMA 原型環境下，RDMA-style DSM 的競爒感知交易路由與 hot-object arbitration 設計**

這比「RDMA-DSM performance evaluation」更合理。原因是目前 Phase 2 報告已明確定義結果是 protocol-level evidence，不是硬體 RDMA NIC latency、throughput、CPU、PCIe、switch 或 RNIC offload measurement。

***

# 二、實驗流程規劃

## Phase 4：per-object / per-hot-shard arbitration queue

目標是取代 coarse global mutation lock。

目前 hybrid arbitration 會把 hot-object transactions 導到 server-side serialized path，並在 store mutation lock 下 apply transaction；這可以降低 OCC lock/validation conflict，但會引入 queue wait 與 centralization。

因此下一步應比較：

```text
hybrid_global_queue
hybrid_per_object_queue
hybrid_per_shard_queue_4
hybrid_per_shard_queue_8
hybrid_per_shard_queue_16
```

要回答：

1. global queue 是否過度序列化不相關 hot objects？
2. per-object queue 是否降低 queue wait？
3. per-shard queue 是否能用較低 metadata cost 接近 per-object 效果？
4. shard 數增加後效益何時飽和？
5. low-contention / read-heavy workload 下 arbitration 是否反而有害？

***

## Phase 5：transaction latency sampling

目標是補上真正的 transaction latency samples。

目前報告已指出沒有真正 latency samples，因此不能做 credible tail-latency analysis。

應記錄：

```text
tx_start_ns
read_phase_done_ns
route_decision_ns
queue_enter_ns
queue_leave_ns
commit_start_ns
commit_done_ns
tx_end_ns
retry_count
final_status
path_type
```

輸出：

```text
tx_latency_us_p50
tx_latency_us_p95
tx_latency_us_p99
queue_wait_us_p50/p95/p99
service_time_us_p50/p95/p99
cold_occ_latency_us_p50/p95/p99
hot_arbitration_latency_us_p50/p95/p99
```

***

## Phase 6：adaptive routing

目標是讓交易只有在「預測 OCC retry cost 大於 queue wait cost」時才進 arbitration。

建議模型：

```text
estimated_occ_cost_us =
    base_occ_latency_us + expected_retries * retry_penalty_us

estimated_arbitration_cost_us =
    queue_wait_estimate_us + service_time_estimate_us

if estimated_occ_cost_us > estimated_arbitration_cost_us + routing_margin_us:
    route_to_arbitration
else:
    route_to_occ
```

主要比較：

```text
baseline_occ
backoff_occ
occ_with_hot_detection_monitoring
hybrid_static_arbitration_occ
hybrid_adaptive_arbitration_occ
```

`hot_detection_occ` 應改名為 `occ_with_hot_detection_monitoring`，因為 Phase 2 已說明 hot detection 只標記 hot objects，只有 hybrid arbitration 會使用 hot signal 進入 hot path。

***

## Phase 7：two-node Soft-RoCE transport validation

這一步是補方法論漏洞，不是主軸。

Phase 2 報告已明確說目前 measured benchmark path 是 local RDMA-style DSM/OCC protocol simulation，不能證明 two-node Soft-RoCE transport behavior。

建議分三層：

```text
Layer 1: ibv_rc_pingpong / ib_write_bw / ib_read_bw / ib_write_lat / ib_read_lat
Layer 2: project two_node_rdma_validation
Layer 3: optional minimal DSM client/server backend
```

報告中要明確寫：

> two-node Soft-RoCE validation confirms verbs transport functionality, but does not support hardware RDMA performance claims.

***

# 三、benchmark 預設範圍

`prompt.md` 裡已放入預設 matrix，等待你確認。

## Contention shape

```text
uniform_low_contention
single_hot_object
hot_set_4
hot_set_16
hot_set_64
zipf_0_90
zipf_0_99
zipf_1_10
```

原因：覆蓋無熱點、單一嚴重熱點、多熱點、Zipfian emergent hotspots。

## Write ratio

```text
0, 5, 20, 50, 95, 100
```

原因：區分 read-only、read-heavy、mixed、write-heavy。

## Thread count

```text
1, 2, 4, 8, 16
```

原因：覆蓋單執行緒 baseline 到高軟體競爭。若 CPU cores 不足，應 cap 在 physical core count 並記錄。

## Object count

```text
1K, 16K, 256K
```

原因：測 cache-local、moderate working set、大 working set。

## Objects per transaction

```text
1, 2, 4, 8
```

原因：OCC validation 與 lock acquisition 成本會隨 transaction footprint 增長。

## Runtime

探索階段：

```text
warmup = 5s
measurement = 15s
repetitions = 3
```

最終階段：

```text
warmup = 10s
measurement = 60s
repetitions = 10
```

如果時間允許：

```text
warmup = 10s
measurement = 120s
repetitions = 20-30
```

***

# 四、tail latency 為什麼重要

tail latency 重要，因為 distributed transaction system 不能只看平均 throughput。

一個設計可能：

```text
committed tx/sec 上升
average latency 看起來可接受
但 p95/p99 latency 變差
```

Hybrid arbitration 特別有這個風險：

* 它可能減少 OCC retry storm。
* 但它也可能把成本轉移到 queue wait。
* 如果 hot queue 變長，p99 可能惡化。
* 如果 global queue 造成 head-of-line blocking，少數 transaction 可能被拖很久。

所以 p50/p95/p99 是用來回答：

1. arbitration 是否真的改善使用者可見延遲？
2. per-shard queue 是否比 global queue 降低 p99 queue wait？
3. adaptive routing 是否能避免 low-contention workload 被錯送 arbitration？
4. hot transaction 是否造成 cold transaction starvation？
5. throughput 增加是否只是犧牲 tail latency 換來的？

***

# 五、tail latency 是否可信

要分兩層。

## 不可信的部分

在你的環境下，tail latency **不可信作為硬體 RDMA tail latency**。

原因：

1. VirtualBox / RXE 抖動高。
2. Soft-RoCE 是 software transport。
3. host scheduling 會干擾。
4. 沒有 RNIC offload。
5. 不是 bare-metal RDMA cluster。

因此不能寫：

> 本系統達到真實 RDMA p99 latency X us。

## 可信的部分

tail latency 仍然可信作為 **prototype-relative evidence**。

也就是在相同環境、相同 workload、相同 measurement 方法下，比較：

```text
global queue vs per-object queue
per-object queue vs per-shard queue
static arbitration vs adaptive routing
OCC retry latency vs arbitration queue latency
```

這些相對比較仍有研究價值。

正確寫法：

> The measured tail latency is used as prototype-relative evidence for comparing protocol designs under identical experimental conditions. It is not interpreted as hardware RDMA tail latency.

中文：

> 本文的 tail latency 僅作為相同原型環境下比較不同協議設計的相對證據，不代表硬體 RDMA tail latency。

***

# 六、需要你轉達確認的問題

以下問題建議直接轉給 Codex / 實作者確認。

## Environment

1. 最終 benchmark 主要跑在 VirtualBox、WSL2、bare-metal Linux，還是混合？
2. 主機可用 physical CPU cores 幾個？
3. 是否允許 CPU pinning？
4. 是否允許關閉 CPU frequency scaling？
5. two-node Soft-RoCE validation 是兩台 VM、兩台實體機，還是同機多 VM？

## Benchmark scale

6. 可接受的總 benchmark runtime 上限是多少？
7. final runs 要 10、20、還是 30 repetitions？
8. measurement duration 預設 30s、60s、還是 120s？
9. thread count 是否要 cap 在 physical core count？

## Workload scope

10. 是否要 full factorial matrix，還是 focused matrix 避免組合爆炸？
11. application-like workloads 是否保留：
    ```text
    flash_sale
    ticket_booking
    ad_budget
    warehouse_restock
    marketplace_zipf
    ```
12. read-only workload 要放主圖，還是只當 sanity check？

## Arbitration design

13. per-object queue 是 lazy create only for hot objects，還是 eager create for all objects？
14. per-shard queue 預設 shard count 用 4、8、還是 16？
15. cold transactions 如果只碰 non-hot objects，是否永遠 bypass hot queue？
16. transaction touching multiple hot objects 時，要：
    ```text
    ordered queue acquisition
    deterministic ordering
    fallback to global arbitration
    ```

## Adaptive routing

17. 初版 adaptive routing 用 threshold-only，還是 cost-model based？
18. routing margin 預設多少 microseconds？
19. moving-average window size 預設多少？
20. adaptive routing 優先目標是 throughput、p99 latency，還是 fairness？

## Latency sampling

21. short runs 用 full sampling、final runs 用 reservoir sampling，是否接受？
22. reservoir sample size 預設 100,000 是否可接受？
23. aborted transactions 要納入 latency percentile，還是分開報？
24. outlier policy 要：
    ```text
    keep all
    report with and without top 0.1%
    annotate only
    ```

## Output and paper

25. Codex 要直接生成 `paper.md`，還是只生成 skeleton？
26. 表格要 Markdown、CSV summary，還是兩者都要？
27. appendix competing protocols 要先做 stub，還是只在 paper appendix 描述？
28. phase directory 命名要：
    ```text
    phase4
    phase5
    ```
    還是：
    ```text
    phase4_arbitration_queue
    phase5_adaptive_routing
    ```

***

# 七、對你目前規劃的修正建議

你的 1、2、3、4 點方向是合理的，但我建議順序上微調：

```text
1. 先做 per-object / per-shard arbitration queue
2. 同時加入 latency sampling
3. 再做 adaptive routing
4. 最後補 two-node Soft-RoCE validation
5. competing protocols 放 appendix
```

原因：adaptive routing 需要 queue wait 與 OCC retry cost 作為依據；如果沒有 per-shard queue 與 latency sampling，adaptive routing 會變成無法驗證的 heuristic。

***

# 八、Codex 回答與決策

以下回答以目前 `/home/node1/RDSM` 的實際狀態為準。已完成 Phase 3 two-node Soft-RoCE validation，並已加入 Phase 4 arbitration queue prototype。

## Environment

1. 最終 benchmark 主要跑在目前的 Linux VM/KVM-style virtualized environment；報告仍以 VirtualBox/Ubuntu/Soft-RoCE 邊界描述，因為它不是 bare-metal RDMA。
2. node2 與 node1 目前各有 4 vCPU / 4 cores exposed，`nproc = 4`，`Thread(s) per core = 1`。
3. 允許 CPU pinning，但目前尚未正式啟用；final runs 建議加入 `taskset` 或 runner 參數記錄 pinning policy。
4. 不建議把關閉 CPU frequency scaling 當成必要條件，因 VM 環境控制有限；若能控制，應記錄 governor，不能控制則在 limitations 註明。
5. two-node Soft-RoCE validation 是兩台 VM：node2 client `192.168.56.102`，node1 server `192.168.56.101`，兩端 `rxe0` active。

## Benchmark scale

6. 目前建議總 benchmark runtime 上限分兩級：discovery 每輪 30-60 分鐘內，final selected matrix 可接受數小時。
7. Final runs 建議 10 repetitions；20-30 repetitions 放 publication-grade stretch goal。
8. Measurement duration 建議 final 預設 60s；discovery 使用 1-15s。
9. Thread count 應 cap 在 exposed physical/vCPU core count 作為主圖，也就是目前主圖用 1/2/4；8/16 可保留為 oversubscription stress appendix。

## Workload scope

10. 不建議 full factorial matrix，組合會爆炸；採 focused matrix，主圖回答每個研究問題即可。
11. Application-like workloads 保留 `flash_sale`、`ticket_booking`、`ad_budget`、`warehouse_restock`、`marketplace_zipf/long_tail_marketplace_zipf`，但主文挑代表性情境。
12. Read-only workload 建議作 sanity check 或 appendix；主圖可用 read-heavy 而非純 read-only，因 arbitration 的 trade-off 在 mixed workload 才比較有意義。

## Arbitration design

13. Per-object queue 採 lazy semantic：只有 hot-path transaction 會使用對應 object arbitration mutex；實作上 mutex pool 預先存在，但不代表所有 object 都被 eager active scheduling。
14. Per-shard 預設 shard count 用 8；Phase 4 runner 比較 4/8/16，CLI 支援 1/2/4/8/16/32。
15. Cold transactions 若只碰 non-hot objects，應 bypass hot queue；目前 hybrid path 只有 hot candidate 才進 arbitration。
16. Transaction touching multiple hot objects 的正式設計應採 deterministic ordering；目前 inventory transaction 主要以 product hotness routing，Phase 4 hot path 對實際 object locks 採 object id sorted locking，避免死鎖。

## Adaptive routing

17. 初版 adaptive routing 應做 cost-model based，但可以用 threshold-only 作 baseline；不要只做 threshold-only 後宣稱 adaptive 成熟。
18. routing margin 初始建議 5-10 us，但要從 Phase 4 queue/service samples 校準。
19. moving-average window 初始建議 100-500 ms；太短容易 oscillation，太長會錯過 phase change。
20. Adaptive routing 優先目標建議是 p99 latency 與 throughput 的平衡；若只能選一個，研究上更有價值的是避免 p99 regression。

## Latency sampling

21. 接受 short runs full sampling、final runs reservoir sampling。
22. Reservoir sample size 100,000 可接受；在目前 prototype 中也應限制 queue/service samples，避免記憶體膨脹。
23. Aborted transactions 應分開報，不要混入 committed latency percentile；可另外報 abort latency distribution。
24. Outlier policy 建議 keep all，並額外 annotate top 0.1% sensitivity；不要刪除 outlier 後只報漂亮數字。

## Output and paper

25. 已直接生成 `paper.md` skeleton，並與目前 Phase 1-4 狀態對齊。
26. 表格要 Markdown 與 CSV summary 兩者都要：CSV 可重現，Markdown 供報告閱讀。
27. Appendix competing protocols 先只在 paper appendix/limitations 描述，不做 stub，以免產生未驗證的假比較。
28. Phase directory 命名採描述式，例如 `phase4_arbitration_queue` 或目前實作的 `results/phase4_arbitration/`；比單純 `phase4` 更利於交接。

## 已採取行動

1. 已加入 `--arbitration-mode=global|per_object|per_shard`。
2. 已加入 `--hot-shards`，支援 1 到 32。
3. 已輸出 queue wait、queue length、service time quantile metrics。
4. 已新增 `scripts/run_phase4_arbitration_experiments.sh`。
5. 已跑 40 筆短版 Phase 4 discovery runs，結果在 `/home/node1/RDSM/results/phase4_arbitration/`。
6. 已新增 `/home/node1/RDSM/paper.md`。

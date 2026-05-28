# RDSM 完整報告：RDMA-style DSM/OCC 協議原型、Hybrid Arbitration 與 Two-Node Soft-RoCE Validation

最後更新：2026-05-27 UTC

## 摘要

本專案建立一個 RDMA-style Distributed Shared Memory（DSM）與 Optimistic Concurrency Control（OCC）交易原型，用來研究 hot-object contention 下，不同交易控制策略的相對行為。Phase 2 已完成 local DSM/OCC benchmark，包含 `baseline_occ`、`backoff_occ`、`hot_detection_occ`、`hybrid_arbitration_occ` 四種策略，並以 flash sale、ticket booking、ad budget、warehouse restock 等應用形狀評估 throughput、retry、lock failure、validation failure、hot path ratio 與 correctness invariants。

Phase 3 新增 two-node Soft-RoCE validation：在 node2 client（192.168.56.102）與 node1 server（192.168.56.101）之間，以 `rxe0` 執行 perftest verbs-level sweep，驗證 RDMA WRITE、RDMA READ、SEND、RDMA WRITE bandwidth 的 RC queue pair transport path 是否真的跨兩台 Linux VM 跑通。這個階段的結論是：two-node Soft-RoCE transport path 成功建立並完成 28/28 組測試，但其數字只能作為 transport calibration 與環境可信邊界，不能被解讀為硬體 RDMA NIC 效能，也不能直接代表 Phase 2 DSM/OCC benchmark 的 end-to-end distributed performance。

本報告刻意不新增 DSM latency instrumentation。原因是目前專案要求先忽略 latency 相關任務增加；Phase 3 雖然解析 perftest latency 欄位，但只把它當作 Soft-RoCE transport validation 的診斷訊號，不用來主張 DSM tail latency 或交易延遲。

## 研究定位與主張邊界

本專案目前可以成立的研究定位是：

> 在沒有硬體 RDMA NIC 的條件下，透過 RDMA-style DSM/OCC 原型研究 hot-object contention 下的 transaction routing 與 contention-control protocol trends，並以 two-node Soft-RoCE verbs validation 界定 transport substrate 是否存在與其可信邊界。

可以主張的內容：

1. 在 local RDMA-style DSM/OCC prototype 中，hybrid arbitration 對 high-contention hot-object workloads 能降低 OCC lock/validation conflict symptoms。
2. 在 single hot product、broad hot write set、flash-sale spike 等情境，hybrid arbitration 的 committed tx/sec 明顯高於 baseline/backoff/hot-detection-only。
3. Backoff OCC 在 read-heavy 或 moderate contention 的情境仍有價值，因為它不需要 server-side serialization。
4. Hot detection only 更適合被視為 instrumentation / monitoring module，而不是完整 competing concurrency-control algorithm。
5. Two-node Soft-RoCE validation 證明目前 VM 環境可以建立跨節點 RC QP 並完成 verbs-level READ/WRITE/SEND/BW 測試。

不能主張的內容：

1. 不能說此系統達到硬體 RDMA NIC 效能。
2. 不能把 VirtualBox + Soft-RoCE latency/bandwidth 外推到 bare-metal RoCE 或 InfiniBand。
3. 不能說 Phase 2 benchmark 已是完整 two-node DSM transport benchmark；它仍是 local protocol simulation。
4. 不能用目前 perftest latency 數字討論 DSM transaction tail latency。
5. 不能宣稱 hybrid arbitration 已具備 production-ready crash recovery、durability 或 fault tolerance。

## 專案架構

主要目錄與檔案：

| 路徑 | 角色 |
|---|---|
| `src/dsm_object.*` | DSM object store、object metadata、global stats、mutation lock |
| `src/occ_engine.*` | OCC read/write/validate/commit path |
| `src/backoff.*` | fixed/exponential/randomized/contention-aware backoff |
| `src/hot_detection.*` | hot object detection counters 與 threshold logic |
| `src/server_arbitration.*` | hybrid arbitration 所需的 server-side serialized execution 支援 |
| `src/rdma_conn.*` | RDMA connection utility；目前 Phase 2 benchmark 主路徑未完整使用它作為 distributed benchmark transport |
| `experiments/phase2_dsm_benchmark.cpp` | Phase 2 local DSM/OCC benchmark executable |
| `scripts/run_phase2_*.sh` | Phase 2 benchmark matrix runners |
| `scripts/parse_phase2_results.py` | Phase 2 CSV / Markdown parser |
| `rdma_client_sweep.sh` | Phase 1 two-node perftest sweep 原始腳本 |
| `stat/` | Phase 1 two-node Soft-RoCE perftest legacy output |
| `scripts/run_phase3_two_node_soft_roce_validation.sh` | Phase 3 two-node Soft-RoCE validation runner |
| `scripts/parse_phase3_results.py` | Phase 3 parser，輸出 CSV 與 validation report |
| `results/phase2/` | Phase 2 raw run directories 與 summary CSV |
| `results/phase3/` | Phase 3 two-node validation raw outputs、CSV、report |

## 系統設計

### DSM Object Model

Prototype 把應用狀態放在一個 local DSM-style object store。每個 object 具有：

1. object id。
2. object type，例如 product stock、user balance、sold count。
3. value。
4. version。
5. lock / owner metadata。
6. per-object access、abort、lock-fail、hotness counters。

Inventory-style transaction 通常會讀 product stock 與 user balance；write transaction 會減少 stock、扣除 balance，並增加 sold count。Read-only transaction 只讀 object，不修改狀態。

### Baseline OCC

`baseline_occ` 使用典型 optimistic transaction path：

1. Transaction 先讀 object value 與 version。
2. Write set 內的 object 在 commit 時嘗試 lock。
3. 驗證 read set 版本沒有被其他交易改變。
4. 寫入新 value，增加 version，釋放 lock。
5. lock acquisition 或 validation 失敗時 retry，超過限制才 abort。

此策略在 low-contention workload 下通常足夠簡單且有效，但在 hot-object write-heavy workload 下容易出現 lock failure、validation failure 與 retry storm。

### Backoff OCC

`backoff_occ` 在 OCC retry path 加入 backoff。其設計目的不是消除衝突，而是避免所有失敗交易立即重試造成同步碰撞。Phase 2 結果顯示 backoff 在 moderate contention 或 read-heavy workload 有價值；它的優點是 overhead 較低，不需要集中式 server arbitration。

### Hot Detection

`hot_detection_occ` 會根據 object access count、abort count、lock fail count 等資訊標記 hot object。這個 variant 目前不改變 transaction execution path，因此它不應被視為與 hybrid arbitration 同等級的完整 concurrency-control algorithm；更精確的定位是「OCC with hot-object monitoring」。

### Hybrid Arbitration

`hybrid_arbitration_occ` 保留 cold transaction 的 OCC path，並把偵測或設定為 hot 的 product transaction 導向 server-side serialized path。其核心想法是：

1. Hot transaction 若繼續走 OCC，可能反覆 CAS-style lock acquisition、validation、retry。
2. 若 routing 準確，將 hot transaction 序列化可以避免大量無效競爭。
3. Cold transaction 仍留在 OCC path，避免所有 workload 都被集中化。

目前實作使用 coarse local mutation lock apply hot transaction。這是 prototype 合理的第一版，但它會引入 centralization bias：在單機 local prototype 中，一把 coarse mutex 的成本遠低於真實 distributed server arbitration 的 network、queueing、server scheduling、NUMA、failure-handling 成本。因此，本報告只把 hybrid 的結果解讀為 protocol trend，而不是可直接外推的 distributed RDMA 效能。

## Correctness Model

Phase 2 benchmark 檢查下列 invariants：

1. Product stock 不得 underflow。
2. User balance 不得 underflow。
3. `sold_count + final_stock = initial_stock`。
4. Duplicate commit count 必須為 0。
5. Invariant violation count 必須為 0。

目前 272 筆 Phase 2 parsed runs 中：

| 指標 | 結果 |
|---|---:|
| Total parsed runs | 272 |
| Focused trade-off runs | 144 |
| Application scenario runs | 32 |
| Invariant violations | 0 |
| Duplicate commits | 0 |
| Correctness status | PASS |

這表示在目前受測 workload 下，hybrid arbitration 雖然改變 hot transaction execution path，但沒有破壞基本 application-level invariants。不過這不是 crash-safety 證明；目前沒有 durability、recovery、replication 或 failover。

## Phase 2 實驗方法

Phase 2 是 local RDMA-style DSM/OCC protocol simulation。它不通過 two-node transport 跑完整 DSM benchmark，而是在同一個 process / VM path 中模擬 object store 與 transaction engine 的協議行為。

主要變因：

1. Algorithm：`baseline_occ`、`backoff_occ`、`hot_detection_occ`、`hybrid_arbitration_occ`。
2. Thread count：不同 worker thread 數。
3. Write ratio：read-heavy、mixed、write-heavy。
4. Access pattern：uniform、Zipfian、explicit hot products。
5. Hot product count：單一 hot product、多 hot product、uniform catalog。
6. Application case：flash sale、ticket booking、ad budget、warehouse restock。

主要 metrics：

1. `committed_tx_per_sec`
2. `abort_rate`
3. `retry_per_commit`
4. `lock_fail_count`
5. `validation_fail_count`
6. `hot_path_ratio`
7. `server_queue_wait_us_p50`
8. correctness counters

## Phase 2 Aggregate Results

Aggregate summary 混合了 low-contention、high-contention、read-heavy、write-heavy、uniform、skewed workloads，因此不能被視為 universal ranking。它只提供整體趨勢概覽。

| Algorithm | Runs | Commit tx/sec mean | Abort rate mean | Retry/commit mean | Lock fails mean | Validation fails mean | Hot path ratio mean |
|---|---:|---:|---:|---:|---:|---:|---:|
| backoff_occ | 68 | 1,503,686 | 0.000 | 0.001 | 683.5 | 586.8 | 0.000 |
| baseline_occ | 68 | 1,416,116 | 0.002 | 0.005 | 2,187.5 | 1,522.0 | 0.000 |
| hot_detection_occ | 68 | 1,306,946 | 0.003 | 0.006 | 1,944.7 | 1,528.7 | 0.000 |
| hybrid_arbitration_occ | 68 | 2,150,444 | 0.000 | 0.000 | 237.9 | 82.2 | 0.752 |

初步觀察：

1. Hybrid aggregate throughput 最高，但這不是「hybrid 永遠最好」的證明。
2. Hybrid lock/validation failures 明顯較低，符合 hot transaction 被導向 serialized path 的預期。
3. Backoff 的 abort/retry 指標很低，表示 retry timing mitigation 有效果。
4. Hot detection only 有額外 detection overhead，但沒有改變 execution path，因此 throughput 低於 baseline/backoff 並不意外。

## Phase 2 Focused Scenario Results

| Workload | Baseline tx/sec | Backoff tx/sec | Hot detection tx/sec | Hybrid tx/sec | Hybrid hot path | 解讀 |
|---|---:|---:|---:|---:|---:|---|
| high_hot16_write100 | 1,057,926 | 1,077,456 | 1,063,773 | 3,382,912 | 1.000 | 多個 hot products 且全寫入，OCC variants 持續競爭；hybrid 序列化 hot path 明顯勝出。 |
| high_hot1_write100 | 1,222,592 | 1,197,894 | 1,127,562 | 3,082,190 | 0.967 | 單一商品 flash-sale pressure 最符合 hybrid arbitration 假設。 |
| low_uniform_read95 | 3,111,043 | 3,299,601 | 3,215,478 | 3,328,468 | 0.123 | Low contention / read-heavy 下 hybrid 大多留在 cold path；優勢很小，需長時間重複驗證。 |
| mixed_hot4_write50 | 1,566,265 | 1,603,785 | 1,652,185 | 2,788,060 | 0.927 | 中度 hot-set contention 中，selective arbitration 有明顯幫助。 |
| mixed_uniform_write20 | 2,566,679 | 2,213,092 | 2,384,812 | 2,618,816 | 0.295 | Uniform mixed traffic 中 hot path 比例有限；hybrid 只略高，不應過度解讀。 |
| zipf99_write100 | 789,374 | 753,261 | 742,618 | 1,205,059 | 0.565 | Zipfian skew 形成 emergent hot objects；hybrid 有幫助但不像 explicit hot set 那麼強。 |

這組 focused results 是 Phase 2 最有說服力的資料，因為它直接檢驗研究假設：當 hot-object contention 明確且 routing 準確時，server-side serialized path 可以比 OCC retry storm 更有效。

## Phase 2 Application Scenario Results

| Scenario | Application | Best algorithm | Baseline tx/sec | Backoff tx/sec | Hot detection tx/sec | Hybrid tx/sec | Hybrid hot path | 解讀 |
|---|---|---|---:|---:|---:|---:|---:|---|
| ad_budget_hot_campaign | ad_budget | hybrid_arbitration_occ | 1,934,236 | 1,947,498 | 1,847,188 | 2,112,533 | 0.850 | Campaign hotspot 存在，但 write ratio 不如 flash sale 極端；backoff 與 hybrid 都合理。 |
| ad_budget_read_heavy_dashboard | ad_budget | backoff_occ | 2,398,931 | 2,763,486 | 2,521,288 | 2,220,779 | 0.040 | Read-heavy dashboard 不適合集中式 arbitration，簡單 cold path 較好。 |
| flash_sale_spike | flash_sale | hybrid_arbitration_occ | 1,130,004 | 938,882 | 1,170,430 | 3,338,807 | 0.990 | 極端單商品熱點，hybrid routing 幾乎完全命中。 |
| long_tail_marketplace_zipf | flash_sale | hybrid_arbitration_occ | 293,851 | 280,811 | 282,388 | 325,123 | 0.700 | Long-tail skew 下 hybrid 有幫助，但絕對差距有限。 |
| mixed_hot_catalog | flash_sale | hybrid_arbitration_occ | 774,359 | 659,494 | 717,437 | 1,128,660 | 0.950 | Hot catalog 下集中處理 hot writes 有利。 |
| ticket_booking_hot_event | ticket_booking | hybrid_arbitration_occ | 986,336 | 890,061 | 865,321 | 2,117,174 | 0.860 | 熱門場次/座位類似 hot product contention。 |
| ticket_booking_many_events | ticket_booking | hybrid_arbitration_occ | 717,012 | 800,118 | 730,664 | 916,456 | 0.390 | Demand 分散時 hybrid 優勢變小；需要更多 repetitions。 |
| warehouse_restock_uniform | warehouse_restock | hybrid_arbitration_occ | 1,407,856 | 991,097 | 1,161,502 | 1,538,132 | 0.010 | 幾乎不是 hot workload，hybrid 多半等同 cold-path OCC；短跑結果不可過度外推。 |

這組結果的重要性在於它包含反例：`ad_budget_read_heavy_dashboard` 中 backoff 最好，hybrid 不是萬能。這讓研究主張更可信，因為它把設計適用條件說清楚，而不是只挑 hybrid wins 的情境。

## Phase 3：Two-Node Soft-RoCE Validation

### 目的

Phase 3 的目的不是把 Phase 2 benchmark 改成真正 distributed DSM，也不是加入 latency analysis，而是補足一個審查者會要求的基本證據：

> 目前環境是否真的能在兩台 Linux VM 之間建立 Soft-RoCE verbs transport path？

本階段使用 node2 作為 client、node1 作為 server：

| Role | Hostname | IP | RDMA device |
|---|---|---|---|
| Client | node2 | 192.168.56.102 | rxe0 |
| Server | node1 | 192.168.56.101 | rxe0 |

兩端 `rdma link show` 均顯示 `rxe0` 為 ACTIVE / LINK_UP，並綁定 host-only network interface `enp0s8`。

### 工具與命令

Phase 3 新增：

```bash
RESULTS_DIR=./results/phase3 LAT_ITERS=1000 BW_DURATION=2 \
  ./scripts/run_phase3_two_node_soft_roce_validation.sh

python3 scripts/parse_phase3_results.py
```

Runner 會在 node1 透過 SSH 啟動 perftest server process，並在 node2 執行 client command。測試矩陣：

1. Tests：`ib_write_lat`、`ib_read_lat`、`ib_send_lat`、`ib_write_bw`
2. Payload sizes：8、64、256、1024、4096、16384、65536 bytes
3. Latency iterations：1000
4. Bandwidth duration：2 seconds

輸出位置：

| Artifact | 路徑 |
|---|---|
| Raw Phase 3 run | `results/phase3/two_node_soft_roce_20260527_225225/` |
| Summary CSV | `results/phase3/two_node_soft_roce_summary.csv` |
| Phase 3 report | `results/phase3/phase3_two_node_soft_roce_report.md` |

### Transport 成功證據

Phase 3 parser 從 perftest output 解析到：

1. `Transport type: IB`
2. `Connection type: RC`
3. `Link type: Ethernet`
4. `Mtu: 1024[B]`
5. `GID index: 1`
6. Local GID 包含 `192.168.56.102`
7. Remote GID 包含 `192.168.56.101`
8. Local/remote QPN 存在
9. 28/28 Phase 3 rerun rows 成功解析

這些資訊足以證明測試不是單機 in-process path，而是 node2 到 node1 的 Soft-RoCE RC verbs path。

### Phase 3 Summary

| Test | Rows | Size range | Mean latency us | Mean p99 us | Mean BW MB/s | Max BW MB/s | Mean MsgRate Mpps |
|---|---:|---:|---:|---:|---:|---:|---:|
| ib_read_lat | 7 | 8-65536 | 6,919.06 | 139,694.39 | 0.00 | 0.00 | 0.000000 |
| ib_send_lat | 7 | 8-65536 | 822.27 | 15,829.41 | 0.00 | 0.00 | 0.000000 |
| ib_write_bw | 7 | 8-65536 | 0.00 | 0.00 | 19.00 | 36.66 | 0.020198 |
| ib_write_lat | 7 | 8-65536 | 2,666.67 | 87,591.44 | 0.00 | 0.00 | 0.000000 |

此表的 latency mean 被少數 outliers 大幅拉高。例如 64 KiB READ latency 出現接近 1 秒的 p99，8B WRITE latency 也出現非常高的 max/p99。這不是硬體 RDMA 的特性，而是 VirtualBox + Linux RXE + guest scheduling + host-only network jitter 的結果。因此，本報告只把它當成「transport path 成功但 jitter 很高」的診斷資料。

### Phase 1 `/stat` Legacy 結果

Phase 1 既有 `/stat` 資料也被 Phase 3 parser 納入，作為歷史測量對照：

| Test | Payload bytes | Avg latency us | p99 us | p999 us | Avg BW MB/s | MsgRate Mpps |
|---|---:|---:|---:|---:|---:|---:|
| ib_read_lat | 2 | 302.54 | 485.69 | 939.14 | 0.00 | 0.000000 |
| ib_send_lat | 2 | 404.16 | 3,760.42 | 6,491.64 | 0.00 | 0.000000 |
| ib_write_bw | 65536 | 0.00 | 0.00 | 0.00 | 36.39 | 0.000582 |
| ib_write_lat | 2 | 455.15 | 2,899.51 | 24,915.93 | 0.00 | 0.000000 |

Phase 1 與 Phase 3 的共同點是：都證明 node2 與 node1 之間可以完成 Soft-RoCE verbs tests。差異是 Phase 3 做了 explicit payload sweep 並保存可重跑的 raw artifacts。兩者都不能被用來代表硬體 RDMA NIC。

## Single-Node 與 Two-Node 的比較

這裡的 single-node 指 Phase 2 local DSM/OCC protocol simulation；two-node 指 Phase 3 Soft-RoCE verbs-level transport validation。兩者回答的是不同問題。

| 面向 | Phase 2 single-node local DSM/OCC | Phase 3 two-node Soft-RoCE validation |
|---|---|---|
| 主要問題 | 不同 contention-control protocol 在 hot-object workloads 下的相對趨勢 | Soft-RoCE transport path 是否真的跨兩台 VM 跑通 |
| 執行路徑 | Local object store + threads + OCC/hybrid logic | node2 client -> node1 server，perftest verbs RC QP |
| 可比較指標 | committed tx/sec、retry、lock fail、validation fail、hot path ratio、correctness | perftest success、GID/QPN、READ/WRITE/SEND completion、bandwidth calibration |
| 不可比較指標 | 不代表網路 latency/bandwidth | 不代表 DSM transaction throughput |
| 主要結論 | Hybrid 在 hot-object contention 高且 routing 準確時有效 | Two-node Soft-RoCE path 存在且可完成 verbs-level tests |
| 主要限制 | 不是 distributed transport benchmark；coarse local lock 有 bias | VirtualBox/RXE jitter 高；不是硬體 RDMA；不是 DSM benchmark |

最重要的比較結論是：Phase 2 的 1M-3M tx/sec 是 local protocol simulation 的 transaction throughput，Phase 3 的 19-36 MB/s bandwidth 或百微秒到毫秒級 latency 是 Soft-RoCE verbs calibration。這兩組數字不能直接相除、不能合併成 end-to-end DSM performance，也不能互相替代。

## Phase 4：Scalable Arbitration Queue 初步實作

依照後續研究任務，本專案已加入 Phase 4 的核心機制：`hybrid_arbitration_occ` 現在支援三種 arbitration mode。

| Mode | CLI | 設計目的 |
|---|---|---|
| Global | `--arbitration-mode global` | 保留舊版 coarse global arbitration 行為，作為比較基準。 |
| Per-object | `--arbitration-mode per_object` | 以 hot product object 作為 arbitration key，避免不相關 hot objects 被同一條 global queue 過度序列化。 |
| Per-shard | `--arbitration-mode per_shard --hot-shards N` | 將 hot products 映射到有限數量 shard queue，在 metadata cost 與 queue parallelism 之間折衷。 |

新增輸出 metrics：

1. `arbitration_mode`
2. `hot_shards`
3. `server_queue_wait_us_p50/p95/p99/max`
4. `queue_length_p50/p95/p99`
5. `service_time_us_p50/p95/p99/max`
6. `hot_cold_interference_count`

新增腳本：

```bash
RESULTS_DIR=./results/phase4_arbitration DURATION_SEC=1 REPETITIONS=1 \
  ./scripts/run_phase4_arbitration_experiments.sh

RESULTS_DIR=./results/phase4_arbitration \
  python3 scripts/parse_phase2_results.py
```

目前已完成一輪短版 discovery/smoke matrix，共 40 runs，輸出於 `results/phase4_arbitration/`。這組資料證明 Phase 4 mode、metrics、parser 與 correctness path 可跑通，但每組只有 1 秒、1 repetition，因此不能作為正式論文數字。正式結論仍需要至少 15-60 秒 measurement、多次 repetition、confidence interval，以及更完整的 workload matrix。

初步觀察只能保守描述：

1. 三種 arbitration mode 都能維持 correctness invariants。
2. `high_hot1_write100` 下不同 queue mode 的差異不大，符合單一 hot object 本來就無法從多 queue parallelism 得到太多收益的直覺。
3. `low_uniform_read95` 下 per-object/per-shard queue wait 明顯較低，顯示 global queue 可能在低 hot-path 比例時引入不必要 head-of-line blocking；但短跑結果不可過度外推。
4. `high_hot16_write100` 與 `zipf99_write100` 的短跑數字仍受目前 object locking、sold-count shared object、VirtualBox scheduling 影響，正式分析前需要延長 duration 並增加 repetitions。

## 對 codex.md 建議的評估

`codex.md` 建議下一階段集中在：

1. per-object / per-shard arbitration queue。
2. true latency distribution。
3. two-node Soft-RoCE transport validation。

本次採納情況：

| 建議 | 是否採納 | 理由 |
|---|---|---|
| two-node Soft-RoCE transport validation | 已採納 | 這是目前最關鍵的可信邊界缺口；已完成 Phase 3 runner、parser、CSV、report。 |
| true latency distribution | 暫不採納 | 使用者明確要求先忽略 latency 相關任務增加；且目前 Soft-RoCE latency jitter 很高，不宜加入 DSM latency 結論。 |
| per-object / per-shard arbitration queue | 暫列 future work | 這是合理方向，但會改變核心 protocol implementation，需獨立設計與測試，不應與 Phase 3 validation 混在一起。 |

對 `codex.md` 的判斷：

1. 其論文骨架可用，因為它把 Introduction、Background、System Design、Correctness、Experimental Methodology、Results、Discussion、Limitations、Future Work 分開，符合系統論文敘事。
2. 需要優化之處是加入 Phase 3 作為獨立 validation chapter，而不是把 Soft-RoCE 結果混入 Phase 2 Results。
3. 需要刪弱 latency 相關主張；目前只能說 perftest latency 很不穩，不能當 DSM tail-latency evidence。
4. `hot_detection_occ` 應重新命名或重述為 monitoring baseline，避免 reviewer 誤以為它是一個完整 execution strategy。

## 論文式章節建議

建議未來論文版本採用下列結構：

1. Introduction
   - 無硬體 RDMA NIC 條件下如何研究 RDMA-style DSM 協議。
   - 本文不主張硬體 RDMA 效能。
   - 研究焦點是 hot-object contention 下的 contention-aware transaction routing。

2. Background
   - DSM。
   - OCC。
   - RDMA one-sided / two-sided verbs 概念。
   - Soft-RoCE 與 VirtualBox 限制。

3. System Design
   - Object metadata。
   - Baseline OCC。
   - Backoff OCC。
   - Hot detection。
   - Hybrid arbitration。

4. Correctness Model
   - Stock invariant。
   - Balance invariant。
   - Sold-count invariant。
   - Duplicate commit prevention。

5. Experimental Methodology
   - Phase 2 local protocol benchmark。
   - Phase 3 two-node transport validation。
   - Workload matrix。
   - Metrics。
   - Comparable / non-comparable boundaries。

6. Results
   - Focused contention workloads。
   - Application scenarios。
   - Conflict metrics。
   - Hot path ratio。
   - Two-node Soft-RoCE validation。

7. Discussion
   - Hybrid 何時有效。
   - Backoff 何時更合理。
   - Baseline 何時足夠。
   - Single-node simulation 與 two-node transport validation 的可信邊界。

8. Limitations
   - 無硬體 RDMA。
   - Phase 2 不是 distributed transport benchmark。
   - 無 crash recovery / durability。
   - 無 DSM true latency samples。
   - Hybrid coarse local mutation lock bias。

9. Future Work
   - Per-object / per-shard arbitration。
   - Adaptive routing。
   - End-to-end two-node DSM benchmark。
   - True latency sampling。
   - Durability and recovery。
   - 更多 repetitions 與 statistical confidence interval。

## 作業系統與分散式系統教授式審查

### 審查結論

以作業系統與分散式系統論文審查標準，本專案目前的評價是：

> Major Revision，但方向正確，且 Phase 3 後可信邊界比 Phase 2 更清楚。

本專案已有明確研究價值：它不是在誇大 Soft-RoCE 數字，而是在 local RDMA-style DSM/OCC prototype 中研究 contention-control protocol trends。Phase 3 補上 two-node Soft-RoCE validation 後，報告可以更誠實地說明：transport substrate 可用，但 benchmark 主體仍是 local protocol simulation。

### 優點

1. 研究問題具體：hot-object contention 下 OCC retry storm 如何緩解。
2. Baseline 設計合理：baseline OCC、backoff OCC、hot detection monitoring、hybrid arbitration 形成清楚比較。
3. Correctness counters 完整，且目前 272 筆 runs invariant violations / duplicate commits 皆為 0。
4. Focused workload 和 application workload 都有，能同時呈現 synthetic insight 與 application-shaped behavior。
5. 報告明確區分 protocol trend、Soft-RoCE validation、hardware RDMA performance，避免常見誇大。
6. Phase 3 runner/parser 讓 two-node validation 可重跑、可追溯。

### 主要問題

1. Phase 2 benchmark path 仍不是 end-to-end two-node DSM over RDMA verbs。
2. Hybrid arbitration 使用 coarse local mutation lock，可能放大單機 prototype 中的優勢。
3. 沒有真正的 DSM transaction latency samples，因此不能討論 p50/p95/p99 tail latency。
4. `hot_detection_occ` 作為 algorithm 名稱容易誤導；它目前只是 monitoring，不改變 execution path。
5. Phase 2 run duration 與 repetitions 偏短，只適合 trend discovery，不足以支撐 publication-grade quantitative claims。
6. Phase 3 Soft-RoCE latency 抖動極高，顯示 VirtualBox/RXE 環境不適合做絕對延遲結論。

### 建議改進

1. 實作 per-object 或 per-hot-shard arbitration queue，取代 coarse global mutation lock。
2. 加入 adaptive routing：只有當預測 OCC retry cost 大於 queue wait cost 時才導向 arbitration。
3. 將 Phase 2 benchmark 真正拆成 client/server，讓交易 read/write/commit path 可以通過 verbs transport。
4. 在未來版本加入 transaction latency sampling，但需與本階段分開，並搭配 pinning、warm-up、repetitions、outlier handling。
5. 將 `hot_detection_occ` 改名為 `occ_with_hot_detection_monitoring`。
6. 對每組 workload 增加 repetitions，報告 mean、stddev、confidence interval。
7. 增加更多 competing protocols，例如 queued locks、timestamp ordering、wound-wait、deterministic scheduling。
8. 若要主張 RDMA 系統效能，需在 bare-metal RDMA/RoCE NIC 或至少更可信的非 VirtualBox 環境重做 transport experiments。

### 最終評語

本專案目前最合理的論文題目不是「高效能 RDMA DSM 系統」，而是：

> Contention-Aware Transaction Routing in an RDMA-style DSM/OCC Prototype with Soft-RoCE Transport Validation

若能在下一階段完成 per-object/per-shard arbitration、end-to-end two-node DSM benchmark，以及嚴謹的 latency/statistical methodology，這個專案可從課程/原型報告提升到接近 workshop paper 的研究完整度。目前則是一個方向正確、邊界誠實、資料可追溯的 Major Revision 級系統原型。

## Reproduction

重建 Phase 2 executable：

```bash
cmake -S . -B build
cmake --build build -j2
```

重新產生 Phase 2 summary：

```bash
python3 scripts/parse_phase2_results.py
```

重新執行 Phase 3 two-node validation：

```bash
RESULTS_DIR=./results/phase3 LAT_ITERS=1000 BW_DURATION=2 \
  ./scripts/run_phase3_two_node_soft_roce_validation.sh

python3 scripts/parse_phase3_results.py
```

注意：執行 Phase 3 前需確認 node2 可 SSH 到 node1，且兩端 `rxe0` 都為 ACTIVE。

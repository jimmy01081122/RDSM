# RDSM 研究方向分析：資源約束下的學術收斂策略

**最後更新**：2026-06-01  
**定位**：針對 virtualized Soft-RoCE 環境的研究突破點分析

---

## 第一部分：現狀診斷

### 當前項目的成就與侷限

#### ✓ 已完成（可發表基礎）
- Phase 1-5：完整的 local protocol evaluation framework
- 540 行 correctness-clean 最終矩陣
- 誠實的邊界設定與方法論清晰度

#### ✗ 關鍵缺口
| 缺口 | 影響 | 原因 |
|-----|------|------|
| 無硬件 RDMA 驗證 | 無法論證實用性 | 資源限制 |
| 虛擬化開銷未量化 | 結論外推困難 | VM jitter 難以控制 |
| Adaptive routing 未成熟 | 新算法失效 | 成本模型過簡單 |
| 無 OS 層分析 | 性能黑盒 | 未涉及調度/內存管理 |
| 缺乏容錯機制 | 系統不完整 | 非初期重點 |

---

## 第二部分：可行的高價值方向

### 🎯 方向 1：OS-DSM 協同優化（**推薦強度：⭐⭐⭐⭐⭐**）

#### 研究核心問題
```
在虛擬化環境中，OS 調度、記憶體管理、虛擬化開銷如何與 DSM 
transaction routing 交互？是否能通過跨層協同減少性能損耗？
```

#### 具體研究議題

**1a. CPU Affinity 與 DSM 隊列親和性**
- **假說**：thread pin to core + per-shard arbitration queue affinity 
  → 減少 IPC overhead + L3 miss
- **實驗設計**：
  - Baseline：RDSM 現有配置（threads 不 pin）
  - Treatment 1：pin to core
  - Treatment 2：pin + queue affinity
  - Treatment 3：pin + NUMA-aware shard placement
- **測量**：
  - 線程遷移次數 (`perf sched`)
  - LLC misses, memory latency (`perf stat`)
  - Queue wait 分佈變化
- **預期成果**：發現調度是 10-30% 的隱藏開銷來源
- **發表場景**：OSDI/EuroSys 系統優化 paper

**1b. 虛擬化開銷的參數化模型**
- **測量集**：
  ```
  - VM exit latency (cause: interrupt, EPT fault)
  - Page fault cost under memory pressure
  - Virtual device I/O latency
  - VM context switch jitter
  ```
- **建立模型**：
  ```
  measured_latency(op) = hw_latency(op) + vm_overhead(op)
  where vm_overhead = base_exit_cost + memory_pressure_factor * page_faults
  ```
- **應用**：用模型反推硬件 RDMA 下的期望性能
- **發表場景**：Performance Modeling workshop 或 ASPLOS 評論

**1c. Memory pressure 對 OCC validation 的影響**
- **問題**：在 Soft-RoCE 虛擬機內，頻繁 page fault 
  → GC 暫停 → OCC read set 變陳舊 → validation fail 率↑
- **實驗**：
  - 逐漸增加 DSM 記憶體工作集大小（64MB → 256MB → 1GB）
  - 測量 major/minor page faults 與 OCC abort ratio 的相關性
  - 對比 Baseline OCC vs Hybrid Arbitration 對內存壓力的 resilience
- **預期發現**：Arbitration 在高內存壓力下更穩健（減少 retry storms）
- **價值**：解釋為什麼 arbitration 在"嘈雜"環境中優勢明顯

---

### 🎯 方向 2：新型一致性協議設計（**推薦強度：⭐⭐⭐⭐**）

#### 研究核心問題
```
在軟件RDMA原型中，能否設計出比OCC更適合contention-heavy的
一致性協議？不依賴硬件特性，完全在軟件層可實現。
```

#### 具體協議方向

**2a. 混合向量時鐘 (Hybrid Vector Clock) DSM**

當前局限：OCC 在高競爭下退化；Arbitration 在低競爭下過度序列化

新協議：
```
Transaction phases:
1. Read phase: 讀取 version + dependency vector
2. Tentative write: 緩衝到本地 write set
3. Adaptive validation:
   - Low contention path: OCC-style optimistic validation
   - High contention path: 基於 vector clock 的因果一致性 partial ordering
   - 無需全局鎖定
4. Commit: 原子性向量時鐘推進
```

**特點**：
- ✓ 允許受控的 out-of-order commits（比 OCC 寬鬆）
- ✓ 避免 arbitration 的 head-of-line blocking
- ✓ 自動適應工作負載競爭度

**實驗驗證**：
- RDSM 現有工作負載上與 OCC/Arbitration 對比
- 測量：commit latency 分佈、p99 tail latency、throughput
- 預期：在 `mixed_hot4_write50` 等中等競爭場景超越兩者

**發表潛力**：SOSP 系統論文（**若成果好**）

**2b. Ownership-based Conflict Resolution**

改進 OCC 的決定性：
- 每個 hot object 維護臨時 ownership lease（軟件模擬）
- Transaction 檢測衝突時，自動向 ownership 持有者 abort（而非隨機重試）
- Arbitration queue 複用 ownership 機制

實驗指標：
- Abort 決定性分析（重複 run 下是否同一 tx abort）
- Queue wait distribution

---

### 🎯 方向 3：真實工作負載適配研究（**推薦強度：⭐⭐⭐⭐**）

#### 研究核心問題
```
RDSM 現有工作負載是合成的。真實 e-commerce/金融應用的 DSM 
訪問模式如何？能否通過工作負載特化提升性能 30%+？
```

#### 具體實施

**3a. 工作負載特徵提取**

從實際應用析取：
| 應用 | 關鍵特徵 | 現有 RDSM 模型 | 改進點 |
|-----|--------|---------------|-------|
| TPC-C (OLTP) | 80% reads, clustered writes | mixed_uniform_write20 | 無分佈相關性 |
| TicketMaster-like | Bursty hot objects | high_hot1/16 | 缺乏 burst 時間模式 |
| Ad budget (real) | Time-series seasonality | zipf99 | 無時間維度 |
| Inventory DSM | Hierarchical object access | 無 | **新場景** |

**實驗設計**：
1. 開發 TPC-C 的 DSM 適配版本
   - 90 seconds warm-up + 10 second measurement
   - 8 warehouses, 10 districts/warehouse
   - District-based hot object (80% access to local district)
2. 並行 benchmark：
   - RDSM TPC-C-like workload
   - vs 標準 mixed_hot4_write50
3. 測量 per-transaction latency percentiles

**預期洞察**：
- 發現 TPC-C 訪問在聚集維度上與平面 hot-set 模型差異
- Adaptive routing 在聚集模式下可能更有效（因為 cost 更可預測）

**發表場景**：系統/DB 會議的工作負載特性分析論文

---

### 🎯 方向 4：容錯與恢復機制（**推薦強度：⭐⭐⭐**）

#### 研究核心問題
```
DSM 系統的 transaction log 與 checkpoint 策略如何設計，
使得在虛擬機故障/網絡分區時恢復成本最小？
```

#### 具體方向

**4a. Undo/Redo Logging 的 DSM 適配**

當前 RDSM 無故障恢復。補充：
- **Write-ahead logging (WAL) for DSM**：
  - 每個 write set entry 的 undo/redo record
  - Arbitration queue 的恢復點
- **Asynchronous checkpointing**：
  - 後臺定期 snapshot 所有 object versions
  - vs eager logging：對比吞吐量與恢復時間
- **故障注入測試**：
  - 模擬 VM crash（SIGKILL thread）
  - 測量恢復時間、遺漏數據量

**4b. Byzantine-tolerant Arbitration（進階）**
- 在多副本環境中，arbitration queue 如何應對副本故障
- 軟件層面實現 Raft-based coordination（不依賴硬件 consensus）

**發表場景**：系統容錯相關會議論文

---

### 🎯 方向 5：性能預測與模型驅動設計（**推薦強度：⭐⭐⭐**）

#### 研究核心問題
```
從 Soft-RoCE 原型測量中建立參數化性能模型，
推外到硬件 RDMA 場景，並反向設計系統參數。
```

#### 具體方法

**5a. 延遲組成分解**

```
Measured OCC latency = 
  + read latency (object fetch)
  + validation latency (lock + CAS)
  + write latency (buffer + commit)
  + vm_overhead (exit, page fault, etc)

Goal: Extract vm_overhead factor
```

測量策略：
- 在 host 和 guest 同時運行 `perf stat` 采集 IPC, LLC miss rate
- 比較 RDSM benchmark with/without Soft-RoCE
- 用迴歸分離硬件成分 vs VM 成分

**5b. 外推模型**

```
hardware_latency(op) ≈ measured_latency(op) / vm_overhead_factor

where vm_overhead_factor = f(workload_intensity, memory_pressure)
```

驗證：
- 若有機會訪問真實 RNIC 環境（collaborative 論文），用模型預測實測
- 評估模型精度 (MAPE)

**5c. 反向設計**

給定硬件 RDMA 延遲預期，用模型指導：
- Optimal arbitration queue size
- Ideal shard count
- Cost model parameters for adaptive routing

**發表場景**：系統性能建模專題

---

## 第三部分：推薦研究路線圖

### **Phase A（立即，1-2 個月）：OS-DSM 協同 + 性能模型**

#### A1. CPU Affinity 與隊列親和性（**3 週**）
```
實驗：pin threads + shard affinity
成本：中等（改動 RDSM thread pool + affinity mask）
風險：低（增量式改動，無需改 algorithm）
預期論文：性能優化短論文 / workshop
```

#### A2. 虛擬化開銷參數化（**3 週**）
```
實驗：分解 perf stat 中的 VM exit, page fault 開銷
成本：低（測量，無編程需求）
風險：低
預期論文：性能模型分析
```

**Phase A 產出**：
- 1 個 workshop paper（affinity 優化）
- 1 個 technical report（VM 開銷模型）
- **改進 RDSM 實現**（加入 affinity 作為最佳實踐）

---

### **Phase B（並行，2-3 個月）：協議創新 OR 工作負載特化**

#### B1. 向量時鐘混合協議（高風險高收益）
```
實驗：實現 Hybrid VC-DSM 協議
成本：高（~2K LOC C++）
風險：高（算法可能無法收斂）
預期論文：SOSP-level 系統論文（若成功）
```

OR

#### B2. TPC-C DSM 適配（低風險中收益）
```
實驗：RDSM 上實現 TPC-C-like workload
成本：中（~500 LOC，主要是 workload generator）
風險：低
預期論文：系統評估/工作負載分析論文
```

**建議選擇 B2**（作為穩健之選），同時 B1 作為"if time permits"的探索。

---

### **Phase C（後期，3-4 個月）：容錯機制**

#### C1. WAL + Checkpoint 實現
```
成本：高（transaction log design 複雜）
但分解性好：
  - Week 1-2: WAL design & proto
  - Week 3: Checkpoint mechanism  
  - Week 4: Recovery validation
```

預期論文：系統論文中的"容錯"章節

---

## 第四部分：每個方向的發表目標

### 最現實的發表路徑

#### ✓ **方向 1（OS-DSM）發表策略**
```
高度推薦：直接發表 systems 會議

可選投稿順序：
1. [優先] EuroSys / ATC（主會）
   - CPU affinity 優化 + VM 開銷模型
   - "Bridging Virtual Machines and DSM: Scheduling & Memory Co-design"

2. [次選] ASPLOS（如果性能模型夠深）
   - 特別是"performance modeling"主題

3. [保底] SOSP ERC / SYSTOR workshop
```

#### ⚠️ **方向 2（新協議）發表策略**
```
高風險。若成功則：
  → SOSP / OSDI 系統論文（tier-1）

若不成功（協議無法超越 OCC+Arbitration）：
  → Downgrade 為"protocol design space exploration" workshop 論文

風險緩解：
  - 同時實施 B2（工作負載特化）作為 backup
  - 保證至少有 1 篇可發表的成果
```

#### ✓ **方向 3（工作負載）發表策略**
```
穩健方向。投稿：
  - SIGMOD/VLDB（DB 側重）
    "Understanding DSM Access Patterns: TPC-C Case Study"
  
  或
  
  - OSDI/EuroSys（系統側重）
    "Workload-Aware Transaction Routing in Distributed Shared Memory"
```

#### ✓ **方向 4（容錯）發表策略**
```
可作為主論文的一部分，或獨立的系統論文：
  - SIGMOD（transaction + recovery）
  - EuroSys（availability 主題）
```

---

## 第五部分：優先排序與資源配置

### **時間表與研究人力配置**

假設研究團隊 1-2 人全職，或 1 人 2/3 時間：

#### **建議優先序（含時間預算）**

| 順序 | 方向 | 投入 | 發表難度 | 預期時程 | 優先理由 |
|-----|-----|------|--------|--------|--------|
| **1** | 方向1A (CPU Affinity) | 3 週 | 低 | 2026/07 | 快速小論文 + 改進RDSM |
| **2** | 方向3 (TPC-C workload) | 4 週 | 低 | 2026/08 | 穩健成果，真實應用 |
| **2b** | 方向1B (VM overhead model) | 2 週 | 低 | 2026/08 | 輔助理論支撐 |
| **3** | 方向4 (容錯) | 6 週 | 中 | 2026/09 | 系統完整性 |
| **4** | 方向2 (新協議) | 8 週 | 高 | 2026/10 | 高風險，時間充足再做 |

---

### **推薦具體執行計劃**

#### **Next 2 Weeks: Phase A1 啟動**
```
Day 1-3:   代碼 audit，理解 RDSM thread model
Day 4-7:   實現 CPU affinity + shard queue mapping
Day 8-10:  基準測試 vs baseline （3 重複）
Day 11-14: 寫作與數據分析
```

**交付物**：
- 修改的 RDSM 源代碼（affinity 分支）
- 4 個 workloads × 2 config (with/without affinity) 的矩陣
- 初稿：3-page 短論文稿

#### **Weeks 3-6: Phase B2 並行（TPC-C 工作負載）**
```
Week 3-4: TPC-C 工作負載生成器開發
Week 5:   基準測試與數據采集
Week 6:   分析 & paper draft
```

**交付物**：
- `workload_tpc_c.cpp`（RDSM 框架下的 TPC-C）
- 性能對比分析圖
- 5-page 工作負載論文

---

## 第六部分：降低風險的檢查點

在每個階段末期，檢查以下指標以決定是否進行下一階段：

### **Phase A 檢查點（2 週末）**
```
✓ CPU affinity 改動是否穩定（無 segfault / data race）？
✓ Correctness 仍為 clean（0 invariant violations）？
✓ 性能改善是否顯著（>5% in target workloads）？
✓ 結果是否重現（3 重複的方差 < 10%）？

Go / No-Go 決策：
  ✓ All pass  → 立即投 workshop
  ⚠️ Most pass → Refine & retry
  ✗ 2+ fail   → Pivot 到其他方向
```

### **Phase B 檢查點（4 週末）**
```
若選擇 B1（新協議）：
  ✓ 基本實現是否編譯無誤？
  ✓ 低競爭工作負載是否通過 correctness 檢查？
  ✓ 初步性能是否接近 OCC（即使未超越）？
  
  Go → 繼續投入
  No-Go → 立即停止，轉向 B2

若選擇 B2（TPC-C）：
  ✓ Workload 生成器是否準確反映 TPC-C 特性？
  ✓ 與 synthetic workloads 的性能對比是否合理？
  
  Go → 撰寫完整論文
  No-Go → 擴展到其他應用（Retail, Flights）
```

---

## 第七部分：與硬件驗證的銜接（Future Phase 6-7 策略）

雖然當前無硬件 RDMA，但可通過以下方式準備：

### **立即可做的準備工作**

**7.1 模型驗證對標**
```
即使無硬件，可聯繫業界 / 其他研究組：
  - Mellanox 學術計劃（可能提供遠程 RNIC 訪問）
  - 其他大學 RDMA 測試床（e.g., 清華、CMU）
  - 商業雲（AWS Elastic Fabric Adapter）提供按時計費測試
```

**7.2 理論模型強化**
```
即使無實測，可通過：
  - 現有 RDMA 論文中的性能數據（FaRM, HERD 等）
  - 建立回歸模型，預測硬件上的表現
  - 用論文聲明："Hardware validation planned for future work"
```

**7.3 協議設計的硬件友好性**
```
新協議設計時，保持硬件可實現性：
  - 如 VC-based 協議，確保 CAS / atomic 操作數量有限
  - 不依賴無界的軟件數據結構
  - 易於遷移到硬件實現
```

---

## 第八部分：推薦研究主題總結

### **最具性價比的研究組合**

我作為研究者，會這樣規劃：

#### **立即執行（未來 3 個月內，確保產出）**

1. **CPU Affinity Optimization for DSM**（方向 1A）
   - 投稿：EuroSys / ASPLOS workshop
   - 發表難度：低 ✓
   - 實用價值：改進 RDSM 基準

2. **TPC-C Workload Characterization in DSM**（方向 3）
   - 投稿：VLDB / SIGMOD 工作負載主題
   - 發表難度：低-中 ✓
   - 學術價值：真實應用指導

#### **選擇性執行（根據進度與興趣）**

3. **VM Overhead Modeling & Extrapolation**（方向 1B + 5）
   - 投稿：ASPLOS / Performance Modeling workshop
   - 發表難度：中
   - 學術價值：性能預測能力

4. **Hybrid Vector Clock DSM Protocol**（方向 2 - 高風險）
   - 投稿：SOSP / OSDI
   - 發表難度：高
   - 學術價值：新協議 + 算法貢獻
   - **只在前三項成功後，時間充足時執行**

#### **後期考慮**

5. **Transaction Logging & Recovery**（方向 4）
   - 作為完整系統論文的一部分
   - 或獨立工作負載特化論文

---

## 第九部分：最終建議

### **我的學術判斷**

如果我是 RDSM 項目的研究顧問，我會建議：

#### **A. 近期（6 個月內）的收斂目標**

✓ **輸出 2-3 篇可發表的論文**：
  1. 性能優化論文（CPU affinity）→ EuroSys workshop
  2. 工作負載論文（TPC-C）→ VLDB/SIGMOD
  3. 性能模型論文（VM overhead）→ workshop / TR

✓ **改進 RDSM 代碼庫**：
  - 加入 CPU affinity 作為標準優化
  - 集成 TPC-C workload 生成器
  - 完整的文檔與可重現性支持

#### **B. 中期（6-12 個月）的選擇**

**情景1：已有 2 篇論文接受 → 投資方向 2（新協議）**
  - 目標：發表頂級系統論文
  - 風險：可接受（已有穩健基礎）

**情景2：正準備投稿階段 → 同步執行容錯機制（方向 4）**
  - 豐富系統功能
  - 為硬件驗證前做準備

#### **C. 長期（1-2 年）的願景**

**明確 Future Phase 6 的實現計劃**：
  - 評估是否可訪問硬件 RNIC（雲、合作機構）
  - 基於 Soft-RoCE 建立的模型進行硬件驗證
  - 發表"From Soft-RDMA Prototyping to Hardware: Lessons & Validation" 綜合論文

---

## 附錄：相關工作與靈感來源

建議閱讀以下論文，獲取研究靈感：

| 主題 | 推薦論文 | 相關性 |
|-----|--------|-------|
| OS-DSM interaction | "NUMA-Aware DSM" (若存在) | 調度協同 |
| VC-based protocols | "Causal Memory" (Amoeba) | 協議設計 |
| TPC-C 分析 | VLDB SIGMOD historical | 工作負載特徵 |
| VM overhead | "Virtual Machine Performance Isolation" | 性能建模 |
| Recovery in DSM | FaRM logging strategy | 容錯設計 |
| Adaptive routing | "Cost-based routing" (PostgreSQL) | 決策理論 |

---

**結論**：在資源約束下，通過 **OS-DSM 協同 + 真實工作負載特化 + 性能建模**，可以在 2-3 個月內產出 2-3 篇學術論文，同時為未來硬件驗證奠定基礎。

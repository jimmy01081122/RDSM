# RDSM 研究方向決策備忘錄

**對象**：RDSM 項目研究團隊  
**編寫日期**：2026-06-01  
**性質**：學術顧問建議

---

## 核心建議：一句話總結

> **在資源約束下，通過 OS-層優化 + 真實工作負載適配 + 性能建模，在 6 個月內產出 2-3 篇可發表的論文，並為未來硬件驗證奠定基礎。**

---

## 現狀評估

| 維度 | 評價 | 關鍵事實 |
|-----|------|--------|
| **已完成工作** | ✓ 扎實 | 540 行 correctness-clean 矩陣；方法論清晰 |
| **新算法貢獻** | ⚠️ 受限 | Adaptive routing 過於保守；無超越靜態策略的場景 |
| **可發表性** | ⚠️ 中等 | 拆分為 2-3 篇 workshop/期刊論文較現實；全章節投 SOSP/OSDI 風險高 |
| **硬件驗證** | ❌ 缺失 | 無硬件 RDMA 環境；虛擬化開銷未量化；外推能力弱 |

---

## 推薦研究方向優先度

### 🥇 **優先級 1：CPU Affinity & OS-DSM 協同**（立即執行）

#### 為什麼？
- ✓ **快速贏（Quick Win）**：3-4 週可出論文
- ✓ **直接改進 RDSM**：實現成果可複用
- ✓ **填補空白**：當前無 OS 層分析
- ✓ **穩健**：低技術風險

#### 預期成果
```
論文 1：「CPU Affinity Optimization for Virtual DSM」
        → EuroSys workshop（60-70% 接受率）
        
改進：RDSM 實現 + 5-10% 性能改善
```

#### 時間投入
- 開發：3-4 週
- 實驗：1 週
- 撰寫：3-5 天

#### 預期發表流程
```
Week 3-4  → 論文初稿完成
Week 5-6  → 投稿 workshop（deadline 通常提前 1-2 個月）
Month 2-3 → 接受 + 會議演講
```

---

### 🥈 **優先級 2：TPC-C 工作負載特性分析**（並行執行）

#### 為什麼？
- ✓ **學術價值**：真實 OLTP 工作負載應用
- ✓ **補充論文**：與 Affinity 論文互補
- ✓ **評估工具**：為他人提供 DSM benchmark
- ✓ **穩健性強**：無算法風險

#### 預期成果
```
論文 2：「Characterizing TPC-C Behavior in RDMA-style DSM」
        → VLDB/SIGMOD 工作負載分析分軌（20-30% 接受率）
        
或
        
        → EuroSys 系統評估論文（30-40% 接受率）
        
交付：workload_generator_tpcc.h（可供後人使用）
```

#### 時間投入
- TPC-C 生成器：2-3 週
- 實驗：1 週
- 撰寫：1 週

#### 發表方向
```
優先投稿：
  1. VLDB/SIGMOD（工作負載特性軌道）
  2. EuroSys（系統評估軌道）
  3. 聯繫應用組（如電商、金融 DSM 人士）做行業應用論文
```

---

### 🥉 **優先級 3：性能模型與虛擬化開銷**（可選，時間充足執行）

#### 為什麼？
- ✓ **填補現有論文的理論基礎**
- ✓ **支持外推到硬件**
- ⚠️ 但工作量大，時間成本高

#### 預期成果
```
技術報告 / Workshop 論文（ASPLOS Performance Modeling Track）

成果：参数化模型 + 硬件延遲外推框架
```

#### 執行條件
```
只有在 Paper 1 & 2 都成功投稿後，才投入此方向
```

---

### ❌ **不推薦（至少當前不做）**

| 方向 | 原因 |
|-----|------|
| **新型一致性協議** | 高風險，算法可能無收斂；若失敗則無 backup |
| **容錯機制** | 工作量大（6-8 週），但學術價值不如前兩者 |
| **分佈式 DSM over 真實 RDMA** | 資源限制，無法實施 |

---

## 關鍵成功指標 (KPI)

### 短期（3 個月）
```
✓ 2 篇論文初稿完成
✓ 投稿 1 篇 workshop 論文
✓ 收到 1-2 個 review feedback
✓ RDSM 代碼有可複用改進（affinity module）
```

### 中期（6 個月）
```
✓ 2 篇論文錄用（workshop + 一級會議或期刊）
✓ 在線發表 / 會議演講 1-2 次
✓ 可選：第 3 篇論文投稿或已錄用
```

### 長期（1 年）
```
✓ RDSM 系統文章在社群中有一定引用
✓ 基礎已為 Future Phase 6（硬件驗證）奠定
✓ 團隊已有 3-4 篇發表記錄
```

---

## 每一步的決策樹

```
現在（Week 1）
  ↓
執行 Affinity 研究（Week 1-4）
  ↓
  ├─ 成功（性能提升 > 3%，correctness clean）
  │   ↓
  │   準備投稿 workshop
  │   並同步執行 TPC-C（Week 2-4）
  │
  └─ 失敗（性能無改善或崩潰）
      ↓
      改進或 pivot → 改為純性能模型研究
      
執行 TPC-C 研究（Week 2-5）
  ↓
  ├─ 發現有趣現象（e.g., TPC-C 適應性路由更好）
  │   ↓
  │   擴展為更完整的工作負載分析論文
  │
  └─ 結果平凡（TPC-C 與 synthetic 無本質差異）
      ↓
      論文改為 technical report
      或加上性能建模來補強

Month 2-3
  ↓
  Affinity 論文已投稿或錄用
  ↓
  決策：
  ├─ 有餘力 & 興趣 → 執行方向 3（性能模型）
  ├─ 時間緊張 → 打磨 TPC-C 論文，準備投期刊
  └─ 有新靈感 → 探索新方向（但需通過 advisor 評估）
```

---

## 投稿策略

### 論文 1：CPU Affinity Optimization

**第一投稿順序**（推薦）：
```
1. [Primary]   EuroSys 2027 workshop
   - Deadline: 通常在 11 月左右
   - 接受率：60-70%（workshop）
   - 時間窗口：充足

2. [Secondary] ASPLOS 2027 Poster Session
   - Deadline: 10 月中旬
   - 錄用率：70-80%（poster）

3. [Fallback]  SYSTOR 2027, ATC 2027
   - 較寬鬆的 deadline
```

### 論文 2：TPC-C Workload Analysis

**第一投稿順序**（推薦）：
```
1. [Primary]   EuroSys 2027 Main Conference
   - Deadline: 6 月初
   - 接受率：15-20%（main）
   - 需早準備

2. [Alternative] VLDB/SIGMOD 2027 工作負載軌道
   - Deadline: 11-12 月
   - 接受率：20-30%（工作負載軌道較 main 容易）

3. [Fallback]  ATC 2027, OSDI 2027
```

### 論文 3：Performance Modeling（可選）

```
投稿：ASPLOS 2027 Performance Modeling Track
或    系統模型專題研討會
```

---

## 資源需求與預算

### 開發時間（假設全職 1 人）
```
Affinity 研究        12-14 週
TPC-C 研究           10-12 週
性能建模（可選）     8-10 週
──────────────────
最少項目：22-26 週（5.5-6.5 個月）
```

### 計算資源
```
✓ 已有 RDSM 環境（虛擬機）
✓ 已有編譯環境（CMake, libibverbs, librdmacm）
✓ 不需額外硬件
```

### 人力
```
推薦配置：
  - 1 人全職（研發 + 實驗）：6 個月
  - 或 2 人並行（一人 Affinity，一人 TPC-C）：3 個月
  - Advisor：月度 1-2 次審查（20-30 分鐘 meeting）
```

---

## 潛在風險與應對

| 風險 | 可能性 | 影響 | 應對 |
|-----|--------|------|------|
| Affinity 改善不顯著 | 20% | Medium | Pivot 到性能建模；或加入更多 metrics（L3 miss rate, context switch count） |
| TPC-C 與 synthetic 差異小 | 30% | Low | 改為 technical report；或擴展到多個應用（Retail, Finance） |
| 新算法協議仍無突破 | 50% | High | **不執行此方向**（按計劃已避免） |
| 虛擬機環境不穩定 | 10% | High | 改為分析性研究（不依賴實驗測量） |
| 投稿被拒 | 40% (first round) | Medium | Iterate → 重投其他會議；或作為 technical report 存檔 |

### 風險緩解策略
```
✓ 前 2 週完成可行性驗證（smoke test）
✓ 每週 checkpoint → 及早發現問題
✓ 備選方向明確（不依賴單一方向成功）
✓ 論文拆分策略：即使一篇被拒，至少有 1-2 篇可投其他會議
```

---

## 與利益相關者的溝通

### 給 Advisor 的推銷詞

```
「當前 RDSM 已完成本地協議驗證（540 行正確的矩陣）。
新方向不在算法創新，而在三個互補的實踐洞察：

1. OS 層面：CPU affinity 與 DSM 的協同
   → 可改進系統實現，發表 workshop/poster 論文

2. 應用層面：真實 OLTP 工作負載（TPC-C）
   → 驗證現有算法的適應性，發表工作負載論文

3. 理論層面：虛擬化開銷的參數化模型
   → 為硬件外推奠定基礎，發表性能建模論文

預期 6 個月內產出 2-3 篇可發表成果，
為 Phase 6（硬件驗證）做好理論準備。」
```

### 給審查委員會的投稿詞

```
「本工作克服了軟件 RDMA 原型的固有限制，
通過三個層次的改進（系統 + 應用 + 理論）
打造更接近生產環境的 DSM 評估框架。」
```

---

## 時程表總結

| 時間 | 里程碑 | 預期產出 |
|-----|------|--------|
| **Week 1-4** | Affinity 研究完成 | Code + 實驗數據 + 初稿 |
| **Week 2-5** | TPC-C 研究完成 | Workload generator + 實驗數據 + 初稿 |
| **Week 6-8** | 論文定稿 & 投稿 | 投稿 2 篇（1 workshop + 1 main/journal） |
| **Month 3** | 首個 Review feedback 收到 | 迭代準備 |
| **Month 4-6** | 論文接受 & 發表 | 1-2 篇錄用；可選第 3 篇投稿 |

---

## 最後的話

### 為什麼這個方向對你有益？

1. **實踐性**：改進的代碼可直接用於 RDSM，不是紙上談兵
2. **發表概率高**：2-3 篇論文中至少 1-2 篇有 60% 以上錄用率
3. **基礎扎實**：為 Phase 6 硬件驗證打好基礎
4. **跨領域**：涉及 OS、系統、應用三個層面，簡歷價值高
5. **時間高效**：6 個月內可見成果，不走冤枉路

### 一句話行動項目

📌 **明天開始**：
```
1. 閱讀本備忘錄 & RESEARCH_DIRECTION_ANALYSIS.md（1 小時）
2. Audit RDSM 代碼中的 thread 創建邏輯（1 小時）
3. 設計 CPU affinity 實現方案的 draft（2 小時）
4. 與 advisor 確認方向（30 分鐘 meeting）
5. Week 1 開始 coding（affinity module）
```

**成功的關鍵**：Action over perfection。開始做，邊做邊調整。

---

**準備好開始了嗎？** 🚀

# RDSM 研究方向規劃：文檔導覽

**生成日期**：2026-06-01  
**對象**：RDSM 團隊、研究人員、顧問

---

## 📍 核心文檔位置

### 1. 🎯 **決策層面**（START HERE）
   - **文件**：[`docs/RESEARCH_MEMO.md`](RESEARCH_MEMO.md)
   - **用途**：1-page executive summary + 行動計畫
   - **讀者**：Advisor、項目負責人、快速決策者
   - **時間**：15 分鐘快速閱讀

### 2. 📊 **戰略層面**（深度規劃）
   - **文件**：[`docs/RESEARCH_DIRECTION_ANALYSIS.md`](RESEARCH_DIRECTION_ANALYSIS.md)
   - **用途**：5 個可行研究方向的詳細分析
   - **內容**：
     - 現狀診斷（優勢/劣勢）
     - 5 個研究方向的獨立評價
     - 優先排序與時程
     - 檢查點與風險評估
   - **讀者**：研究人員、專案規劃者
   - **時間**：30-45 分鐘精讀

### 3. 🔧 **執行層面**（操作手冊）
   - **文件**：[`docs/IMPLEMENTATION_GUIDE.md`](IMPLEMENTATION_GUIDE.md)
   - **用途**：優先 2 個方向的逐步實施指南
   - **內容**：
     - 方向 1A：CPU Affinity Optimization（3 頁）
     - 方向 3：TPC-C 工作負載適配（3 頁）
     - 每個方向包含：背景、具體步驟、代碼框架、實驗設計、檢查點
   - **讀者**：開發人員、實驗執行者
   - **時間**：邊做邊參考

---

## 🎓 讀取順序建議

### 情景 A：決策層（管理者/Advisor）
```
1. 讀 RESEARCH_MEMO.md（核心建議 + KPI）           [15 min]
2. 瀏覽 RESEARCH_DIRECTION_ANALYSIS.md 第一部分    [10 min]
3. 與團隊討論 → 確認方向                            [30 min]
```
**總耗時**：~1 小時 ✓

### 情景 B：研究規劃（項目負責人）
```
1. 精讀 RESEARCH_DIRECTION_ANALYSIS.md              [45 min]
2. 精讀 RESEARCH_MEMO.md                             [15 min]
3. 參考 IMPLEMENTATION_GUIDE.md 快速瞭解技術細節    [15 min]
4. 製定項目時程表                                    [30 min]
```
**總耗時**：~2 小時 ✓

### 情景 C：開發執行（工程師/研究者）
```
1. 粗讀 RESEARCH_MEMO.md 理解方向                   [10 min]
2. 精讀 IMPLEMENTATION_GUIDE.md 對應章節            [30-45 min]
3. 按步驟實施（開發 + 實驗）                         [4-6 週]
4. 參考 RESEARCH_DIRECTION_ANALYSIS.md 深化理解     [按需]
```
**進行式**：持續 ✓

---

## 📋 文檔內容速查表

| 主題 | 位置 | 關鍵內容 |
|-----|-----|--------|
| **研究概況** | MEMO 第一部分 | 現狀、核心建議、KPI |
| **優先度排序** | MEMO 第二部分 | 優先級 1/2/3、為什麼 |
| **具體方向** | ANALYSIS 第二部分 | 5 個方向的詳細描述 |
| **時間規劃** | ANALYSIS 第三部分 | Phase A/B/C 時程表 |
| **發表策略** | ANALYSIS 第四部分 | 每個方向的投稿目標 |
| **Affinity 實施** | IMPLEMENTATION 第一部分 | 4 個步驟 + 代碼框架 |
| **TPC-C 實施** | IMPLEMENTATION 第二部分 | 3 個步驟 + 工作負載生成器 |
| **時程表** | IMPLEMENTATION 最後部分 | 4 週執行計劃 |

---

## 🚀 快速開始（3 步驟）

### Step 1：確認方向（Day 1）
```
[ ] 閱讀 RESEARCH_MEMO.md（15 min）
[ ] 與 advisor 討論確認（20 min）
→ 決策：執行優先級 1 (Affinity)?
```

### Step 2：設計實施方案（Day 2-3）
```
[ ] 讀 IMPLEMENTATION_GUIDE.md 第一部分（20 min）
[ ] Audit RDSM thread pool 代碼（30 min）
[ ] 設計 CPU affinity module 架構（1 hour）
[ ] 與 advisor 評審方案（30 min）
→ Go ahead or pivot?
```

### Step 3：開始開發（Week 1）
```
[ ] 實現 affinity.h + 集成（3 days）
[ ] 基線測試（1 day）
[ ] 並發執行 TPC-C 開發（2 days）
→ 完成 Week 1 milestone
```

---

## 📊 預期成果總結

### 6 個月內預期交付

```
✓ 代碼改進
  - CPU affinity module（300-400 LOC）
  - TPC-C workload generator（500-700 LOC）
  - 性能 5-10% 改善

✓ 學術成果
  - 論文 1：CPU Affinity（EuroSys workshop）
  - 論文 2：TPC-C Workload（VLDB/SIGMOD 或 EuroSys）
  - 可選論文 3：Performance Modeling（workshop）

✓ 社群貢獻
  - 改進的 RDSM 分支（upstream 可合併）
  - TPC-C DSM benchmark（供其他研究者使用）
  - 完整的文檔與可重現性支持
```

---

## 📚 相關資源

### 內部參考
- `README.md` - RDSM 項目總覽
- `HANDOFF.md` - 當前工作狀態
- `PROJECT_PLAN_STATUS.md` - 項目進度表
- `paper.md` - 最終研究報告

### 後續行動
- [ ] 確認研究方向（與 advisor）
- [ ] 分配人力與時間
- [ ] 建立 weekly checkpoint 機制
- [ ] 準備投稿時程表

---

## ❓ 常見問題

### Q1：為什麼不繼續做新演算法？
**A**：新協議（e.g., vector clock DSM）風險高，已有 adaptive routing 失敗案例。轉向 OS 層優化和實際應用驗證，風險更低、成功率更高。

### Q2：6 個月真的能做完？
**A**：
- Affinity（3-4 週）+ TPC-C（4-5 週）= 7-9 週 ✓
- 撰寫投稿（3-4 週）✓
- buffer 和 revision（4-5 週）✓
- 總計 24-26 週在 6 個月內 ✓

### Q3：投稿成功率是多少？
**A**：
- Affinity（workshop）：60-70% ✓ 穩健
- TPC-C（一級會議）：20-30% ⚠️ 需投多個會議
- 至少 1 篇錄用率：80%+ ✓ 有把握

### Q4：與硬件驗證怎麼銜接？
**A**：
- 現在建立的性能模型 → 用於外推到硬件 RDMA
- 改進的代碼架構 → 為 Phase 6 奠定基礎
- 論文發表 → 建立學術信譽，便於申請 RNIC 資源

### Q5：可以並行幾個方向？
**A**：
- 1-2 人團隊：Affinity（全職） + TPC-C（並行 Week 2 開始）✓
- 3+ 人團隊：可加入性能建模，但不推薦 4+ 方向並行

---

## 🎯 主要決策點

| 決策點 | 時間 | 行動 |
|-------|-----|-----|
| **方向確認** | Day 1 | Advisor review + Go/No-go |
| **Affinity 可行性** | Week 1 end | 檢查 performance improvement ≥ 3% |
| **TPC-C 完成度** | Week 4 end | 檢查 workload 準確性 |
| **投稿準備** | Week 6 | 論文初稿完成，評估是否投稿 ready |
| **第三方向決定** | Month 2 end | 若有餘力，決定是否執行性能建模 |

---

## 最後提醒

✅ **立即行動**
```
1. 今天讀 RESEARCH_MEMO.md
2. 明天與 advisor 討論確認
3. 後天開始 Week 1 開發
```

✅ **保持迭代**
```
- 每週檢查點會議（30 min）
- 遇到障礙立即 pivot（按 RESEARCH_DIRECTION_ANALYSIS.md 備選方向）
- 月度完整審視（與 advisor）
```

✅ **質量優於速度**
```
- 寧可 1 篇高質論文，不要 3 篇平庸論文
- 每個實驗保證 correctness-clean（學 RDSM 風格）
- 論文寫作投入 25-30% 時間
```

---

**祝研究順利！🎓**

有問題？參考完整文檔或與 advisor 討論。


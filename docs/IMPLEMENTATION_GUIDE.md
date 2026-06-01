# RDSM 研究方向實施指南：具體操作步驟

**作者**：AI Research Advisor  
**更新**：2026-06-01  
**對標**：方向 1A（CPU Affinity）+ 方向 3（TPC-C 工作負載）

---

## 優先方向 1A：CPU Affinity 與隊列親和性

### 背景與研究問題

**觀察**：RDSM 當前未控制線程調度，threads 可能頻繁在 cores 間遷移。

**假說**：將 transaction threads 與 arbitration queue threads pin 到特定 cores + NUMA 節點 
→ 減少 IPC overhead + 提升 L3 cache locality 
→ 期望 5-15% 性能改善

### 實施方案

#### 步驟 1：測量基線（1 天）

**新增文件**：`experiments/affinity_baseline.cpp`

```cpp
// 采集當前線程 placement 信息
#include <sched.h>
#include <numa.h>

void print_thread_affinity() {
    cpu_set_t cpuset;
    sched_getaffinity(0, sizeof(cpu_set_t), &cpuset);
    
    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    for (int i = 0; i < cpu_count; i++) {
        if (CPU_ISSET(i, &cpuset)) {
            printf("Thread %d runs on CPU %d (NUMA %d)\n", 
                   gettid(), i, numa_node_of_cpu(i));
        }
    }
}
```

**測試方法**：
```bash
# 修改 phase2_dsm_benchmark.cpp 啟動時打印 affinity 信息
./build/phase2_dsm_benchmark \
  --application-case flash_sale \
  --workload-name mixed_hot4_write50 \
  --algorithm hybrid_static_arbitration_occ_per_shard_8 \
  --threads 4 \
  --duration-sec 1 \
  2>&1 | grep "Thread.*CPU"
```

**預期輸出**：看到 threads 在多個 cores 上跳動（未 pin）

---

#### 步驟 2：實現 CPU Affinity 支持（2 天）

**修改點**：`include/thread_pool.h` 或新建 `include/affinity.h`

```cpp
#pragma once
#include <sched.h>
#include <thread>
#include <vector>
#include <numa.h>

class CpuAffinityManager {
public:
    enum AffinityMode {
        NONE,           // 不設置 affinity
        PIN_TO_CORE,    // 每個 thread 固定到一個 core
        PIN_PER_SHARD   // 同一 shard 的 threads 綁定到同一 NUMA 節點
    };

    static void set_thread_affinity(int thread_id, AffinityMode mode, int total_threads, int num_shards) {
        if (mode == NONE) return;
        
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        
        if (mode == PIN_TO_CORE) {
            // Simple round-robin: thread 0 → core 0, thread 1 → core 1, etc.
            int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
            int assigned_core = thread_id % num_cpus;
            CPU_SET(assigned_core, &cpuset);
            
        } else if (mode == PIN_PER_SHARD) {
            // Assign threads of same shard to same NUMA node
            int shard_id = thread_id / (total_threads / num_shards);
            int num_nodes = numa_num_configured_nodes();
            int node_id = shard_id % num_nodes;
            
            struct bitmask *mask = numa_allocate_cpumask();
            numa_node_to_cpus(node_id, mask);
            
            for (int i = 0; i < CPU_SETSIZE; i++) {
                if (numa_bitmask_isbitset(mask, i)) {
                    CPU_SET(i, &cpuset);
                }
            }
            numa_free_cpumask(mask);
        }
        
        sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
        
        char cpu_str[256];
        cpu_mask_to_string(&cpuset, cpu_str);
        fprintf(stderr, "Thread %d pinned to: %s\n", thread_id, cpu_str);
    }
    
private:
    static void cpu_mask_to_string(cpu_set_t *set, char *buf) {
        sprintf(buf, "");
        for (int i = 0; i < CPU_SETSIZE; i++) {
            if (CPU_ISSET(i, set)) {
                strcat(buf, "%d,");
                sprintf(buf + strlen(buf) - 2, "%d,", i);
            }
        }
        // trim last comma
        if (strlen(buf) > 0) buf[strlen(buf)-1] = '\0';
    }
};
```

**集成到 RDSM**：修改 `experiments/phase2_dsm_benchmark.cpp`

```cpp
// 在 main() 中添加新的 CLI 選項
add_argument(parser, "affinity-mode", "none|pin-core|pin-shard", "none");

// 在線程啟動時調用
std::vector<std::thread> threads;
for (int i = 0; i < thread_count; i++) {
    threads.emplace_back([i, affinity_mode, num_shards]() {
        CpuAffinityManager::set_thread_affinity(i, affinity_mode, thread_count, num_shards);
        // ... 原有的 transaction 運行邏輯
    });
}
```

---

#### 步驟 3：實驗設計與數據采集（3 天）

**測試矩陣**：

```bash
# Baseline (no affinity)
for config in low_uniform_read95 mixed_hot4_write50 high_hot1_write100 zipf99_write100; do
  for threads in 2 4; do
    for rep in 1 2 3; do
      ./build/phase2_dsm_benchmark \
        --workload-name $config \
        --algorithm hybrid_static_arbitration_occ_per_shard_8 \
        --threads $threads \
        --affinity-mode none \
        --duration-sec 10 \
        > results/affinity_baseline_${config}_t${threads}_rep${rep}.txt
    done
  done
done

# Treatment 1: pin to core
for config in low_uniform_read95 mixed_hot4_write50 high_hot1_write100 zipf99_write100; do
  for threads in 2 4; do
    for rep in 1 2 3; do
      ./build/phase2_dsm_benchmark \
        --workload-name $config \
        --algorithm hybrid_static_arbitration_occ_per_shard_8 \
        --threads $threads \
        --affinity-mode pin-core \
        --duration-sec 10 \
        > results/affinity_pincore_${config}_t${threads}_rep${rep}.txt
    done
  done
done

# Treatment 2: pin per shard
# (same, but --affinity-mode pin-shard)
```

**性能指標提取**：

修改 `scripts/parse_affinity_results.py`：

```python
import json
import re
import sys
from pathlib import Path

def parse_result_file(filepath):
    """Extract throughput, p99 latency, and per-file metrics"""
    with open(filepath) as f:
        content = f.read()
    
    result = {}
    
    # 提取 throughput (transactions/sec)
    m = re.search(r'Committed txns:\s+(\d+)', content)
    if m:
        result['committed_txns'] = int(m.group(1))
    
    m = re.search(r'Total time:\s+([\d.]+)\s*sec', content)
    if m:
        duration = float(m.group(1))
        result['throughput'] = result['committed_txns'] / duration
    
    # 提取 latency percentiles
    for p in ['p50', 'p95', 'p99']:
        pattern = f'{p}_latency:\\s*([\\d.]+)\\s*us'
        m = re.search(pattern, content)
        if m:
            result[f'latency_{p}'] = float(m.group(1))
    
    return result

def main():
    # 比較 baseline vs affinity treatments
    baseline_results = {}
    pincore_results = {}
    pinshard_results = {}
    
    for config in ['low_uniform_read95', 'mixed_hot4_write50', 'high_hot1_write100']:
        baseline_results[config] = {2: [], 4: []}
        pincore_results[config] = {2: [], 4: []}
        pinshard_results[config] = {2: [], 4: []}
        
        for threads in [2, 4]:
            for rep in [1, 2, 3]:
                # Parse baseline
                path = f'results/affinity_baseline_{config}_t{threads}_rep{rep}.txt'
                if Path(path).exists():
                    baseline_results[config][threads].append(parse_result_file(path))
                
                # Parse pin-core
                path = f'results/affinity_pincore_{config}_t{threads}_rep{rep}.txt'
                if Path(path).exists():
                    pincore_results[config][threads].append(parse_result_file(path))
                
                # Parse pin-shard
                path = f'results/affinity_pinshard_{config}_t{threads}_rep{rep}.txt'
                if Path(path).exists():
                    pinshard_results[config][threads].append(parse_result_file(path))
    
    # 計算均值與改進
    print("| Config | Threads | Baseline Thr | Pin-Core Thr | Improvement | P99 Latency Chg |")
    print("|--------|---------|--------------|--------------|-------------|-----------------|")
    
    for config in ['low_uniform_read95', 'mixed_hot4_write50', 'high_hot1_write100']:
        for threads in [2, 4]:
            if baseline_results[config][threads]:
                baseline_thr = sum(r['throughput'] for r in baseline_results[config][threads]) / len(baseline_results[config][threads])
                pincore_thr = sum(r['throughput'] for r in pincore_results[config][threads]) / len(pincore_results[config][threads]) if pincore_results[config][threads] else 0
                
                improvement = ((pincore_thr - baseline_thr) / baseline_thr * 100) if baseline_thr > 0 else 0
                
                baseline_p99 = sum(r.get('latency_p99', 0) for r in baseline_results[config][threads]) / len(baseline_results[config][threads])
                pincore_p99 = sum(r.get('latency_p99', 0) for r in pincore_results[config][threads]) / len(pincore_results[config][threads]) if pincore_results[config][threads] else 0
                
                p99_change = ((pincore_p99 - baseline_p99) / baseline_p99 * 100) if baseline_p99 > 0 else 0
                
                print(f"| {config} | {threads} | {baseline_thr:.1f} | {pincore_thr:.1f} | {improvement:+.1f}% | {p99_change:+.1f}% |")

if __name__ == '__main__':
    main()
```

---

#### 步驟 4：結果分析與論文撰寫（1 天）

**預期發現**：

```
假設 pin-core 改善：
  - low_uniform_read95: +7-10%  (cache locality 效果明顯)
  - mixed_hot4_write50: +5-8%   (moderate improvement)
  - high_hot1_write100: +2-5%   (contention-limited, affinity 幫助小)
  - zipf99_write100: +4-7%      (skew pattern benefits from affinity)
```

**論文大綱**（3-4 頁短論文）：

```markdown
# CPU Affinity Optimization for RDMA-style DSM under Virtualization

## Abstract
- 問題：虛擬化環境中線程調度開銷隱藏
- 方法：CPU affinity + NUMA-aware shard placement
- 結果：平均 6% 吞吐改善，p99 latency 降低 4-8%

## 1. Introduction
- 虛擬機內 context switch 成本（80-200 ns）
- DSM transaction 對調度敏感

## 2. Design
- CpuAffinityManager API
- 三種 affinity 策略（none, pin-core, pin-per-shard）
- 集成到 RDSM 的方式

## 3. Evaluation
- 測試矩陣（4 workloads × 2 threads × 3 reps）
- 吞吐與 latency 結果
- Breakdown：cache miss rate, context switches

## 4. Conclusion & Future
- Affinity 在低競爭場景效果好
- 對比 hardware RDMA 的啟示

## References (10-15)
- VM 調度論文
- NUMA 優化論文
- 相關 DSM 系統
```

---

### 交付物清單

```
✓ Code:
  - include/affinity.h（新）
  - experiments/affinity_baseline.cpp（參考）
  - 修改 phase2_dsm_benchmark.cpp（CLI 選項 + thread affinity）
  - scripts/parse_affinity_results.py（新）

✓ Data:
  - results/affinity_baseline_*.txt （48 files）
  - results/affinity_pincore_*.txt （48 files）
  - results/affinity_pinshard_*.txt （48 files）
  - 匯總 CSV

✓ Paper:
  - 3-4 page short paper draft
  - figures (1-2 throughput comparison, 1 latency CDF)
  - 投稿目標：EuroSys workshop 或 ASPLOS poster
```

---

## 優先方向 3：TPC-C 工作負載適配

### 背景與研究問題

**觀察**：RDSM 現有工作負載為合成或輕度應用象徵。

**假說**：TPC-C（真實 OLTP 基準）有特定的訪問模式（聚集、時序相關性） 
→ 不同於平面 Zipf 分佈 
→ 期望發現 adaptive routing 在聚集模式下更有效

### 實施方案

#### 步驟 1：TPC-C 工作負載生成器（2 天）

**新文件**：`experiments/workload_generator_tpcc.h`

```cpp
#pragma once
#include <vector>
#include <random>
#include <unordered_map>

// TPC-C 配置
struct TpcCConfig {
    int num_warehouses = 10;      // 通常 1-100
    int num_districts_per_warehouse = 10;
    int num_customers_per_district = 3000;
    int num_items = 100000;
    
    // 交易混合 (NewOrder:Payment:Delivery = 45:43:4 default)
    double neworder_ratio = 0.45;
    double payment_ratio = 0.43;
    double delivery_ratio = 0.04;
    double orderstatus_ratio = 0.04;
    double stocklevel_ratio = 0.04;
};

class TpcCWorkloadGenerator {
private:
    TpcCConfig config;
    std::mt19937 rng;
    
    // TPC-C 使用 non-uniform random distribution (NUR) for customer selection
    // 75% 的客戶訪問來自 home district，25% 來自 remote
    int generate_warehouse_id() {
        return rng() % config.num_warehouses;
    }
    
    int generate_district_id(int w_id) {
        return rng() % config.num_districts_per_warehouse;
    }
    
    int generate_customer_id_local(int w_id, int d_id) {
        // 75% local, 25% remote within warehouse
        if ((rng() % 100) < 75) {
            return (rng() % config.num_customers_per_district) + 
                   d_id * config.num_customers_per_district;
        } else {
            int remote_d = (rng() % (config.num_districts_per_warehouse - 1));
            if (remote_d >= d_id) remote_d++;
            return (rng() % config.num_customers_per_district) +
                   remote_d * config.num_customers_per_district;
        }
    }
    
public:
    TpcCWorkloadGenerator(const TpcCConfig& cfg) : config(cfg), rng(std::random_device{}()) {}
    
    // Transaction type: 0=NewOrder, 1=Payment, 2=Delivery, 3=OrderStatus, 4=StockLevel
    struct TpcCTransaction {
        int type;  // transaction type
        std::vector<int> read_keys;    // 讀取的 object keys
        std::vector<int> write_keys;   // 寫入的 object keys
        int hot_object = -1;           // -1 if none; else shared hot object (sold counter, etc.)
    };
    
    TpcCTransaction generate_transaction() {
        double r = (rng() % 100) / 100.0;
        
        TpcCTransaction tx;
        
        if (r < config.neworder_ratio) {
            // New Order Transaction
            // Access: warehouse, district, customer, 5-15 order lines, stock
            tx.type = 0;
            
            int w_id = generate_warehouse_id();
            int d_id = generate_district_id(w_id);
            int c_id = generate_customer_id_local(w_id, d_id);
            
            // 讀：warehouse, district, customer
            tx.read_keys.push_back(w_id);  // warehouse inventory
            tx.read_keys.push_back(d_id);  // district next order
            tx.read_keys.push_back(c_id);  // customer
            
            // 寫：order, order_line, stock (6-15 items)
            int num_items = 5 + (rng() % 10);
            for (int i = 0; i < num_items; i++) {
                int item_id = rng() % config.num_items;
                tx.write_keys.push_back(item_id);  // stock[item_id]
            }
            
            // Hot object: shared sold counter (10% probability)
            if ((rng() % 100) < 10) {
                tx.hot_object = 0;  // Global or per-warehouse sold counter
            }
            
        } else if (r < config.neworder_ratio + config.payment_ratio) {
            // Payment Transaction
            // Access: warehouse, district, customer, balance update
            tx.type = 1;
            
            int w_id = generate_warehouse_id();
            int d_id = generate_district_id(w_id);
            int c_id = generate_customer_id_local(w_id, d_id);
            
            tx.read_keys.push_back(w_id);
            tx.read_keys.push_back(d_id);
            tx.read_keys.push_back(c_id);
            
            tx.write_keys.push_back(c_id);  // update balance
            
        } else if (r < config.neworder_ratio + config.payment_ratio + config.delivery_ratio) {
            // Delivery Transaction (較少競爭)
            tx.type = 2;
            
            int w_id = generate_warehouse_id();
            int d_id = generate_district_id(w_id);
            
            // 順序掃描（低競爭）
            tx.read_keys.push_back(w_id);
            tx.read_keys.push_back(d_id);
            
        } else {
            // Order Status, Stock Level (read-only)
            tx.type = rng() % 2 == 0 ? 3 : 4;
            
            int w_id = generate_warehouse_id();
            int d_id = generate_district_id(w_id);
            int c_id = generate_customer_id_local(w_id, d_id);
            
            tx.read_keys.push_back(c_id);
            if (tx.type == 4) {
                tx.read_keys.push_back(w_id);  // Stock level scan
            }
        }
        
        return tx;
    }
};
```

**集成方式**：修改 `experiments/phase2_dsm_benchmark.cpp`

```cpp
// 在 workload selection 邏輯中添加
if (workload_name == "tpcc_mixed") {
    TpcCConfig tpcc_cfg;
    tpcc_cfg.num_warehouses = 10;
    tpcc_cfg.num_districts_per_warehouse = 10;
    TpcCWorkloadGenerator tpcc_gen(tpcc_cfg);
    
    for (auto& tx : transaction_batch) {
        auto tpcc_tx = tpcc_gen.generate_transaction();
        // 映射到 RDSM read/write keys
        tx.read_set = tpcc_tx.read_keys;
        tx.write_set = tpcc_tx.write_keys;
        if (tpcc_tx.hot_object >= 0) {
            tx.write_set.push_back(tpcc_tx.hot_object);
        }
    }
}
```

---

#### 步驟 2：TPC-C vs Synthetic 工作負載對比實驗（1 天）

**實驗設計**：

```bash
# 收集 TPC-C 工作負載
for algo in baseline_occ backoff_occ hybrid_static_arbitration_occ_per_shard_8 hybrid_adaptive_arbitration_occ_per_shard_8; do
  for threads in 1 2 4; do
    for rep in 1 2 3; do
      ./build/phase2_dsm_benchmark \
        --workload-name tpcc_mixed \
        --algorithm $algo \
        --threads $threads \
        --duration-sec 10 \
        --latency-sampling bounded_rotation \
        --latency-sample-size 10000 \
        > results/tpcc_${algo}_t${threads}_rep${rep}.json
    done
  done
done

# 對比現有 synthetic workload (mixed_hot4_write50 類似)
# ...（已在現有矩陣中）
```

**數據分析**：`scripts/analyze_tpcc_characteristics.py`

```python
import json
import re
from pathlib import Path
import numpy as np

def analyze_tpcc_vs_synthetic():
    """比較 TPC-C 與 mixed_hot4_write50 的特性"""
    
    print("=" * 80)
    print("TPC-C vs Synthetic Workload Characteristics")
    print("=" * 80)
    
    # 收集 TPC-C 結果
    tpcc_results = {}
    for algo in ['baseline_occ', 'hybrid_adaptive_arbitration_occ_per_shard_8']:
        tpcc_results[algo] = []
        for rep in range(1, 4):
            path = f'results/tpcc_{algo}_t2_rep{rep}.json'
            if Path(path).exists():
                with open(path) as f:
                    tpcc_results[algo].append(json.load(f))
    
    # 收集 synthetic 結果（從既有最終矩陣）
    synthetic_results = {}
    for algo in ['baseline_occ', 'hybrid_adaptive_arbitration_occ_per_shard_8']:
        synthetic_results[algo] = []
        for rep in range(1, 4):
            path = f'results/final_focused_matrix/mixed_hot4_write50_{algo}_t2_rep{rep}.json'
            if Path(path).exists():
                with open(path) as f:
                    synthetic_results[algo].append(json.load(f))
    
    # 比較指標
    for algo in ['baseline_occ', 'hybrid_adaptive_arbitration_occ_per_shard_8']:
        print(f"\nAlgorithm: {algo}")
        print("-" * 80)
        
        if tpcc_results[algo] and synthetic_results[algo]:
            tpcc_throughput = np.mean([r['throughput'] for r in tpcc_results[algo]])
            tpcc_p99 = np.mean([r['latency_p99'] for r in tpcc_results[algo]])
            tpcc_abort_ratio = np.mean([r['abort_ratio'] for r in tpcc_results[algo]])
            
            synthetic_throughput = np.mean([r['throughput'] for r in synthetic_results[algo]])
            synthetic_p99 = np.mean([r['latency_p99'] for r in synthetic_results[algo]])
            synthetic_abort_ratio = np.mean([r['abort_ratio'] for r in synthetic_results[algo]])
            
            print(f"Throughput (txns/sec):")
            print(f"  TPC-C:      {tpcc_throughput:>8.1f}")
            print(f"  Synthetic:  {synthetic_throughput:>8.1f}")
            print(f"  Ratio:      {tpcc_throughput/synthetic_throughput:>8.2f}x")
            print()
            print(f"P99 Latency (us):")
            print(f"  TPC-C:      {tpcc_p99:>8.1f}")
            print(f"  Synthetic:  {synthetic_p99:>8.1f}")
            print(f"  Ratio:      {tpcc_p99/synthetic_p99:>8.2f}x")
            print()
            print(f"Abort Ratio:")
            print(f"  TPC-C:      {tpcc_abort_ratio:>8.1%}")
            print(f"  Synthetic:  {synthetic_abort_ratio:>8.1%}")

if __name__ == '__main__':
    analyze_tpcc_vs_synthetic()
```

---

#### 步驟 3：論文撰寫（1 天）

**論文大綱**（5-6 頁）：

```markdown
# Understanding DSM Workload Characteristics: A TPC-C Case Study

## Abstract
- 問題：DSM 工作負載特性如何影響算法選擇？
- 方法：在 RDSM 上實現 TPC-C，與 synthetic workloads 對比
- 發現：TPC-C 訪問聚集性與時序相關性導致不同的競爭模式

## 1. Introduction
- DSM 工作負載在學術中常為合成的
- TPC-C 代表真實 OLTP 特性
- 問題：如何在 TPC-C 上運行 DSM？

## 2. TPC-C 特性分析
- 基本配置 (warehouses, districts, customers)
- 五種交易類型與訪問模式
  * NewOrder: write-heavy, clustered access
  * Payment: balanced, hot customer updates
  * Delivery: sequential scan (low contention)
  * OrderStatus, StockLevel: read-only
- 非均勻分佈 (75% local district, 25% remote)

## 3. RDSM TPC-C 實現
- 工作負載生成器設計
- 對象映射策略
- 集成方式

## 4. 實驗結果
### 4.1 Throughput & Latency
- TPC-C vs mixed_hot4_write50 對比
- 算法適應性分析（哪些算法適合 TPC-C）

### 4.2 Transaction Type Breakdown
- 按交易類型分析性能差異
- NewOrder (write-heavy) vs Delivery (scan)

### 4.3 Adaptive Routing 有效性
- TPC-C 的聚集特性是否幫助 adaptive routing 決策？

## 5. 相關工作
- TPC-C 在其他 DSM/DB 系統中的應用

## 6. 結論 & 未來工作
- TPC-C 工作負載的價值
- 未來：其他應用（零售、物流）的 DSM 適配

## References (15-20)
- TPC-C 規範
- 相關 DSM 評估論文
- OLTP 性能論文
```

---

### 交付物清單

```
✓ Code:
  - experiments/workload_generator_tpcc.h（新）
  - 修改 phase2_dsm_benchmark.cpp（新 workload type）
  - scripts/analyze_tpcc_characteristics.py（新）

✓ Data:
  - results/tpcc_*.json （48 files）
  - 匯總表格與對比分析

✓ Paper:
  - 5-6 page paper draft
  - figures:
    * TPC-C 交易類型分佈圖
    * Throughput 對比（算法 × workload）
    * Latency CDF（TPC-C vs synthetic）
    * Per-transaction-type breakdown
  - 投稿目標：VLDB, SIGMOD, 或 EuroSys
```

---

## 執行時間表

| 週 | 任務 | 工作量 | 輸出 |
|----|------|--------|------|
| **Week 1** | **方向 1A 啟動** | | |
| Mon-Tue | 基線測量 + 設計 affinity | 1 day | Affinity design doc |
| Wed-Thu | 實現 + 集成 | 1.5 days | Code branch |
| Fri-Sat | 實驗運行 | 1.5 days | 48 baseline runs |
| **Week 2** | **方向 1A 完成 + 方向 3 啟動** | | |
| Mon-Tue | Affinity 實驗 + 結果分析 | 1.5 days | Comparison table |
| Wed-Thu | TPC-C 工作負載開發 | 1.5 days | workload_generator_tpcc.h |
| Fri-Sat | TPC-C 集成 + 初步測試 | 1 day | Code ready for experiments |
| **Week 3** | **方向 3 實驗** | | |
| Mon-Wed | TPC-C 完整實驗 | 2 days | 48 TPC-C runs |
| Thu-Fri | 結果分析 | 1 day | Analysis tables + charts |
| Sat | 論文撰寫 | 1 day | Draft completion |
| **Week 4** | **論文定稿與投稿** | | |
| Mon-Tue | Affinity 論文定稿 | 1 day | Final paper v1 |
| Wed-Thu | TPC-C 論文定稿 | 1.5 days | Final paper v2 |
| Fri | 投稿準備 | 0.5 day | Ready to submit |

---

## 檢查清單

### Phase A（Affinity）完成條件

- [ ] CPU affinity 代碼實現無 crash
- [ ] Correctness 檢查通過（0 invariants violations）
- [ ] 3 重複實驗完成
- [ ] Throughput 改善 ≥ 3%（任何 config）
- [ ] 結果方差 < 15%
- [ ] Affinity 論文初稿完成

### Phase B（TPC-C）完成條件

- [ ] TPC-C workload generator 通過 smoke test
- [ ] 與 synthetic workload 性能可比（誤差 < 20%）
- [ ] 3 重複實驗完成
- [ ] 按交易類型的性能分解清晰
- [ ] TPC-C 論文初稿完成

---

## 預期發表結果

**6 個月內預期產出**：

1. ✓ **CPU Affinity Optimization**
   - 投稿：EuroSys workshop / ASPLOS poster session
   - 預期接受率：60-70%

2. ✓ **TPC-C DSM Workload Analysis**
   - 投稿：VLDB, SIGMOD, 或 EuroSys main
   - 預期接受率：20-40%（取決於 novel 程度）

3. ⚠️ **可選：VM Overhead Modeling**（若時間許可）
   - 投稿：ASPLOS workshop / Performance Modeling Track
   - 預期接受率：70-80%

**總論文數：2-3 篇 ✓**

/**
 * FaRM DSM 基準測試程式
 * 測試 RDMA 操作與 DSM 事務處理
 * 
 */

#include "../include/rdma_wrapper.h"
#include "../include/memory_manager.h"
#include "../include/dsm_transaction.h"
#include "../include/perf_monitor.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cstring>


void print_banner(const std::string &title) {
 std::cout << "\n";
 std::cout << "========================================" << std::endl;
 std::cout << " " << title << std::endl;
 std::cout << "========================================" << std::endl;
}

/**
 * 測試 1：RDMA 裝置初始化
 */
void test_rdma_initialization() {
 print_banner("測試 1：RDMA 裝置初始化");
 
 RDMAWrapper rdma;
 
 if (rdma.init_device() != 0) {
 std::cerr << "警告：未找到可用 RDMA 裝置 (在 WSL2 環境下屬正常現象)" << std::endl;
 std::cerr << "基準測試將使用 Soft-RoCE 或模擬模式運行" << std::endl;
 return;
 }
 
 std::cout << " RDMA 裝置初始化成功" << std::endl;
 
 if (rdma.setup_connection() != 0) {
 std::cerr << " 連線設定失敗" << std::endl;
 return;
 }
 
 std::cout << " 連線設定完成" << std::endl;
}

/**
 * 測試 2：記憶體管理 (Slab 分配器)
 */
void test_memory_management() {
 print_banner("測試 2：記憶體管理 (Slab 分配器)");
 
 RDMAWrapper rdma;
 MemoryManager mem_mgr(&rdma);
 
 // 初始化 10MB 總空間，每個 Slab 大小為 1MB
 if (mem_mgr.initialize(10 * 1024 * 1024, 1024 * 1024) != 0) {
 std::cerr << " 記憶體管理器初始化失敗" << std::endl;
 return;
 }
 
 std::cout << " 記憶體管理器已初始化" << std::endl;
 
 // 執行分配測試
 const int NUM_ALLOCS = 10;
 void *ptrs[NUM_ALLOCS];
 
 for (int i = 0; i < NUM_ALLOCS; i++) {
 size_t alloc_size = (i + 1) * 1024; // 分配 1KB, 2KB, ..., 10KB
 ptrs[i] = mem_mgr.allocate(alloc_size);
 
 if (!ptrs[i]) {
 std::cerr << " 第 " << i << " 次分配失敗" << std::endl;
 return;
 }
 
 std::cout << " 已分配 " << alloc_size << " 位元組" << std::endl;
 }
 
 std::cout << "\n目前記憶體利用率: " << (mem_mgr.memory_utilization() * 100.0) << "%" << std::endl;
 mem_mgr.print_stats();
 
 // 執行釋放測試
 for (int i = 0; i < NUM_ALLOCS; i++) {
 mem_mgr.deallocate(ptrs[i]);
 }
 
 std::cout << " 所有記憶體已釋放" << std::endl;
}

/**
 * 測試 3：RDMA 單邊操作
 */
void test_rdma_operations() {
 print_banner("測試 3：RDMA 單邊操作 (One-sided Operations)");
 
 RDMALatencyBenchmark bench(nullptr);
 
 std::cout << "正在運行微基準測試 (Micro-benchmarks)..." << std::endl;
 
 bench.benchmark_write_latency(64, 4096, 1000);
 std::cout << "\nRDMA WRITE 延遲測試結果：" << std::endl;
 bench.export_results(".");
 
 bench.benchmark_read_latency(64, 4096, 1000);
 std::cout << "\nRDMA READ 延遲測試結果：" << std::endl;
 bench.export_results(".");
}

/**
 * 測試 4：DSM 事務處理 (OCC)
 */
void test_transactions() {
 print_banner("測試 4：DSM 事務處理 (OCC)");
 
 RDMAWrapper rdma;
 MemoryManager mem_mgr(&rdma);
 mem_mgr.initialize(1024 * 1024, 256 * 1024);
 
 DSMNode node(0, &rdma, &mem_mgr);
 node.initialize(1024 * 1024);
 
 TransactionManager txn_mgr(&node);
 
 std::cout << "正在建立測試物件..." << std::endl;
 uint64_t obj1 = node.create_object(nullptr, 256);
 uint64_t obj2 = node.create_object(nullptr, 256);
 
 std::cout << " 物件建立完成：ID " << obj1 << ", ID " << obj2 << std::endl;
 
 // 執行範例事務
 std::cout << "\n正在執行事務示例..." << std::endl;
 auto txn = txn_mgr.begin_transaction();
 
 std::cout << " 事務已啟動 (ID: " << txn->get_id() << ")" << std::endl;
 std::cout << " 當前狀態：EXECUTING (執行中)" << std::endl;
 
 if (txn->validate() == 0) {
 std::cout << " 事務驗證成功" << std::endl;
 if (txn->commit() == 0) {
 std::cout << " 事務提交成功" << std::endl;
 }
 }
 
 node.print_stats();
}

/**
 * 測試 5：DSM 吞吐量測試
 */
void test_throughput() {
 print_banner("測試 5：DSM 吞吐量基準測試");
 
 RDMAWrapper rdma;
 MemoryManager mem_mgr(&rdma);
 DSMNode node(0, &rdma, &mem_mgr);
 TransactionManager txn_mgr(&node);
 
 DSMThroughputBenchmark bench(&txn_mgr);
 
 std::cout << "\n--- 唯讀負載 (Read-only Workload) ---" << std::endl;
 bench.benchmark_read_workload(100, 1000);
 bench.export_results(".");
 
 std::cout << "\n--- 高寫入負載 (Write-heavy Workload) ---" << std::endl;
 bench.benchmark_write_workload(100, 1000);
 bench.export_results(".");
 
 std::cout << "\n--- 混合負載 (Mixed Workload, 50% 寫入) ---" << std::endl;
 bench.benchmark_mixed_workload(100, 1000, 0.5);
 bench.export_results(".");
 
 std::cout << "\n--- 高競爭負載 (High Contention Workload) ---" << std::endl;
 bench.benchmark_contention_workload(10, 100, 4);
 bench.export_results(".");
}

/**
 * 列印系統開發摘要
 */
void print_summary() {
 print_banner("實作摘要 (Summary)");
 std::cout << "\nFaRM DSM 實作進度狀態：" << std::endl;
 std::cout << " 階段 1：RDMA 基礎設施搭建" << std::endl;
 std::cout << " 階段 2：記憶體管理 (Slab 分配器)" << std::endl;
 std::cout << " 階段 3：RDMA 單邊操作實作" << std::endl;
 std::cout << " 階段 4：OCC 事務機制開發" << std::endl;
 std::cout << " 階段 5：效能監控與分析工具" << std::endl;
 
 std::cout << "\n核心特性：" << std::endl;
 std::cout << " - 基於 RDMA Verbs API 的可靠連線 (RC) 封裝" << std::endl;
 std::cout << " - 高效的 Slab 記憶體分配器，優化記憶體註冊開銷" << std::endl;
 std::cout << " - 樂觀併發控制 (OCC) 確保分布式數據一致性" << std::endl;
 std::cout << " - 詳盡的效能指標採集與匯出系統" << std::endl;
}

int main(int argc, char *argv[]) {
 std::cout << "\n";
 std::cout << "╔════════════════════════════════════════╗" << std::endl;
 std::cout << "║ FaRM DSM 分布式共享記憶體系統測試 ║" << std::endl;
 std::cout << "║ 基於快速遠端記憶體 (Fast Remote Memory) ║" << std::endl;
 std::cout << "╚════════════════════════════════════════╝" << std::endl;
 
 try {
 test_rdma_initialization();
 test_memory_management();
 test_rdma_operations();
 test_transactions();
 test_throughput();
 print_summary();
 } catch (const std::exception &e) {
 std::cerr << "\n 發生異常：" << e.what() << std::endl;
 return 1;
 }
 
 std::cout << "\n 所有測試項目已完成" << std::endl;
 return 0;
}

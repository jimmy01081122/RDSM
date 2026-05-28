/**
 * HERD: 高效能、記憶體友好的可靠數據報 (Reliable Datagram) 實作
 * 參考文獻：Using RDMA to Build a High-Performance Key-Value Store (SIGCOMM '14)
 * 
 * 核心思想：混合通信模式
 * - 客戶端使用 UC (Unreliable Connected) Write 發送請求
 * - 服務端使用 UD (Unreliable Datagram) 發送回應
 */

#ifndef HERD_KV_STORE_H
#define HERD_KV_STORE_H

#include "rdma_wrapper.h"
#include <string>
#include <cstring>
#include <cstdint>
#include <map>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>

// 哈希表配置
const int HERD_LOG_SLOTS = 24;  // 2^24 個槽位
const int HERD_NUM_SLOTS = (1 << HERD_LOG_SLOTS);
const int HERD_NUM_BUCKETS = HERD_NUM_SLOTS / 4;  // 每個 Bucket 包含 4 個項目

// 請求/回應格式
struct HERDRequest {
    uint16_t opcode;  // 操作碼：GET=1, PUT=2, DELETE=3
    uint16_t key_len;
    uint32_t value_len;
    char key[64];
    char value[256];
};

struct HERDResponse {
    uint16_t status;    // 狀態：0=成功, 1=找不到, 2=錯誤
    uint16_t value_len;
    char value[256];
};

/**
 * 哈希表分錄 (Entry)
 */
struct HERDEntry {
    uint64_t key_hash;  // 金鑰哈希值
    uint32_t key_len;   // 金鑰長度
    uint32_t value_len; // 數值長度
    uint64_t timestamp; // 時間戳
    char key[64];       // 金鑰數據
    char value[256];    // 數值數據
    uint8_t valid;      // 有效位元 (0=無效, 1=有效)
};

/**
 * 哈希桶 (Bucket) - 包含 4 個分錄
 */
struct HERDBucket {
    HERDEntry entries[4];
    uint32_t lock;  // 簡單自旋鎖 (Spinlock)
};

/**
 * HERD Key-Value Store 服務端
 */
class HERDServer {
public:
    HERDServer(uint32_t server_id, RDMAWrapper *rdma_wrapper);
    ~HERDServer();
    
    // 服務器生命週期
    int initialize(uint32_t num_threads = 1); // 初始化服務器
    int start_server();                      // 啟動服務器
    void stop_server();                       // 停止服務器
    
    // 數據操作 (由請求處理程序調用)
    int handle_get(const std::string &key, std::string &value);     // 處理 GET 請求
    int handle_put(const std::string &key, const std::string &value); // 處理 PUT 請求
    int handle_delete(const std::string &key);                       // 處理 DELETE 請求
    
    // 統計資訊
    void print_stats();
    uint64_t get_total_requests() const { return total_requests_; }
    uint64_t get_total_gets() const { return total_gets_; }
    uint64_t get_total_puts() const { return total_puts_; }
    
private:
    uint32_t server_id_;
    RDMAWrapper *rdma_wrapper_;
    
    // 哈希表
    std::vector<HERDBucket> hash_table_;
    uint32_t num_buckets_;
    std::mutex ht_mutex_;
    
    // 統計數據
    uint64_t total_requests_ = 0;
    uint64_t total_gets_ = 0;
    uint64_t total_puts_ = 0;
    
    // 請求處理線程
    std::vector<std::thread> worker_threads_;
    volatile bool running_ = false;
    
    // 哈希函數
    uint64_t hash_key(const std::string &key) const;
    uint32_t get_bucket_index(uint64_t key_hash) const;
    
    // 內部哈希表操作
    int hash_get(const std::string &key, std::string &value);
    int hash_put(const std::string &key, const std::string &value);
    int hash_delete(const std::string &key);
    
    void worker_thread_fn(); // 工作線程主函數
};

/**
 * HERD Key-Value Store 客戶端
 */
class HERDClient {
public:
    HERDClient(uint32_t client_id, RDMAWrapper *rdma_wrapper);
    ~HERDClient();
    
    // 連線管理
    int connect(const char *server_addr, uint16_t port); // 連接至服務端
    void disconnect();                                   // 斷開連線
    
    // 客戶端操作 API
    int get(const std::string &key, std::string &value);       // 獲取數據
    int put(const std::string &key, const std::string &value); // 存入數據
    int delete_key(const std::string &key);                   // 刪除數據
    
    // 批次操作
    int batch_get(const std::vector<std::string> &keys,
                  std::vector<std::string> &values);
    int batch_put(const std::map<std::string, std::string> &kvs);
    
    // 統計資訊
    void print_stats();
    uint64_t get_total_requests() const { return total_requests_; }
    double get_avg_latency() const;
    
private:
    uint32_t client_id_;
    RDMAWrapper *rdma_wrapper_;
    
    // 服務端資訊
    uint32_t server_rkey_;
    uint64_t server_addr_;
    
    // 本地請求/回應緩衝區
    HERDRequest req_buf_;
    HERDResponse resp_buf_;
    
    // 統計數據
    uint64_t total_requests_ = 0;
    std::vector<uint64_t> request_latencies_;
    mutable std::mutex stats_mutex_;
    
    // 輔助方法
    int send_request(const HERDRequest &req);
    int recv_response(HERDResponse &resp);
};

/**
 * HERD 基準測試工具
 */
class HERDBenchmark {
public:
    HERDBenchmark();
    ~HERDBenchmark();
    
    // 基準測試場景
    int benchmark_get_latency(HERDClient *client, int num_requests = 10000);   // GET 延遲測試
    int benchmark_put_latency(HERDClient *client, int num_requests = 10000);   // PUT 延遲測試
    int benchmark_mixed_workload(HERDClient *client, int num_requests = 10000, 
                                 double write_ratio = 0.5);                // 混合負載測試
    int benchmark_throughput(HERDClient *client, int num_clients = 1,
                            int requests_per_client = 10000);              // 吞吐量測試
    
    // 匯出結果
    int export_results(const std::string &filename);
};

#endif // HERD_KV_STORE_H

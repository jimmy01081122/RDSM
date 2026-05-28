
#include "../include/herd_kv_store.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <chrono>

// ============ HERD 哈希函數 ============

/**
 * FNV-1a 哈希演算法實作
 */
static inline uint64_t hash_fnv1a(const void *key, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= ((uint8_t*)key)[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

// ============ HERDServer 實作 ============

HERDServer::HERDServer(uint32_t server_id, RDMAWrapper *rdma_wrapper)
    : server_id_(server_id), rdma_wrapper_(rdma_wrapper),
      num_buckets_(HERD_NUM_BUCKETS) {
}

HERDServer::~HERDServer() {
    stop_server();
    hash_table_.clear();
}

/**
 * 初始化服務端
 */
int HERDServer::initialize(uint32_t num_threads) {
    // 初始化哈希表
    hash_table_.resize(num_buckets_);
    for (auto &bucket : hash_table_) {
        bucket.lock = 0;
        for (int i = 0; i < 4; i++) {
            bucket.entries[i].valid = 0;
        }
    }
    
    std::cout << "HERD 服務端 " << server_id_ << " 已初始化" << std::endl;
    std::cout << "  哈希表大小: " << num_buckets_ << " 個桶 (Buckets)" << std::endl;
    std::cout << "  工作線程數: " << num_threads << std::endl;
    
    return 0;
}

/**
 * 啟動服務端
 */
int HERDServer::start_server() {
    running_ = true;
    // 在真實實作中，此處會啟動背景工作線程來處理 RDMA 請求
    std::cout << "HERD 服務端 " << server_id_ << " 已啟動" << std::endl;
    return 0;
}

/**
 * 停止服務端
 */
void HERDServer::stop_server() {
    running_ = false;
    // 等待所有線程結束
    for (auto &t : worker_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    std::cout << "HERD 服務端 " << server_id_ << " 已停止" << std::endl;
}

/**
 * 處理 GET 操作
 */
int HERDServer::handle_get(const std::string &key, std::string &value) {
    std::lock_guard<std::mutex> lock(ht_mutex_);
    total_requests_++;
    total_gets_++;
    return hash_get(key, value);
}

/**
 * 處理 PUT 操作
 */
int HERDServer::handle_put(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lock(ht_mutex_);
    total_requests_++;
    total_puts_++;
    return hash_put(key, value);
}

/**
 * 處理 DELETE 操作
 */
int HERDServer::handle_delete(const std::string &key) {
    std::lock_guard<std::mutex> lock(ht_mutex_);
    total_requests_++;
    return hash_delete(key);
}

/**
 * 列印服務端統計資訊
 */
void HERDServer::print_stats() {
    std::cout << "\n=== HERD 服務端 " << server_id_ << " 統計資訊 ===" << std::endl;
    std::cout << "總請求數: " << total_requests_ << std::endl;
    std::cout << "GET 請求數: " << total_gets_ << std::endl;
    std::cout << "PUT 請求數: " << total_puts_ << std::endl;
    std::cout << "哈希表負載因子: " << (total_puts_ / (double)num_buckets_) << std::endl;
    std::cout << "================================\n" << std::endl;
}

uint64_t HERDServer::hash_key(const std::string &key) const {
    return hash_fnv1a(key.c_str(), key.length());
}

uint32_t HERDServer::get_bucket_index(uint64_t key_hash) const {
    return (key_hash >> 2) % num_buckets_;
}

/**
 * 內部哈希表查詢
 */
int HERDServer::hash_get(const std::string &key, std::string &value) {
    uint64_t key_hash = hash_key(key);
    uint32_t bucket_idx = get_bucket_index(key_hash);
    
    if (bucket_idx >= num_buckets_) {
        return -1;
    }
    
    HERDBucket &bucket = hash_table_[bucket_idx];
    
    // 在桶內進行線性搜尋 (每個桶 4 個項目)
    for (int i = 0; i < 4; i++) {
        HERDEntry &entry = bucket.entries[i];
        if (entry.valid && entry.key_hash == key_hash &&
            entry.key_len == key.length() &&
            memcmp(entry.key, key.c_str(), key.length()) == 0) {
            value.assign(entry.value, entry.value_len);
            return 0;  // 成功找到
        }
    }
    
    return 1;  // 未找到
}

/**
 * 內部哈希表存入
 */
int HERDServer::hash_put(const std::string &key, const std::string &value) {
    if (key.length() > 64 || value.length() > 256) {
        return -1;  // 金鑰或數值過長
    }
    
    uint64_t key_hash = hash_key(key);
    uint32_t bucket_idx = get_bucket_index(key_hash);
    
    if (bucket_idx >= num_buckets_) {
        return -1;
    }
    
    HERDBucket &bucket = hash_table_[bucket_idx];
    
    // 尋找現有的分錄或空位
    int empty_slot = -1;
    for (int i = 0; i < 4; i++) {
        HERDEntry &entry = bucket.entries[i];
        
        if (!entry.valid) {
            if (empty_slot == -1) {
                empty_slot = i;
            }
        } else if (entry.key_hash == key_hash &&
                   entry.key_len == key.length() &&
                   memcmp(entry.key, key.c_str(), key.length()) == 0) {
            // 更新現有分錄
            entry.value_len = value.length();
            memcpy(entry.value, value.c_str(), value.length());
            entry.timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            return 0;
        }
    }
    
    // 插入至空位
    if (empty_slot != -1) {
        HERDEntry &entry = bucket.entries[empty_slot];
        entry.valid = 1;
        entry.key_hash = key_hash;
        entry.key_len = key.length();
        entry.value_len = value.length();
        entry.timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        memcpy(entry.key, key.c_str(), key.length());
        memcpy(entry.value, value.c_str(), value.length());
        return 0;
    }
    
    return -1;  // 桶已滿 (發生哈希碰撞且無空位)
}

/**
 * 內部哈希表刪除
 */
int HERDServer::hash_delete(const std::string &key) {
    uint64_t key_hash = hash_key(key);
    uint32_t bucket_idx = get_bucket_index(key_hash);
    
    if (bucket_idx >= num_buckets_) {
        return -1;
    }
    
    HERDBucket &bucket = hash_table_[bucket_idx];
    
    for (int i = 0; i < 4; i++) {
        HERDEntry &entry = bucket.entries[i];
        if (entry.valid && entry.key_hash == key_hash &&
            entry.key_len == key.length() &&
            memcmp(entry.key, key.c_str(), key.length()) == 0) {
            entry.valid = 0;
            return 0;  // 成功刪除
        }
    }
    
    return 1;  // 未找到
}

void HERDServer::worker_thread_fn() {
    // 工作線程佔位符
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ============ HERDClient 實作 ============

HERDClient::HERDClient(uint32_t client_id, RDMAWrapper *rdma_wrapper)
    : client_id_(client_id), rdma_wrapper_(rdma_wrapper) {
    memset(&req_buf_, 0, sizeof(req_buf_));
    memset(&resp_buf_, 0, sizeof(resp_buf_));
}

HERDClient::~HERDClient() {
}

/**
 * 客戶端連接
 */
int HERDClient::connect(const char *server_addr, uint16_t port) {
    std::cout << "HERD 客戶端 " << client_id_ << " 正在連接至 "
              << server_addr << ":" << port << std::endl;
    return 0;
}

void HERDClient::disconnect() {
    std::cout << "HERD 客戶端 " << client_id_ << " 已斷開連線" << std::endl;
}

/**
 * 客戶端 GET 操作
 */
int HERDClient::get(const std::string &key, std::string &value) {
    auto start = std::chrono::high_resolution_clock::now();
    
    memset(&req_buf_, 0, sizeof(req_buf_));
    req_buf_.opcode = 1;  // 操作碼 1 為 GET
    req_buf_.key_len = key.length();
    memcpy(req_buf_.key, key.c_str(), key.length());
    
    if (send_request(req_buf_) != 0) {
        return -1;
    }
    
    if (recv_response(resp_buf_) != 0) {
        return -1;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        total_requests_++;
        request_latencies_.push_back(latency);
    }
    
    if (resp_buf_.status == 0) {
        value.assign(resp_buf_.value, resp_buf_.value_len);
        return 0;
    }
    
    return 1;  // 找不到
}

/**
 * 客戶端 PUT 操作
 */
int HERDClient::put(const std::string &key, const std::string &value) {
    auto start = std::chrono::high_resolution_clock::now();
    
    if (key.length() > 64 || value.length() > 256) {
        return -1;
    }
    
    memset(&req_buf_, 0, sizeof(req_buf_));
    req_buf_.opcode = 2;  // 操作碼 2 為 PUT
    req_buf_.key_len = key.length();
    req_buf_.value_len = value.length();
    memcpy(req_buf_.key, key.c_str(), key.length());
    memcpy(req_buf_.value, value.c_str(), value.length());
    
    if (send_request(req_buf_) != 0) {
        return -1;
    }
    
    if (recv_response(resp_buf_) != 0) {
        return -1;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        total_requests_++;
        request_latencies_.push_back(latency);
    }
    
    return resp_buf_.status == 0 ? 0 : -1;
}

/**
 * 客戶端 DELETE 操作
 */
int HERDClient::delete_key(const std::string &key) {
    if (key.length() > 64) {
        return -1;
    }
    
    memset(&req_buf_, 0, sizeof(req_buf_));
    req_buf_.opcode = 3;  // 操作碼 3 為 DELETE
    req_buf_.key_len = key.length();
    memcpy(req_buf_.key, key.c_str(), key.length());
    
    if (send_request(req_buf_) != 0) {
        return -1;
    }
    
    if (recv_response(resp_buf_) != 0) {
        return -1;
    }
    
    return resp_buf_.status == 0 ? 0 : 1;
}

int HERDClient::batch_get(const std::vector<std::string> &keys,
                          std::vector<std::string> &values) {
    values.clear();
    for (const auto &key : keys) {
        std::string value;
        if (get(key, value) == 0) {
            values.push_back(value);
        } else {
            values.push_back("");
        }
    }
    return 0;
}

int HERDClient::batch_put(const std::map<std::string, std::string> &kvs) {
    for (const auto &kv : kvs) {
        if (put(kv.first, kv.second) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * 列印客戶端統計資訊
 */
void HERDClient::print_stats() {
    std::cout << "\n=== HERD 客戶端 " << client_id_ << " 統計資訊 ===" << std::endl;
    std::cout << "總請求數: " << total_requests_ << std::endl;
    std::cout << "平均延遲: " << get_avg_latency() << " ns" << std::endl;
    std::cout << "================================\n" << std::endl;
}

double HERDClient::get_avg_latency() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (request_latencies_.empty()) {
        return 0.0;
    }
    
    uint64_t sum = 0;
    for (auto latency : request_latencies_) {
        sum += latency;
    }
    return sum / (double)request_latencies_.size();
}

/**
 * 模擬發送請求 (透過 RDMA UC Write)
 */
int HERDClient::send_request(const HERDRequest &req) {
    return 0;
}

/**
 * 模擬接收回應 (透過 RDMA UD)
 */
int HERDClient::recv_response(HERDResponse &resp) {
    resp.status = 0;  // 預設為成功
    return 0;
}

// ============ HERDBenchmark 實作 ============

HERDBenchmark::HERDBenchmark() {
}

HERDBenchmark::~HERDBenchmark() {
}

/**
 * 測試 GET 操作延遲
 */
int HERDBenchmark::benchmark_get_latency(HERDClient *client, int num_requests) {
    std::cout << "正在運行 HERD GET 延遲基準測試 (" << num_requests << " 次請求)..." << std::endl;
    
    for (int i = 0; i < num_requests; i++) {
        std::string key = "key_" + std::to_string(i % 1000);
        std::string value;
        client->get(key, value);
    }
    
    return 0;
}

/**
 * 測試 PUT 操作延遲
 */
int HERDBenchmark::benchmark_put_latency(HERDClient *client, int num_requests) {
    std::cout << "正在運行 HERD PUT 延遲基準測試 (" << num_requests << " 次請求)..." << std::endl;
    
    for (int i = 0; i < num_requests; i++) {
        std::string key = "key_" + std::to_string(i);
        std::string value = "value_" + std::to_string(i);
        client->put(key, value);
    }
    
    return 0;
}

/**
 * 測試混合負載
 */
int HERDBenchmark::benchmark_mixed_workload(HERDClient *client, int num_requests, 
                                           double write_ratio) {
    std::cout << "正在運行 HERD 混合負載基準測試 (寫入比例: " << write_ratio << ")..." << std::endl;
    
    for (int i = 0; i < num_requests; i++) {
        std::string key = "key_" + std::to_string(i % 10000);
        bool is_write = (rand() / (double)RAND_MAX) < write_ratio;
        
        if (is_write) {
            std::string value = "value_" + std::to_string(i);
            client->put(key, value);
        } else {
            std::string value;
            client->get(key, value);
        }
    }
    
    return 0;
}

/**
 * 測試吞吐量
 */
int HERDBenchmark::benchmark_throughput(HERDClient *client, int num_clients,
                                       int requests_per_client) {
    std::cout << "正在運行 HERD 吞吐量基準測試 (" << num_clients << " 個客戶端, 每人 "
              << requests_per_client << " 次請求)..." << std::endl;
    
    int total_requests = num_clients * requests_per_client;
    
    for (int i = 0; i < total_requests; i++) {
        std::string key = "key_" + std::to_string(i % 10000);
        std::string value = "value_" + std::to_string(i);
        client->put(key, value);
    }
    
    return 0;
}

/**
 * 匯出測試結果
 */
int HERDBenchmark::export_results(const std::string &filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return -1;
    }
    
    file << "HERD 基準測試結果\n";
    file << "======================\n\n";
    file.close();
    
    return 0;
}

/**
 * 分布式事務模組 - OCC (樂觀併發控制) 實作
 * 用於 FaRM DSM 的事務處理機制
 * 
 */

#ifndef DSM_TRANSACTION_H
#define DSM_TRANSACTION_H

#include "rdma_wrapper.h"
#include "memory_manager.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <atomic>

// 事務狀態列舉
enum class TransactionState {
    INIT,        // 初始化
    EXECUTING,   // 執行中
    VALIDATING,  // 驗證中
    COMMITTING,  // 提交中
    COMMITTED,   // 已提交
    ABORTED      // 已中止
};

// 讀取操作記錄
struct ReadOp {
    uint64_t remote_addr;   // 遠端位址
    uint32_t remote_rkey;   // 遠端金鑰
    uint64_t version;       // 讀取時的版本號
    void *local_buffer;     // 本地緩衝區
    size_t size;            // 大小
    std::chrono::high_resolution_clock::time_point timestamp; // 時間戳
};

// 寫入操作記錄
struct WriteOp {
    uint64_t remote_addr;   // 遠端位址
    uint32_t remote_rkey;   // 遠端金鑰
    void *data;             // 待寫入數據
    size_t size;            // 大小
};

/**
 * 帶版本號的物件 - 用於樂觀併發控制
 */
struct VersionedObject {
    uint64_t version;   // 版本號
    uint64_t lock;      // 鎖定狀態 (0: 未鎖定, 1: 已鎖定)
    char data[256];     // 物件數據 (簡化實作：固定 256 位元組)
};

/**
 * OCC 事務類別
 */
class Transaction {
public:
    Transaction(uint64_t txn_id, RDMAWrapper *rdma_wrapper);
    ~Transaction();
    
    // 事務生命週期管理
    void begin();                                                                       // 開始事務
    int read(uint64_t remote_addr, uint32_t remote_rkey, void *buffer, size_t size);    // 讀取數據
    int write(uint64_t remote_addr, uint32_t remote_rkey, const void *data, size_t size); // 寫入數據
    int validate();                                                                    // 驗證事務 (OCC 核心步驟)
    int commit();                                                                      // 提交事務
    void abort();                                                                       // 中止事務
    
    // 狀態管理
    TransactionState get_state() const { return state_; }
    uint64_t get_timestamp() const { return timestamp_; }
    uint64_t get_id() const { return txn_id_; }
    
    // 統計資訊
    int get_read_count() const { return read_set_.size(); }
    int get_write_count() const { return write_set_.size(); }
    
private:
    uint64_t txn_id_;           // 事務 ID
    TransactionState state_;    // 當前狀態
    uint64_t timestamp_;        // 事務開始時間戳
    RDMAWrapper *rdma_wrapper_; // RDMA 工具指標
    
    std::vector<ReadOp> read_set_;   // 讀取集合
    std::vector<WriteOp> write_set_; // 寫入集合
    
    // 驗證階段的內部操作
    int acquire_locks();           // 獲取寫入物件的鎖
    int release_locks();           // 釋放鎖
    int check_read_consistency();  // 檢查讀取一致性
};

/**
 * DSM 節點 - 代表分布式共享記憶體中的一個節點
 */
class DSMNode {
public:
    DSMNode(uint32_t node_id, RDMAWrapper *rdma_wrapper, MemoryManager *mem_mgr);
    ~DSMNode();
    
    // 初始化節點
    int initialize(size_t shared_memory_size);
    
    // 物件管理
    uint64_t create_object(const void *init_data, size_t size);  // 建立物件
    int update_object(uint64_t obj_id, const void *data, size_t size); // 更新物件
    void* get_object_ptr(uint64_t obj_id);                        // 獲取物件指針
    
    // 版本追蹤
    uint64_t get_object_version(uint64_t obj_id); // 獲取物件版本
    int increment_version(uint64_t obj_id);      // 增加版本號
    
    // 心跳與故障檢測
    void send_heartbeat();                                           // 發送心跳
    bool is_alive(uint32_t remote_node_id, int timeout_ms = 5000);   // 檢查遠端節點是否存活
    
    // 統計資訊
    void print_stats();
    
private:
    uint32_t node_id_;          // 節點 ID
    RDMAWrapper *rdma_wrapper_; // RDMA 工具指標
    MemoryManager *mem_mgr_;    // 記憶體管理器指標
    
    std::map<uint64_t, VersionedObject*> objects_; // 管理的物件地圖
    std::atomic<uint64_t> object_counter_{0};      // 物件計數器
    std::atomic<uint64_t> txn_counter_{0};         // 事務計數器
    
    std::chrono::high_resolution_clock::time_point last_heartbeat_; // 最後一次心跳時間
};

/**
 * 事務管理器 - 協調分布式事務的執行
 */
class TransactionManager {
public:
    TransactionManager(DSMNode *local_node);
    ~TransactionManager();
    
    // 事務執行
    std::shared_ptr<Transaction> begin_transaction();             // 開始新事務
    int commit_transaction(std::shared_ptr<Transaction> txn);     // 協調提交事務
    
    // 統計資訊
    uint64_t get_total_transactions() const { return total_txn_count_; }
    uint64_t get_aborted_transactions() const { return aborted_txn_count_; }
    double get_abort_rate() const {
        return total_txn_count_ > 0 ? aborted_txn_count_ / (double)total_txn_count_ : 0.0;
    }
    
private:
    DSMNode *local_node_;                      // 本地節點指標
    std::atomic<uint64_t> total_txn_count_{0};   // 總事務數量
    std::atomic<uint64_t> aborted_txn_count_{0}; // 已中止的事務數量
    std::atomic<uint64_t> txn_id_counter_{0};    // 事務 ID 生成器
};

#endif // DSM_TRANSACTION_H

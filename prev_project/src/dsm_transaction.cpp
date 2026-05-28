
#include "../include/dsm_transaction.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <chrono>

// ============ Transaction 實作 ============

Transaction::Transaction(uint64_t txn_id, RDMAWrapper *rdma_wrapper)
    : txn_id_(txn_id), rdma_wrapper_(rdma_wrapper),
      state_(TransactionState::INIT) {
    timestamp_ = std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

Transaction::~Transaction() {
    // 釋放讀取集合的緩衝區
    for (auto &op : read_set_) {
        if (op.local_buffer) {
            free(op.local_buffer);
        }
    }
    // 釋放寫入集合的緩衝區
    for (auto &op : write_set_) {
        if (op.data) {
            free((void*)op.data);
        }
    }
}

/**
 * 開始事務
 */
void Transaction::begin() {
    state_ = TransactionState::EXECUTING;
    timestamp_ = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    read_set_.clear();
    write_set_.clear();
}

/**
 * 讀取遠端物件
 */
int Transaction::read(uint64_t remote_addr, uint32_t remote_rkey,
                      void *buffer, size_t size) {
    if (state_ != TransactionState::EXECUTING) {
        return -1;
    }
    
    // 記錄讀取操作
    ReadOp op;
    op.remote_addr = remote_addr;
    op.remote_rkey = remote_rkey;
    op.local_buffer = malloc(size);
    op.size = size;
    op.timestamp = std::chrono::high_resolution_clock::now();
    
    if (!op.local_buffer) {
        return -1;
    }
    
    // 執行 RDMA 讀取
    // 在真實實作中，這會觸發非同步 RDMA READ
    // 本專案中簡化為記錄操作
    
    read_set_.push_back(op);
    
    // 複製到用戶提供的緩衝區
    if (buffer) {
        memcpy(buffer, op.local_buffer, size);
    }
    
    return 0;
}

/**
 * 寫入物件 (寫入到本地寫入集)
 */
int Transaction::write(uint64_t remote_addr, uint32_t remote_rkey,
                       const void *data, size_t size) {
    if (state_ != TransactionState::EXECUTING) {
        return -1;
    }
    
    // 記錄寫入操作
    WriteOp op;
    op.remote_addr = remote_addr;
    op.remote_rkey = remote_rkey;
    op.data = malloc(size);
    op.size = size;
    
    if (!op.data) {
        return -1;
    }
    
    memcpy(op.data, data, size);
    write_set_.push_back(op);
    
    return 0;
}

/**
 * 獲取所有寫入物件的鎖 (使用 RDMA CAS)
 */
int Transaction::acquire_locks() {
    // 使用 RDMA 原子操作 CAS 來獲取所有寫入物件的鎖
    for (auto &op : write_set_) {
        uint64_t result;
        // 嘗試將鎖從 0 改為 1
        if (rdma_wrapper_ && rdma_wrapper_->rdma_atomic_cas(op.remote_rkey, op.remote_addr,
                                           0, 1, &result) != 0) {
            return -1;
        }
        // 如果 result 不為 0，代表鎖已被其他事務佔用
        // 此處簡化處理，非 0 即失敗
        if (rdma_wrapper_ && result != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * 釋放所有寫入物件的鎖
 */
int Transaction::release_locks() {
    // 透過 RDMA 寫入來釋放鎖
    for (auto &op : write_set_) {
        uint64_t unlock_val = 0;
        // 在真實實作中，會向鎖的位址寫入 0
    }
    return 0;
}

/**
 * 檢查讀取的一致性
 */
int Transaction::check_read_consistency() {
    // 驗證讀取的物件版本是否自讀取以來未曾改變
    // 本簡化版本假設一致
    return 0;
}

/**
 * 事務驗證階段
 */
int Transaction::validate() {
    if (state_ != TransactionState::EXECUTING) {
        return -1;
    }
    
    state_ = TransactionState::VALIDATING;
    
    // 1. 驗證讀取一致性
    if (check_read_consistency() != 0) {
        abort();
        return -1;
    }
    
    // 2. 獲取寫入物件的鎖
    if (!write_set_.empty()) {
        if (acquire_locks() != 0) {
            abort();
            return -1;
        }
    }
    
    return 0;
}

/**
 * 提交事務
 */
int Transaction::commit() {
    if (state_ != TransactionState::VALIDATING) {
        return -1;
    }
    
    state_ = TransactionState::COMMITTING;
    
    // 1. 執行實際的 RDMA 寫入
    for (auto &op : write_set_) {
        if (rdma_wrapper_ && rdma_wrapper_->rdma_write(nullptr, 0, op.remote_rkey,
                                      op.remote_addr, op.size) != 0) {
            abort();
            return -1;
        }
    }
    
    // 2. 釋放鎖
    if (!write_set_.empty()) {
        release_locks();
    }
    
    state_ = TransactionState::COMMITTED;
    return 0;
}

/**
 * 中止事務
 */
void Transaction::abort() {
    state_ = TransactionState::ABORTED;
    release_locks();
    // 清理資源已在解構子中處理
}

// ============ DSMNode 實作 ============

DSMNode::DSMNode(uint32_t node_id, RDMAWrapper *rdma_wrapper, MemoryManager *mem_mgr)
    : node_id_(node_id), rdma_wrapper_(rdma_wrapper), mem_mgr_(mem_mgr) {
}

DSMNode::~DSMNode() {
    for (auto &pair : objects_) {
        if (pair.second) {
            free(pair.second);
        }
    }
    objects_.clear();
}

/**
 * 初始化節點資訊
 */
int DSMNode::initialize(size_t shared_memory_size) {
    std::cout << "DSM 節點 " << node_id_ << " 初始化完成，共享記憶體大小: "
              << shared_memory_size << " 位元組" << std::endl;
    last_heartbeat_ = std::chrono::high_resolution_clock::now();
    return 0;
}

/**
 * 建立新物件
 */
uint64_t DSMNode::create_object(const void *init_data, size_t size) {
    uint64_t obj_id = object_counter_.fetch_add(1);
    
    // 配置物件空間
    VersionedObject *obj = (VersionedObject *)malloc(sizeof(VersionedObject));
    if (!obj) {
        return 0;
    }
    
    obj->version = 0;
    obj->lock = 0;
    if (init_data && size <= sizeof(obj->data)) {
        memcpy(obj->data, init_data, size);
    }
    
    objects_[obj_id] = obj;
    return obj_id;
}

/**
 * 更新物件數據
 */
int DSMNode::update_object(uint64_t obj_id, const void *data, size_t size) {
    auto it = objects_.find(obj_id);
    if (it == objects_.end()) {
        return -1;
    }
    
    if (size > sizeof(it->second->data)) {
        return -1;
    }
    
    memcpy(it->second->data, data, size);
    it->second->version++;
    
    return 0;
}

void* DSMNode::get_object_ptr(uint64_t obj_id) {
    auto it = objects_.find(obj_id);
    if (it != objects_.end()) {
        return it->second->data;
    }
    return nullptr;
}

uint64_t DSMNode::get_object_version(uint64_t obj_id) {
    auto it = objects_.find(obj_id);
    if (it != objects_.end()) {
        return it->second->version;
    }
    return 0;
}

int DSMNode::increment_version(uint64_t obj_id) {
    auto it = objects_.find(obj_id);
    if (it != objects_.end()) {
        it->second->version++;
        return 0;
    }
    return -1;
}

/**
 * 發送心跳訊號
 */
void DSMNode::send_heartbeat() {
    last_heartbeat_ = std::chrono::high_resolution_clock::now();
}

/**
 * 檢查節點是否存活
 */
bool DSMNode::is_alive(uint32_t remote_node_id, int timeout_ms) {
    return true; // 簡化實作，預設皆存活
}

/**
 * 列印節點統計資訊
 */
void DSMNode::print_stats() {
    std::cout << "\n=== DSM 節點 " << node_id_ << " 統計資訊 ===" << std::endl;
    std::cout << "總物件數: " << objects_.size() << std::endl;
    std::cout << "總事務數: " << txn_counter_.load() << std::endl;
    std::cout << "================================\n" << std::endl;
}

// ============ TransactionManager 實作 ============

TransactionManager::TransactionManager(DSMNode *local_node)
    : local_node_(local_node) {
}

TransactionManager::~TransactionManager() {
}

/**
 * 開始一個新事務
 */
std::shared_ptr<Transaction> TransactionManager::begin_transaction() {
    uint64_t txn_id = txn_id_counter_.fetch_add(1);
    auto txn = std::make_shared<Transaction>(txn_id, nullptr);
    txn->begin();
    return txn;
}

/**
 * 協調事務提交
 */
int TransactionManager::commit_transaction(std::shared_ptr<Transaction> txn) {
    if (!txn) {
        return -1;
    }
    
    // 執行驗證
    if (txn->validate() != 0) {
        aborted_txn_count_++;
        total_txn_count_++;
        return -1;
    }
    
    // 執行提交
    if (txn->commit() != 0) {
        aborted_txn_count_++;
        total_txn_count_++;
        return -1;
    }
    
    total_txn_count_++;
    return 0;
}

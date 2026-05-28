/**
 * 記憶體管理器 - Slab 分配器實作
 * 預先分配大型記憶體區塊以減少 RDMA 記憶體註冊 (ibv_reg_mr) 的開銷
 * 
 */

#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include "rdma_wrapper.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>

/**
 * Slab - 連續的記憶體區塊
 */
class Slab {
public:
    Slab(size_t size, std::shared_ptr<RDMAMemoryRegion> mr);
    ~Slab();
    
    void* allocate(size_t size);  // 在 Slab 內分配空間
    void deallocate(void *ptr);   // 釋放空間
    
    size_t total_size() const { return total_size_; }
    size_t available_size() const { return available_size_; }
    size_t used_size() const { return total_size_ - available_size_; }
    
    std::shared_ptr<RDMAMemoryRegion> get_memory_region() const { return mr_; }
    void* get_base_addr() const { return base_addr_; }
    
    double utilization() const {
        return (total_size_ - available_size_) / (double)total_size_;
    }
    
private:
    void *base_addr_;
    size_t total_size_;
    size_t available_size_;
    std::shared_ptr<RDMAMemoryRegion> mr_;
    std::unordered_map<void*, size_t> allocation_map_;
    std::mutex mutex_;
};

/**
 * 記憶體管理器 - 管理多個 Slab
 */
class MemoryManager {
public:
    explicit MemoryManager(RDMAWrapper *rdma_wrapper);
    ~MemoryManager();
    
    // 初始化預分配的 Slab
    int initialize(size_t total_size, size_t slab_size);
    
    // 分配記憶體 (返回已在 RDMA 註冊的記憶體位址)
    void* allocate(size_t size);
    
    // 釋放記憶體
    void deallocate(void *ptr);
    
    // 獲取指針對應的 RDMA 記憶體區域資訊
    std::shared_ptr<RDMAMemoryRegion> get_memory_region(void *ptr);
    
    // 統計資訊
    void print_stats();
    size_t total_registered_memory() const;
    double memory_utilization() const;
    
    // 巨型頁 (Hugepages) 支持
    int enable_hugepages(size_t hugepage_size = 2 * 1024 * 1024);
    void disable_hugepages();
    
private:
    RDMAWrapper *rdma_wrapper_;
    std::vector<std::shared_ptr<Slab>> slabs_;
    std::unordered_map<void*, std::shared_ptr<Slab>> ptr_to_slab_map_;
    std::mutex mutex_;
    bool use_hugepages_ = false;
    
    std::shared_ptr<Slab> create_slab(size_t size);
    std::shared_ptr<Slab> find_slab_for_allocation(size_t size);
};

#endif // MEMORY_MANAGER_H


#include "../include/memory_manager.h"
#include <iostream>
#include <algorithm>
#include <cstring>

// ============ Slab 實作 ============

Slab::Slab(size_t size, std::shared_ptr<RDMAMemoryRegion> mr)
    : total_size_(size), available_size_(size), mr_(mr) {
    base_addr_ = mr ? mr->addr : nullptr;
}

Slab::~Slab() {
    allocation_map_.clear();
}

/**
 * 在 Slab 中分配記憶體
 */
void* Slab::allocate(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (size > available_size_) {
        return nullptr;
    }
    
    // 簡單的 First-fit 分配演算法
    void *ptr = nullptr;
    if (allocation_map_.empty()) {
        ptr = base_addr_;
    } else {
        // 尋找第一個足夠大的空隙
        uint8_t *current = (uint8_t *)base_addr_;
        for (auto &pair : allocation_map_) {
            uint8_t *alloc_ptr = (uint8_t *)pair.first;
            size_t alloc_size = pair.second;
            
            size_t gap_size = alloc_ptr - current;
            if (gap_size >= size) {
                ptr = current;
                break;
            }
            current = alloc_ptr + alloc_size;
        }
        
        if (!ptr) {
            // 分配在最後一個分配區塊之後
            ptr = current;
        }
    }
    
    // 檢查是否超出 Slab 邊界
    if ((uint8_t *)ptr + size <= (uint8_t *)base_addr_ + total_size_) {
        allocation_map_[ptr] = size;
        available_size_ -= size;
        return ptr;
    }
    
    return nullptr;
}

/**
 * 釋放 Slab 中的記憶體
 */
void Slab::deallocate(void *ptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = allocation_map_.find(ptr);
    if (it != allocation_map_.end()) {
        available_size_ += it->second;
        allocation_map_.erase(it);
    }
}

// ============ MemoryManager 實作 ============

MemoryManager::MemoryManager(RDMAWrapper *rdma_wrapper)
    : rdma_wrapper_(rdma_wrapper) {
}

MemoryManager::~MemoryManager() {
    slabs_.clear();
    ptr_to_slab_map_.clear();
}

/**
 * 初始化記憶體池
 * @param total_size 總記憶體大小
 * @param slab_size 每個 Slab 的大小
 */
int MemoryManager::initialize(size_t total_size, size_t slab_size) {
    if (!rdma_wrapper_) {
        return -1;
    }
    
    size_t allocated = 0;
    while (allocated < total_size) {
        size_t current_slab_size = std::min(slab_size, total_size - allocated);
        
        // 分配記憶體
        void *slab_mem = malloc(current_slab_size);
        if (!slab_mem) {
            std::cerr << "無法分配 Slab 記憶體" << std::endl;
            return -1;
        }
        
        // 向 RDMA 註冊
        auto mr = rdma_wrapper_->register_memory(slab_mem, current_slab_size);
        if (!mr) {
            std::cerr << "無法註冊 Slab 記憶體至 RDMA" << std::endl;
            free(slab_mem);
            return -1;
        }
        
        // 建立 Slab 物件
        auto slab = std::make_shared<Slab>(current_slab_size, mr);
        slabs_.push_back(slab);
        
        allocated += current_slab_size;
    }
    
    std::cout << "記憶體管理器初始化完成，共建立 " << slabs_.size() 
              << " 個 Slabs (總計 " << allocated << " 位元組)" << std::endl;
    
    return 0;
}

/**
 * 分配記憶體
 */
void* MemoryManager::allocate(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 尋找有足夠空間的 Slab
    for (auto &slab : slabs_) {
        if (slab->available_size() >= size) {
            void *ptr = slab->allocate(size);
            if (ptr) {
                ptr_to_slab_map_[ptr] = slab;
                return ptr;
            }
        }
    }
    
    std::cerr << "沒有足夠空間的 Slab 可供分配 " << size << " 位元組" << std::endl;
    return nullptr;
}

/**
 * 釋放記憶體
 */
void MemoryManager::deallocate(void *ptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ptr_to_slab_map_.find(ptr);
    if (it != ptr_to_slab_map_.end()) {
        auto slab = it->second;
        slab->deallocate(ptr);
        ptr_to_slab_map_.erase(it);
    }
}

/**
 * 獲取 RDMA 記憶體區域資訊
 */
std::shared_ptr<RDMAMemoryRegion> MemoryManager::get_memory_region(void *ptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ptr_to_slab_map_.find(ptr);
    if (it != ptr_to_slab_map_.end()) {
        return it->second->get_memory_region();
    }
    
    return nullptr;
}

/**
 * 列印記憶體使用統計
 */
void MemoryManager::print_stats() {
    std::cout << "\n=== 記憶體管理器統計資訊 ===" << std::endl;
    std::cout << "總 Slab 數量: " << slabs_.size() << std::endl;
    std::cout << "總分配次數: " << ptr_to_slab_map_.size() << std::endl;
    
    size_t total_size = 0;
    size_t total_used = 0;
    
    for (size_t i = 0; i < slabs_.size(); i++) {
        auto &slab = slabs_[i];
        size_t used = slab->used_size();
        total_size += slab->total_size();
        total_used += used;
        
        std::cout << "  Slab " << i << ": " << used << "/" << slab->total_size() 
                  << " 位元組 (" << (slab->utilization() * 100.0) << "%)" << std::endl;
    }
    
    std::cout << "總計記憶體: " << total_used << "/" << total_size << " 位元組" << std::endl;
    std::cout << "總體利用率: " << (total_size > 0 ? (total_used * 100.0 / total_size) : 0) << "%" << std::endl;
    std::cout << "================================\n" << std::endl;
}

size_t MemoryManager::total_registered_memory() const {
    size_t total = 0;
    for (const auto &slab : slabs_) {
        total += slab->total_size();
    }
    return total;
}

double MemoryManager::memory_utilization() const {
    size_t total = 0;
    size_t used = 0;
    for (const auto &slab : slabs_) {
        total += slab->total_size();
        used += slab->used_size();
    }
    return total > 0 ? used / (double)total : 0.0;
}

/**
 * 啟用巨型頁支持 (Hugepages)
 */
int MemoryManager::enable_hugepages(size_t hugepage_size) {
    use_hugepages_ = true;
    std::cout << "巨型頁 (Hugepages) 已啟用 (大小: " << hugepage_size << " 位元組)" << std::endl;
    return 0;
}

void MemoryManager::disable_hugepages() {
    use_hugepages_ = false;
}

std::shared_ptr<Slab> MemoryManager::create_slab(size_t size) {
    void *slab_mem = malloc(size);
    if (!slab_mem) {
        return nullptr;
    }
    
    auto mr = rdma_wrapper_->register_memory(slab_mem, size);
    if (!mr) {
        free(slab_mem);
        return nullptr;
    }
    
    return std::make_shared<Slab>(size, mr);
}

std::shared_ptr<Slab> MemoryManager::find_slab_for_allocation(size_t size) {
    for (auto &slab : slabs_) {
        if (slab->available_size() >= size) {
            return slab;
        }
    }
    return nullptr;
}

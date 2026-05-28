/**
 * RDMA Verbs API 封裝層
 * 為 FaRM DSM 實作提供簡化的 RDMA 操作介面
 * 
 */

#ifndef RDMA_WRAPPER_H
#define RDMA_WRAPPER_H

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <cstring>
#include <vector>
#include <memory>
#include <iostream>

// 常數定義
const int MAX_INLINE_SIZE = 256;    // 最大內聯數據大小
const int CQ_DEPTH = 1024;         // 完成隊列深度
const int QP_DEPTH = 512;          // 隊列對深度
const int MAX_SGE = 2;             // 最大散集元素數量
const uint16_t RDMA_PORT = 20079;  // RDMA 服務埠號
const int MSG_TIMEOUT_MS = 1000;   // 訊息超時時間（毫秒）

/**
 * RDMA 連線上下文
 * 儲存特定連線的 RDMA 資源，如保護域、完成隊列和隊列對
 */
struct RDMAConnContext {
    struct ibv_context *context = nullptr;           // 裝置上下文
    struct ibv_pd *pd = nullptr;                     // 保護域 (Protection Domain)
    struct ibv_cq *cq = nullptr;                     // 完成隊列 (Completion Queue)
    struct ibv_qp *qp = nullptr;                     // 隊列對 (Queue Pair)
    struct ibv_comp_channel *comp_channel = nullptr; // 完成通知通道
    
    uint32_t local_qpn = 0;   // 本地隊列對編號
    uint32_t remote_qpn = 0;  // 遠端隊列對編號
    uint32_t local_psn = 0;   // 本地封包序列號
    uint32_t remote_psn = 0;  // 遠端封包序列號
    
    uint16_t lid = 0;         // 本地識別碼 (Local Identifier)
    uint8_t gid_index = 0;    // 全域識別碼索引
    union ibv_gid local_gid;  // 本地全域識別碼
    union ibv_gid remote_gid; // 遠端全域識別碼
    
    ~RDMAConnContext();
};

/**
 * RDMA 記憶體區域封裝
 * 代表已在 RDMA 裝置註冊的記憶體區域
 */
struct RDMAMemoryRegion {
    struct ibv_mr *mr = nullptr; // 註冊的記憶體區域指標
    void *addr = nullptr;        // 起始位址
    size_t size = 0;             // 大小
    uint32_t rkey = 0;           // 遠端存取金鑰 (用於遠端讀取/寫入)
    uint32_t lkey = 0;           // 本地存取金鑰
    
    RDMAMemoryRegion(struct ibv_mr *m) : mr(m) {
        if (mr) {
            addr = mr->addr;
            size = mr->length;
            rkey = mr->rkey;
            lkey = mr->lkey;
        }
    }
    
    ~RDMAMemoryRegion();
};

/**
 * RDMA 封裝類別
 * 提供 RDMA 操作的高階介面，隱藏底層 Verbs API 的複雜性
 */
class RDMAWrapper {
public:
    RDMAWrapper();
    ~RDMAWrapper();
    
    // 裝置管理
    int init_device(const char *device_name = nullptr); // 初始化 RDMA 裝置
    void cleanup();                                    // 清理資源
    
    // 連線管理
    int setup_connection();                                   // 設定連線參數
    int connect_remote(const char *remote_addr, uint16_t port); // 建立與遠端的連線
    
    // 記憶體管理
    std::shared_ptr<RDMAMemoryRegion> register_memory(void *addr, size_t size); // 註冊記憶體
    void unregister_memory(std::shared_ptr<RDMAMemoryRegion> mr);              // 取消註冊
    
    // 單邊操作 (One-sided operations)
    int rdma_write(std::shared_ptr<RDMAMemoryRegion> local_mr,
                   uint64_t local_offset,
                   uint32_t remote_rkey,
                   uint64_t remote_addr,
                   size_t length); // RDMA 寫入
    
    int rdma_read(std::shared_ptr<RDMAMemoryRegion> local_mr,
                  uint64_t local_offset,
                  uint32_t remote_rkey,
                  uint64_t remote_addr,
                  size_t length); // RDMA 讀取
    
    int rdma_atomic_cas(uint32_t remote_rkey,
                       uint64_t remote_addr,
                       uint64_t compare_val,
                       uint64_t swap_val,
                       uint64_t *result); // RDMA 原子操作 (比較並交換)
    
    // 雙邊操作 (Two-sided operations)
    int send_message(const void *buf, size_t length); // 發送訊息
    int recv_message(void *buf, size_t max_length);  // 接收訊息
    
    // 輪詢與事件處理
    int poll_completion(struct ibv_wc *wc, int num_entries); // 輪詢完成隊列
    int wait_completion(struct ibv_wc *wc, int num_entries, int timeout_ms = MSG_TIMEOUT_MS); // 等待完成
    
    // 工具函數
    void print_device_info();                               // 列印裝置資訊
    const char* get_device_name() const { return device_name; }
    
private:
    char device_name[256] = {0};
    std::vector<struct ibv_device*> device_list;
    std::shared_ptr<RDMAConnContext> conn_ctx;
    
    int create_completion_queue(); // 建立完成隊列
    int create_queue_pair();       // 建立隊列對
    int modify_qp_to_rtr();        // 修改隊列對狀態至準備接收 (Ready to Receive)
    int modify_qp_to_rts();        // 修改隊列對狀態至準備發送 (Ready to Send)
};

#endif // RDMA_WRAPPER_H

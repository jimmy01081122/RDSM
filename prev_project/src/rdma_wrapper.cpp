

#include "../include/rdma_wrapper.h"
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

// ============ RDMAConnContext 解構子 ============
RDMAConnContext::~RDMAConnContext() {
    if (qp) ibv_destroy_qp(qp);
    if (cq) ibv_destroy_cq(cq);
    if (comp_channel) ibv_destroy_comp_channel(comp_channel);
    if (pd) ibv_dealloc_pd(pd);
    if (context) ibv_close_device(context);
}

// ============ RDMAMemoryRegion 解構子 ============
RDMAMemoryRegion::~RDMAMemoryRegion() {
    if (mr) {
        ibv_dereg_mr(mr);
    }
}

// ============ RDMAWrapper 實作 ============

RDMAWrapper::RDMAWrapper() {
    memset(device_name, 0, sizeof(device_name));
}

RDMAWrapper::~RDMAWrapper() {
    cleanup();
}

/**
 * 初始化 RDMA 裝置
 * @param device_name_param 裝置名稱，若為 nullptr 則使用第一個找到的裝置
 * @return 0 成功，-1 失敗
 */
int RDMAWrapper::init_device(const char *device_name_param) {
    int num_devices = 0;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    
    if (!dev_list) {
        std::cerr << "無法獲取 IB 裝置列表" << std::endl;
        return -1;
    }
    
    if (num_devices == 0) {
        std::cerr << "未找到 IB 裝置。注意：可能需要設定 Soft-RoCE。" << std::endl;
        ibv_free_device_list(dev_list);
        return -1;
    }
    
    // 選擇裝置
    struct ibv_device *device = nullptr;
    if (device_name_param) {
        for (int i = 0; i < num_devices; i++) {
            if (strcmp(ibv_get_device_name(dev_list[i]), device_name_param) == 0) {
                device = dev_list[i];
                break;
            }
        }
        if (!device) {
            std::cerr << "找不到裝置 " << device_name_param << std::endl;
            ibv_free_device_list(dev_list);
            return -1;
        }
    } else {
        device = dev_list[0]; // 使用第一個裝置
    }
    
    strcpy(device_name, ibv_get_device_name(device));
    
    // 開啟裝置
    struct ibv_context *ctx = ibv_open_device(device);
    if (!ctx) {
        std::cerr << "無法開啟裝置 " << device_name << std::endl;
        ibv_free_device_list(dev_list);
        return -1;
    }
    
    conn_ctx = std::make_shared<RDMAConnContext>();
    conn_ctx->context = ctx;
    
    // 查詢裝置屬性
    struct ibv_device_attr device_attr;
    if (ibv_query_device(ctx, &device_attr)) {
        std::cerr << "無法查詢裝置屬性" << std::endl;
        ibv_free_device_list(dev_list);
        return -1;
    }
    
    // 配置保護域 (Protection Domain)
    conn_ctx->pd = ibv_alloc_pd(ctx);
    if (!conn_ctx->pd) {
        std::cerr << "無法配置保護域 (PD)" << std::endl;
        ibv_free_device_list(dev_list);
        return -1;
    }
    
    print_device_info();
    ibv_free_device_list(dev_list);
    
    return 0;
}

/**
 * 建立完成隊列 (Completion Queue)
 */
int RDMAWrapper::create_completion_queue() {
    if (!conn_ctx || !conn_ctx->context) {
        return -1;
    }
    
    // 建立完成通知通道
    conn_ctx->comp_channel = ibv_create_comp_channel(conn_ctx->context);
    if (!conn_ctx->comp_channel) {
        std::cerr << "無法建立完成通道" << std::endl;
        return -1;
    }
    
    // 建立完成隊列
    conn_ctx->cq = ibv_create_cq(conn_ctx->context, CQ_DEPTH,
                                  nullptr, conn_ctx->comp_channel, 0);
    if (!conn_ctx->cq) {
        std::cerr << "無法建立完成隊列 (CQ)" << std::endl;
        return -1;
    }
    
    return 0;
}

/**
 * 建立隊列對 (Queue Pair)
 */
int RDMAWrapper::create_queue_pair() {
    if (!conn_ctx || !conn_ctx->pd || !conn_ctx->cq) {
        return -1;
    }
    
    struct ibv_qp_init_attr qp_init_attr = {};
    qp_init_attr.qp_type = IBV_QPT_RC;  // 可靠連線 (Reliable Connected)
    qp_init_attr.send_cq = conn_ctx->cq;
    qp_init_attr.recv_cq = conn_ctx->cq;
    qp_init_attr.cap.max_send_wr = QP_DEPTH;
    qp_init_attr.cap.max_recv_wr = QP_DEPTH;
    qp_init_attr.cap.max_send_sge = MAX_SGE;
    qp_init_attr.cap.max_recv_sge = MAX_SGE;
    qp_init_attr.cap.max_inline_data = MAX_INLINE_SIZE;
    
    conn_ctx->qp = ibv_create_qp(conn_ctx->pd, &qp_init_attr);
    if (!conn_ctx->qp) {
        std::cerr << "無法建立隊列對 (QP)" << std::endl;
        return -1;
    }
    
    conn_ctx->local_qpn = conn_ctx->qp->qp_num;
    
    // 將 QP 修改為 INIT 狀態
    struct ibv_qp_attr qp_attr = {};
    
    qp_attr.qp_state = IBV_QPS_INIT;
    qp_attr.pkey_index = 0;
    qp_attr.port_num = 1;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | 
                               IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_LOCAL_WRITE;
    
    int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    if (ibv_modify_qp(conn_ctx->qp, &qp_attr, flags)) {
        std::cerr << "無法將 QP 修改為 INIT 狀態" << std::endl;
        return -1;
    }
    
    return 0;
}

/**
 * 將 QP 修改為 RTR (Ready to Receive) 狀態
 */
int RDMAWrapper::modify_qp_to_rtr() {
    if (!conn_ctx || !conn_ctx->qp) {
        return -1;
    }
    
    struct ibv_qp_attr qp_attr = {};
    
    qp_attr.qp_state = IBV_QPS_RTR;
    qp_attr.path_mtu = IBV_MTU_1024;
    qp_attr.dest_qp_num = conn_ctx->remote_qpn;
    qp_attr.rq_psn = conn_ctx->remote_psn;
    qp_attr.max_dest_rd_atomic = 1;
    qp_attr.min_rnr_timer = 12;
    
    qp_attr.ah_attr.is_global = 0;
    qp_attr.ah_attr.dlid = conn_ctx->lid;
    qp_attr.ah_attr.sl = 0;
    qp_attr.ah_attr.src_path_bits = 0;
    qp_attr.ah_attr.port_num = 1;
    
    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER;
    
    if (ibv_modify_qp(conn_ctx->qp, &qp_attr, flags)) {
        std::cerr << "無法將 QP 修改為 RTR 狀態" << std::endl;
        return -1;
    }
    
    return 0;
}

/**
 * 將 QP 修改為 RTS (Ready to Send) 狀態
 */
int RDMAWrapper::modify_qp_to_rts() {
    if (!conn_ctx || !conn_ctx->qp) {
        return -1;
    }
    
    struct ibv_qp_attr qp_attr = {};
    
    qp_attr.qp_state = IBV_QPS_RTS;
    qp_attr.timeout = 14;
    qp_attr.retry_cnt = 7;
    qp_attr.rnr_retry = 7;
    qp_attr.sq_psn = conn_ctx->local_psn;
    qp_attr.max_rd_atomic = 1;
    
    int flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    
    if (ibv_modify_qp(conn_ctx->qp, &qp_attr, flags)) {
        std::cerr << "無法將 QP 修改為 RTS 狀態" << std::endl;
        return -1;
    }
    
    return 0;
}

/**
 * 設定連線資訊
 */
int RDMAWrapper::setup_connection() {
    if (create_completion_queue() != 0) {
        return -1;
    }
    
    if (create_queue_pair() != 0) {
        return -1;
    }
    
    // 查詢埠屬性以獲取 LID
    struct ibv_port_attr port_attr;
    if (ibv_query_port(conn_ctx->context, 1, &port_attr)) {
        std::cerr << "無法查詢埠屬性" << std::endl;
        return -1;
    }
    
    conn_ctx->lid = port_attr.lid;
    
    // 獲取 GID
    if (ibv_query_gid(conn_ctx->context, 1, 0, &conn_ctx->local_gid)) {
        std::cerr << "無法查詢 GID" << std::endl;
        return -1;
    }
    
    // 生成隨機序列號 (PSN)
    conn_ctx->local_psn = rand() & 0xffffff;
    
    std::cout << "RDMA 連線設定完成" << std::endl;
    std::cout << "  本地 QPN: " << conn_ctx->local_qpn << std::endl;
    std::cout << "  本地 PSN: " << conn_ctx->local_psn << std::endl;
    std::cout << "  本地 LID: " << conn_ctx->lid << std::endl;
    
    return 0;
}

/**
 * 模擬連接到遠端
 */
int RDMAWrapper::connect_remote(const char *remote_addr, uint16_t port) {
    std::cout << "正在連接至 " << remote_addr << ":" << port << std::endl;
    return 0;
}

/**
 * 註冊記憶體區域
 */
std::shared_ptr<RDMAMemoryRegion> RDMAWrapper::register_memory(void *addr, size_t size) {
    if (!conn_ctx || !conn_ctx->pd) {
        return nullptr;
    }
    
    int flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | 
                IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
    
    struct ibv_mr *mr = ibv_reg_mr(conn_ctx->pd, addr, size, flags);
    if (!mr) {
        std::cerr << "記憶體區域註冊失敗" << std::endl;
        return nullptr;
    }
    
    return std::make_shared<RDMAMemoryRegion>(mr);
}

void RDMAWrapper::unregister_memory(std::shared_ptr<RDMAMemoryRegion> mr) {
    // 智慧指標會自動處理釋放
}

/**
 * 執行 RDMA 寫入操作
 */
int RDMAWrapper::rdma_write(std::shared_ptr<RDMAMemoryRegion> local_mr,
                            uint64_t local_offset,
                            uint32_t remote_rkey,
                            uint64_t remote_addr,
                            size_t length) {
    if (!conn_ctx || !conn_ctx->qp || !local_mr) {
        return -1;
    }
    
    struct ibv_sge sge;
    sge.addr = (uint64_t)local_mr->addr + local_offset;
    sge.length = length;
    sge.lkey = local_mr->lkey;
    
    struct ibv_send_wr wr = {};
    struct ibv_send_wr *bad_wr = nullptr;
    
    wr.wr_id = (uint64_t)this;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = remote_rkey;
    
    if (ibv_post_send(conn_ctx->qp, &wr, &bad_wr)) {
        std::cerr << "發送 RDMA WRITE 請求失敗" << std::endl;
        return -1;
    }
    
    return 0;
}

/**
 * 執行 RDMA 讀取操作
 */
int RDMAWrapper::rdma_read(std::shared_ptr<RDMAMemoryRegion> local_mr,
                           uint64_t local_offset,
                           uint32_t remote_rkey,
                           uint64_t remote_addr,
                           size_t length) {
    if (!conn_ctx || !conn_ctx->qp || !local_mr) {
        return -1;
    }
    
    struct ibv_sge sge;
    sge.addr = (uint64_t)local_mr->addr + local_offset;
    sge.length = length;
    sge.lkey = local_mr->lkey;
    
    struct ibv_send_wr wr = {};
    struct ibv_send_wr *bad_wr = nullptr;
    
    wr.wr_id = (uint64_t)this;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = remote_rkey;
    
    if (ibv_post_send(conn_ctx->qp, &wr, &bad_wr)) {
        std::cerr << "發送 RDMA READ 請求失敗" << std::endl;
        return -1;
    }
    
    return 0;
}

/**
 * 執行 RDMA 原子操作 (CAS)
 */
int RDMAWrapper::rdma_atomic_cas(uint32_t remote_rkey,
                                 uint64_t remote_addr,
                                 uint64_t compare_val,
                                 uint64_t swap_val,
                                 uint64_t *result) {
    if (!conn_ctx || !conn_ctx->qp) {
        return -1;
    }
    
    // 配置用於儲存原子操作結果的本地緩衝區
    size_t result_size = sizeof(uint64_t);
    void *result_buf = malloc(result_size);
    if (!result_buf) {
        return -1;
    }
    
    auto mr = register_memory(result_buf, result_size);
    if (!mr) {
        free(result_buf);
        return -1;
    }
    
    struct ibv_sge sge;
    sge.addr = (uint64_t)result_buf;
    sge.length = result_size;
    sge.lkey = mr->lkey;
    
    struct ibv_send_wr wr = {};
    struct ibv_send_wr *bad_wr = nullptr;
    
    wr.wr_id = (uint64_t)this;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
    wr.wr.atomic.remote_addr = remote_addr;
    wr.wr.atomic.rkey = remote_rkey;
    wr.wr.atomic.compare_add = compare_val;
    wr.wr.atomic.swap = swap_val;
    
    if (ibv_post_send(conn_ctx->qp, &wr, &bad_wr)) {
        std::cerr << "發送 ATOMIC CAS 請求失敗" << std::endl;
        free(result_buf);
        return -1;
    }
    
    // 等待操作完成
    struct ibv_wc wc;
    if (wait_completion(&wc, 1) > 0 && wc.status == IBV_WC_SUCCESS) {
        if (result) {
            *result = *(uint64_t*)result_buf;
        }
    }
    
    unregister_memory(mr);
    free(result_buf);
    
    return 0;
}

/**
 * 輪詢完成隊列
 */
int RDMAWrapper::poll_completion(struct ibv_wc *wc, int num_entries) {
    if (!conn_ctx || !conn_ctx->cq) {
        return -1;
    }
    
    return ibv_poll_cq(conn_ctx->cq, num_entries, wc);
}

/**
 * 等待完成事件 (帶超時)
 */
int RDMAWrapper::wait_completion(struct ibv_wc *wc, int num_entries, int timeout_ms) {
    int n = poll_completion(wc, num_entries);
    if (n > 0) {
        return n;
    }
    
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count() < timeout_ms) {
        n = poll_completion(wc, num_entries);
        if (n > 0) {
            return n;
        }
        usleep(10);  // 暫停 10 微秒
    }
    
    return 0;
}

int RDMAWrapper::send_message(const void *buf, size_t length) {
    // 雙邊發送操作佔位符
    return 0;
}

int RDMAWrapper::recv_message(void *buf, size_t max_length) {
    // 雙邊接收操作佔位符
    return 0;
}

/**
 * 列印裝置資訊
 */
void RDMAWrapper::print_device_info() {
    if (!conn_ctx || !conn_ctx->context) {
        return;
    }
    
    struct ibv_device_attr device_attr;
    if (ibv_query_device(conn_ctx->context, &device_attr)) {
        std::cerr << "無法查詢裝置屬性" << std::endl;
        return;
    }
    
    std::cout << "\n=== RDMA 裝置資訊 ===" << std::endl;
    std::cout << "裝置名稱: " << device_name << std::endl;
    std::cout << "韌體版本: " << device_attr.fw_ver << std::endl;
    std::cout << "最大 MR 大小: " << device_attr.max_mr_size << " 位元組" << std::endl;
    std::cout << "最大 CQE 數量: " << device_attr.max_cqe << std::endl;
    std::cout << "最大 QP 數量: " << device_attr.max_qp << std::endl;
    std::cout << "最大 QP WR 數量: " << device_attr.max_qp_wr << std::endl;
    std::cout << "最大 SGE 數量: " << device_attr.max_sge << std::endl;
    std::cout << "================================\n" << std::endl;
}

void RDMAWrapper::cleanup() {
    conn_ctx.reset();
}

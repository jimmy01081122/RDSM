#ifndef RDMA_CONN_H
#define RDMA_CONN_H

#include <stdint.h>
#include <string>
#include <memory>
#include <vector>

struct RDMAConfig {
    std::string device_name;      // e.g., "rxe0"
    uint16_t ib_port;            // IB port number
    int gid_index;                // GID index (-1 for default)
    uint32_t max_wr;              // max work requests
    uint32_t max_sge;             // max scatter-gather entries
};

struct RDMAMemRegion {
    void* addr;
    uint32_t length;
    uint32_t lkey;
    uint32_t rkey;
};

class RDMAConnection {
public:
    RDMAConnection(const RDMAConfig& config);
    ~RDMAConnection();
    RDMAConnection(const RDMAConnection&) = delete;
    RDMAConnection& operator=(const RDMAConnection&) = delete;

    // Server-side (passive, listening)
    int listen(uint16_t port);

    // Client-side (active, connects to server)
    int connect(const std::string& server_addr, uint16_t server_port);

    // Memory operations
    RDMAMemRegion register_memory(void* addr, uint32_t length);
    void unregister_memory(uint32_t lkey);

    // RDMA operations (enqueue WR)
    int rdma_read(uint64_t remote_addr, uint32_t remote_rkey,
                  void* local_addr, uint32_t local_lkey, uint32_t length,
                  uint64_t wr_id);

    int rdma_write(uint64_t remote_addr, uint32_t remote_rkey,
                   void* local_addr, uint32_t local_lkey, uint32_t length,
                   uint64_t wr_id);

    // Atomic CAS
    int rdma_compare_swap(uint64_t remote_addr, uint32_t remote_rkey,
                          uint64_t compare_val, uint64_t swap_val,
                          void* local_result_addr, uint32_t local_result_lkey,
                          uint64_t wr_id);

    // Polling for completions
    int poll_cq(int max_entries);

    // Get completion info (after poll)
    struct Completion {
        uint64_t wr_id;
        int status;  // 0 = success, else error
    };

    int get_completions(std::vector<Completion>& completions, int max_count);

    // Query state
    bool is_connected() const { return connected_; }
    uint64_t get_server_addr() const { return server_mr_addr_; }
    uint32_t get_server_rkey() const { return server_rkey_; }

private:
    RDMAConfig config_;
    bool connected_;
    uint64_t server_mr_addr_;    // Remote memory region address (shared by server)
    uint32_t server_rkey_;        // Remote memory region key

    // RDMA context pointers (simplified, actual impl would have ibv_*_ptr types)
    void* context_;
    void* pd_;
    void* cq_;
    void* qp_;
    void* mr_list_;
};

#endif // RDMA_CONN_H

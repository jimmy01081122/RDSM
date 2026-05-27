#include "rdma_conn.h"
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <map>

struct RDMAConnectionImpl {
    struct rdma_cm_id* cm_id;
    struct rdma_cm_id* listen_id;
    struct ibv_pd* pd;
    struct ibv_cq* cq;
    struct ibv_qp* qp;
    std::map<uint32_t, struct ibv_mr*> mr_map;
    bool is_server;
    uint64_t server_addr;
    uint32_t server_rkey;
};

RDMAConnection::RDMAConnection(const RDMAConfig& config)
    : config_(config), connected_(false), server_mr_addr_(0), server_rkey_(0),
      context_(nullptr), pd_(nullptr), cq_(nullptr), qp_(nullptr), mr_list_(nullptr) {
}

RDMAConnection::~RDMAConnection() {
    // Cleanup RDMA resources
    if (context_) {
        auto impl = static_cast<RDMAConnectionImpl*>(context_);
        if (impl->qp) ibv_destroy_qp(impl->qp);
        if (impl->cq) ibv_destroy_cq(impl->cq);
        if (impl->pd) ibv_dealloc_pd(impl->pd);
        for (auto& p : impl->mr_map) {
            if (p.second) ibv_dereg_mr(p.second);
        }
        if (impl->cm_id) rdma_destroy_id(impl->cm_id);
        if (impl->listen_id) rdma_destroy_id(impl->listen_id);
        delete impl;
    }
}

int RDMAConnection::listen(uint16_t port) {
    struct rdma_addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_port_space = RDMA_PS_TCP;
    hints.ai_flags = RAI_PASSIVE;

    struct rdma_addrinfo* res;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    int ret = rdma_getaddrinfo(nullptr, port_str, &hints, &res);
    if (ret) {
        std::cerr << "rdma_getaddrinfo failed: " << strerror(ret) << std::endl;
        return -1;
    }

    auto impl = new RDMAConnectionImpl();
    impl->is_server = true;

    ret = rdma_create_id(nullptr, &impl->listen_id, nullptr, RDMA_PS_TCP);
    if (ret) {
        std::cerr << "rdma_create_id failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    ret = rdma_bind_addr(impl->listen_id, res->ai_src_addr);
    if (ret) {
        std::cerr << "rdma_bind_addr failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    ret = rdma_listen(impl->listen_id, 1);
    if (ret) {
        std::cerr << "rdma_listen failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    struct rdma_cm_event* event;
    ret = rdma_get_cm_event(impl->listen_id->channel, &event);
    if (ret) {
        std::cerr << "rdma_get_cm_event failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
        std::cerr << "Unexpected CM event: " << event->event << std::endl;
        rdma_ack_cm_event(event);
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    impl->cm_id = event->id;
    rdma_ack_cm_event(event);

    // Setup QP
    struct ibv_qp_init_attr qp_attr = {};
    qp_attr.cap.max_send_wr = config_.max_wr;
    qp_attr.cap.max_recv_wr = config_.max_wr;
    qp_attr.cap.max_send_sge = config_.max_sge;
    qp_attr.cap.max_recv_sge = config_.max_sge;
    qp_attr.qp_type = IBV_QPT_RC;

    if (rdma_create_qp(impl->cm_id, impl->pd, &qp_attr)) {
        std::cerr << "rdma_create_qp failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    impl->qp = impl->cm_id->qp;

    struct rdma_conn_param cm_params = {};
    cm_params.initiator_depth = 1;
    cm_params.responder_resources = 1;

    if (rdma_accept(impl->cm_id, &cm_params)) {
        std::cerr << "rdma_accept failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    ret = rdma_get_cm_event(impl->cm_id->channel, &event);
    if (ret || event->event != RDMA_CM_EVENT_ESTABLISHED) {
        std::cerr << "Connection not established" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }
    rdma_ack_cm_event(event);

    context_ = impl;
    connected_ = true;
    rdma_freeaddrinfo(res);
    return 0;
}

int RDMAConnection::connect(const std::string& server_addr, uint16_t server_port) {
    struct rdma_addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_port_space = RDMA_PS_TCP;

    struct rdma_addrinfo* res;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", server_port);

    int ret = rdma_getaddrinfo(const_cast<char*>(server_addr.c_str()), port_str, &hints, &res);
    if (ret) {
        std::cerr << "rdma_getaddrinfo failed: " << strerror(ret) << std::endl;
        return -1;
    }

    auto impl = new RDMAConnectionImpl();
    impl->is_server = false;

    ret = rdma_create_id(nullptr, &impl->cm_id, nullptr, RDMA_PS_TCP);
    if (ret) {
        std::cerr << "rdma_create_id failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    ret = rdma_resolve_addr(impl->cm_id, nullptr, res->ai_dst_addr, 2000);
    if (ret) {
        std::cerr << "rdma_resolve_addr failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    struct rdma_cm_event* event;
    ret = rdma_get_cm_event(impl->cm_id->channel, &event);
    if (ret || event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
        std::cerr << "Address not resolved" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }
    rdma_ack_cm_event(event);

    ret = rdma_resolve_route(impl->cm_id, 2000);
    if (ret) {
        std::cerr << "rdma_resolve_route failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    ret = rdma_get_cm_event(impl->cm_id->channel, &event);
    if (ret || event->event != RDMA_CM_EVENT_ROUTE_RESOLVED) {
        std::cerr << "Route not resolved" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }
    rdma_ack_cm_event(event);

    // Setup QP
    struct ibv_qp_init_attr qp_attr = {};
    qp_attr.cap.max_send_wr = config_.max_wr;
    qp_attr.cap.max_recv_wr = config_.max_wr;
    qp_attr.cap.max_send_sge = config_.max_sge;
    qp_attr.cap.max_recv_sge = config_.max_sge;
    qp_attr.qp_type = IBV_QPT_RC;

    if (rdma_create_qp(impl->cm_id, impl->pd, &qp_attr)) {
        std::cerr << "rdma_create_qp failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    impl->qp = impl->cm_id->qp;

    struct rdma_conn_param cm_params = {};
    cm_params.initiator_depth = 1;
    cm_params.responder_resources = 1;

    if (rdma_connect(impl->cm_id, &cm_params)) {
        std::cerr << "rdma_connect failed" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }

    ret = rdma_get_cm_event(impl->cm_id->channel, &event);
    if (ret || event->event != RDMA_CM_EVENT_ESTABLISHED) {
        std::cerr << "Connection not established" << std::endl;
        rdma_freeaddrinfo(res);
        delete impl;
        return -1;
    }
    rdma_ack_cm_event(event);

    context_ = impl;
    connected_ = true;
    rdma_freeaddrinfo(res);
    return 0;
}

RDMAMemRegion RDMAConnection::register_memory(void* addr, uint32_t length) {
    if (!context_) {
        return {nullptr, 0, 0, 0};
    }

    auto impl = static_cast<RDMAConnectionImpl*>(context_);
    struct ibv_mr* mr = ibv_reg_mr(impl->pd, addr, length, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC);
    if (!mr) {
        std::cerr << "ibv_reg_mr failed" << std::endl;
        return {nullptr, 0, 0, 0};
    }

    impl->mr_map[mr->lkey] = mr;
    return {addr, length, mr->lkey, mr->rkey};
}

void RDMAConnection::unregister_memory(uint32_t lkey) {
    if (!context_) return;
    auto impl = static_cast<RDMAConnectionImpl*>(context_);
    auto it = impl->mr_map.find(lkey);
    if (it != impl->mr_map.end()) {
        ibv_dereg_mr(it->second);
        impl->mr_map.erase(it);
    }
}

int RDMAConnection::rdma_read(uint64_t remote_addr, uint32_t remote_rkey,
                              void* local_addr, uint32_t local_lkey, uint32_t length,
                              uint64_t wr_id) {
    if (!connected_ || !context_) return -1;

    auto impl = static_cast<RDMAConnectionImpl*>(context_);

    struct ibv_sge sge = {};
    sge.addr = (uintptr_t)local_addr;
    sge.length = length;
    sge.lkey = local_lkey;

    struct ibv_send_wr wr = {};
    wr.wr_id = wr_id;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = remote_rkey;

    struct ibv_send_wr* bad_wr;
    return ibv_post_send(impl->qp, &wr, &bad_wr);
}

int RDMAConnection::rdma_write(uint64_t remote_addr, uint32_t remote_rkey,
                               void* local_addr, uint32_t local_lkey, uint32_t length,
                               uint64_t wr_id) {
    if (!connected_ || !context_) return -1;

    auto impl = static_cast<RDMAConnectionImpl*>(context_);

    struct ibv_sge sge = {};
    sge.addr = (uintptr_t)local_addr;
    sge.length = length;
    sge.lkey = local_lkey;

    struct ibv_send_wr wr = {};
    wr.wr_id = wr_id;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = remote_rkey;

    struct ibv_send_wr* bad_wr;
    return ibv_post_send(impl->qp, &wr, &bad_wr);
}

int RDMAConnection::rdma_compare_swap(uint64_t remote_addr, uint32_t remote_rkey,
                                      uint64_t compare_val, uint64_t swap_val,
                                      uint64_t wr_id) {
    if (!connected_ || !context_) return -1;

    auto impl = static_cast<RDMAConnectionImpl*>(context_);

    struct ibv_sge sge = {};
    sge.addr = (uintptr_t)&swap_val;
    sge.length = sizeof(uint64_t);
    sge.lkey = 0;  // Local lkey (dummy)

    struct ibv_send_wr wr = {};
    wr.wr_id = wr_id;
    wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.wr.atomic.remote_addr = remote_addr;
    wr.wr.atomic.rkey = remote_rkey;
    wr.wr.atomic.compare_add = compare_val;
    wr.wr.atomic.swap = swap_val;

    struct ibv_send_wr* bad_wr;
    return ibv_post_send(impl->qp, &wr, &bad_wr);
}

int RDMAConnection::poll_cq(int max_entries) {
    if (!context_) return -1;
    auto impl = static_cast<RDMAConnectionImpl*>(context_);

    struct ibv_wc* wc = new struct ibv_wc[max_entries];
    int ne = ibv_poll_cq(impl->cq, max_entries, wc);
    delete[] wc;
    return ne;
}

int RDMAConnection::get_completions(std::vector<Completion>& completions, int max_count) {
    if (!context_) return -1;
    auto impl = static_cast<RDMAConnectionImpl*>(context_);

    struct ibv_wc* wc = new struct ibv_wc[max_count];
    int ne = ibv_poll_cq(impl->cq, max_count, wc);

    for (int i = 0; i < ne; i++) {
        completions.push_back({wc[i].wr_id, (wc[i].status == IBV_WC_SUCCESS) ? 0 : -1});
    }

    delete[] wc;
    return ne;
}

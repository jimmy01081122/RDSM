# Phase 3 Soft-RoCE Validation Summary

This summary validates Soft-RoCE verbs transport functionality only. It does not claim hardware RDMA latency, hardware RDMA bandwidth, RNIC offload behavior, PCIe behavior, switch behavior, or bare-metal cluster scalability.

## Nodes

| Role | Hostname | IP | vCPU | Kernel | rxe0 | ibv summary |
|---|---|---:|---:|---|---|---|
| Client | node2 | 192.168.56.102 | 4 | 5.15.0-179-generic | ACTIVE/LINK_UP on enp0s8 | rxe0 present; active_mtu=1024 (3); link_layer=Ethernet |
| Server | node1 | 192.168.56.101 | 4 | 5.15.0-179-generic | ACTIVE/LINK_UP on enp0s8 | rxe0 present; active_mtu=1024 (3); link_layer=Ethernet |

## Validation Status

| Layer | Operation | Status | Rows | Success | Failure | Notes |
|---|---|---|---:|---:|---:|---|
| external_verbs_tool | ibv_rc_pingpong | success | 2 | 2 | 0 | parsed from Phase 3/3a perftest outputs |
| external_verbs_tool | ib_write_bw | success | 7 | 7 | 0 | parsed from Phase 3/3a perftest outputs |
| external_verbs_tool | ib_read_bw | success | 2 | 2 | 0 | parsed from Phase 3/3a perftest outputs |
| external_verbs_tool | ib_write_lat | success | 7 | 7 | 0 | parsed from Phase 3/3a perftest outputs |
| external_verbs_tool | ib_read_lat | success | 7 | 7 | 0 | parsed from Phase 3/3a perftest outputs |
| external_verbs_tool | ib_send_lat | success | 7 | 7 | 0 | parsed from Phase 3/3a perftest outputs |
| project_two_node_rdma_validation | RDMA_WRITE | deferred | 0 | 0 | 0 | deferred: existing RDMA wrapper needs connection/MR/CQ setup work and should not block Phase 5 |
| project_two_node_rdma_validation | RDMA_READ | deferred | 0 | 0 | 0 | deferred: existing RDMA wrapper needs connection/MR/CQ setup work and should not block Phase 5 |
| project_two_node_rdma_validation | RDMA_CAS | deferred | 0 | 0 | 0 | deferred: existing RDMA wrapper needs connection/MR/CQ setup work and should not block Phase 5 |

## Interpretation

- `ibv_rc_pingpong`, `ib_write_bw`, `ib_read_bw`, `ib_write_lat`, `ib_read_lat`, and `ib_send_lat` have successful two-node Soft-RoCE rows in the current artifacts.
- Project-level `two_node_rdma_validation` is deferred because the existing RDMA wrapper needs non-trivial connection/MR/CQ setup work; this should not block Phase 5 latency sampling.
- The observed latency/bandwidth values remain transport diagnostics, not DSM transaction performance evidence.

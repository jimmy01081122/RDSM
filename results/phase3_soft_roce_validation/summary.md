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
| external_verbs_tool | ibv_rc_pingpong | not_collected | 0 | 0 | 0 | not present in current Phase 3 artifacts |
| external_verbs_tool | ib_write_bw | success | 7 | 7 | 0 | parsed from Phase 3 rerun perftest outputs |
| external_verbs_tool | ib_read_bw | not_collected | 0 | 0 | 0 | not present in current Phase 3 artifacts |
| external_verbs_tool | ib_write_lat | success | 7 | 7 | 0 | parsed from Phase 3 rerun perftest outputs |
| external_verbs_tool | ib_read_lat | success | 7 | 7 | 0 | parsed from Phase 3 rerun perftest outputs |
| external_verbs_tool | ib_send_lat | success | 7 | 7 | 0 | parsed from Phase 3 rerun perftest outputs |
| project_two_node_rdma_validation | RDMA_WRITE | not_implemented | 0 | 0 | 0 | two_node_rdma_validation executable is not present yet; no project wrapper READ/WRITE/CAS result exists |
| project_two_node_rdma_validation | RDMA_READ | not_implemented | 0 | 0 | 0 | two_node_rdma_validation executable is not present yet; no project wrapper READ/WRITE/CAS result exists |
| project_two_node_rdma_validation | RDMA_CAS | not_implemented | 0 | 0 | 0 | two_node_rdma_validation executable is not present yet; no project wrapper READ/WRITE/CAS result exists |

## Interpretation

- `ib_write_bw`, `ib_write_lat`, `ib_read_lat`, and `ib_send_lat` completed successfully across node2 -> node1 in the Phase 3 rerun.
- `ibv_rc_pingpong` and `ib_read_bw` are not present in the current artifacts and should be collected if Layer 1 coverage must be complete.
- The project-specific `two_node_rdma_validation` executable is still future work; therefore READ/WRITE/CAS wrapper success counts are intentionally reported as not implemented, not silently assumed.
- The observed latency jitter remains a transport diagnostic, not DSM transaction latency evidence.

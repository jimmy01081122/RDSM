# Phase 3 Two-Node Soft-RoCE Validation Report

## Scope

This phase validates the two-node Soft-RoCE verbs transport path between node2 (client, 192.168.56.102) and node1 (server, 192.168.56.101). It is a transport sanity check and calibration step, not a hardware RDMA performance claim and not an end-to-end distributed DSM benchmark.

The validation uses perftest RC queue-pair tests over `rxe0`: RDMA WRITE latency, RDMA READ latency, SEND latency, and RDMA WRITE bandwidth. Phase 1 legacy `/stat` results are parsed beside the new Phase 3 rerun so the report can compare historical single-shot measurements with the reproducible sweep.

## Artifacts

- Summary CSV: `results/phase3/two_node_soft_roce_summary.csv`
- Parsed successful rows: 32
- Phase 3 rerun rows: 28
- Phase 1 `/stat` rows: 4
- Failed or unparsable rows: 0

## Transport Evidence

Successful rows expose RC transport metadata: `Transport type: IB`, `Connection type: RC`, `Link type: Ethernet`, GID index, local/remote GID, and local/remote QPN. In this environment the GIDs encode the host-only network addresses, which confirms that the observed path is node2 -> node1 over Soft-RoCE rather than an in-process benchmark.

## Phase 3 Rerun Summary

| Test | Rows | Size range | Mean latency us | Mean p99 us | Mean BW MB/s | Mean MsgRate Mpps |
|---|---:|---:|---:|---:|---:|---:|
| ib_read_lat | 7 | 8-65536 | 6919.06 | 139694.39 | 0.00 | 0.000000 |
| ib_send_lat | 7 | 8-65536 | 822.27 | 15829.41 | 0.00 | 0.000000 |
| ib_write_bw | 7 | 8-65536 | 0.00 | 0.00 | 19.00 | 0.020198 |
| ib_write_lat | 7 | 8-65536 | 2666.67 | 87591.44 | 0.00 | 0.000000 |

## Legacy `/stat` Summary

| Test | Payload bytes | Avg latency us | p99 us | p999 us | Avg BW MB/s | MsgRate Mpps |
|---|---:|---:|---:|---:|---:|---:|
| ib_read_lat | 2 | 302.54 | 485.69 | 939.14 | 0.00 | 0.000000 |
| ib_send_lat | 2 | 404.16 | 3760.42 | 6491.64 | 0.00 | 0.000000 |
| ib_write_bw | 65536 | 0.00 | 0.00 | 0.00 | 36.39 | 0.000582 |
| ib_write_lat | 2 | 455.15 | 2899.51 | 24915.93 | 0.00 | 0.000000 |

## Interpretation

- The two-node validation is useful because it proves the Soft-RoCE stack, RC QP setup, address exchange, and CQ completion path are operational across two VMs.
- The measurements should not be used as hardware RDMA latency/bandwidth numbers. They include VirtualBox networking, Linux RXE software processing, guest scheduling, and host-only Ethernet effects.
- These results are intentionally kept separate from Phase 2 local DSM/OCC throughput. Phase 2 compares contention-control protocol trends; Phase 3 validates that the transport substrate exists and gives a calibration boundary.
- The legacy `/stat` latency rows use a 2-byte default payload and 1000 iterations, while the Phase 3 rerun sweeps explicit payload sizes. They are comparable as environment evidence, not as a stable latency distribution.

## Review-Grade Limitations

- This phase does not run the DSM/OCC benchmark over remote verbs; it only validates standalone verbs-level transport.
- No CAS/atomic perftest row is included yet, so the validation covers READ/WRITE/SEND but not remote atomic behavior.
- Soft-RoCE in VirtualBox has high jitter; tail values should be treated as diagnostic noise unless repetitions, pinning, warm-up, and host-load control are added.
- Latency-related DSM claims remain intentionally out of scope for the current project phase.

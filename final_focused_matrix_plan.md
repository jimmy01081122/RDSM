# Final Focused Matrix Plan

This plan intentionally avoids a full factorial benchmark. It targets the main research questions while keeping runtime manageable.

## Main-Text Thread Counts

Use only:

```text
1, 2, 4
```

The current environment exposes 4 vCPU/core contexts. Threads 8 and 16 should be appendix-only oversubscription stress.

## Final Run Policy

```text
warmup = 10s
measurement = 60s
repetitions = 10
```

Report mean, standard deviation, 95% confidence interval, repetition count, warmup duration, and measurement duration.

## Main-Text Workloads

```text
low_uniform_read95
mixed_uniform_write20
mixed_hot4_write50
high_hot1_write100
high_hot16_write100
zipf99_write100
flash_sale_spike
ticket_booking_hot_event
ad_budget_read_heavy_dashboard
long_tail_marketplace_zipf
```

## Algorithms / Modes

Main algorithms:

```text
baseline_occ
backoff_occ
occ_with_hot_detection_monitoring
hybrid_static_arbitration_occ
```

Phase 4 arbitration comparison for hybrid:

```text
global
per_object
per_shard hot_shards=4
per_shard hot_shards=8
per_shard hot_shards=16
```

Adaptive routing should be added only after Phase 5 latency sampling and queue-cost calibration are implemented.

## Non-Claims

Do not merge local protocol evidence with two-node Soft-RoCE transport validation into a single performance claim. Do not claim hardware RDMA performance.

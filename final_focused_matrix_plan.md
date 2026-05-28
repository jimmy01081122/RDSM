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
duration_sec = 10s
repetitions = 3
latency_sampling = reservoir
latency_sample_size = 10000
adaptive default = routing_margin_us=5, cost_window_ms=500, min_samples_before_adapt=100, adaptive_object_scope=shard, hot_shards=8
```

Report mean, standard deviation, 95% confidence interval, repetition count, warmup duration, and measurement duration.

## Main-Text Synthetic Focused Workloads

```text
low_uniform_read95
mixed_uniform_write20
mixed_hot4_write50
high_hot1_write100
high_hot16_write100
zipf99_write100
```

These workloads answer mechanism questions about contention shape, queueing, hot-path routing, and OCC conflict symptoms.

## Main-Text Application-Like Workloads

```text
flash_sale_spike
ticket_booking_hot_event
ad_budget_read_heavy_dashboard
long_tail_marketplace_zipf
```

These workloads should be reported in a separate figure/table from synthetic workloads. Do not create a combined universal ranking table.

## Algorithms / Modes

Main algorithms:

```text
baseline_occ
backoff_occ
hybrid_static_arbitration_occ_global
hybrid_static_arbitration_occ_per_object
hybrid_static_arbitration_occ_per_shard_8
hybrid_adaptive_arbitration_occ_per_shard_8
```

Sanity/appendix only unless needed for overhead discussion:

```text
occ_with_hot_detection_monitoring
```

Sold counter isolation:

```text
sold_counter_mode=global       # global metadata bottleneck
sold_counter_mode=per_product  # isolates arbitration queue behavior
```

Use `per_product` for arbitration-isolation plots and `global` when discussing application-level global metadata bottlenecks.

For the main final matrix, use `sold_counter_mode=per_product`. Run `sold_counter_mode=global` only as a controlled bottleneck comparison for:

```text
mixed_hot4_write50
high_hot16_write100
```

## Appendix Metadata

All runner manifests and summaries should include:

```text
appendix_only
appendix_reason
```

Rows with `threads > 4` should set:

```text
appendix_only=true
appendix_reason=oversubscription_threads_exceed_exposed_cores
```

Main plotting scripts should default to `appendix_only == false`. Appendix plots may include oversubscription rows.

Adaptive routing should be added only after Phase 5 latency sampling and queue-cost calibration are implemented.

## Non-Claims

Do not merge local protocol evidence with two-node Soft-RoCE transport validation into a single performance claim. Do not claim hardware RDMA performance.

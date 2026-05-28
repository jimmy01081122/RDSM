# Phase 5 Adaptive Routing Design Note

Date: 2026-05-28 UTC

## 1. Problem Statement

Static hybrid arbitration routes known-hot transactions to a serialized arbitration path. This can reduce OCC retry storms under severe hot-object contention, but it can also add queue wait when OCC would have succeeded cheaply. The adaptive routing problem is to decide, per candidate transaction, whether the expected OCC retry cost is higher than the expected arbitration queue cost.

## 2. Routing Objective

Route a transaction to arbitration only when predicted OCC cost exceeds predicted arbitration cost by a configurable margin. The goal is not to maximize a single smoke-run throughput number; the goal is to expose a controllable mechanism for studying retry cost versus queueing cost.

## 3. Cost Model

Initial model:

```text
estimated_occ_cost_us =
    ewma_occ_latency_us + ewma_retry_per_tx * retry_penalty_us

retry_penalty_us =
    max(1, ewma_occ_latency_us)

estimated_arbitration_cost_us =
    ewma_queue_wait_us + ewma_service_time_us

route_to_arbitration if:
    estimated_occ_cost_us > estimated_arbitration_cost_us + routing_margin_us
```

The first prototype keeps this model intentionally small. It should be treated as a calibration target, not a mature routing policy.

## 4. Per-Object/Per-Shard State

The prototype maintains adaptive state at a configurable scope:

```text
--adaptive-object-scope=global|shard|object
```

Each scope maintains:

- EWMA OCC latency
- EWMA retry-per-transaction
- EWMA arbitration queue wait
- EWMA arbitration service time
- sample count
- previous route decision for oscillation tracking

The default scope is `shard`, matching the scalable arbitration queue design.

## 5. EWMA Update Rule

The initial prototype uses:

```text
new_value = (1 - alpha) * old_value + alpha * observed_value
alpha = 0.20
```

`--cost-window-ms` is kept as a CLI parameter for experiment metadata and future time-windowed calibration. The current minimal prototype does not yet implement a strict wall-clock sliding window.

## 6. Cold-Start Behavior

When a scope has fewer than:

```text
--min-samples-before-adapt
```

samples, known-hot transactions fall back to the static hybrid rule and route to arbitration. The run increments:

```text
adaptive_insufficient_samples_count
```

This gives the router early arbitration observations instead of making arbitrary OCC decisions without data.

## 7. Insufficient-Sample Fallback

Fallback is conservative:

- known-hot transaction: static hybrid arbitration
- non-hot transaction: OCC

The fallback is explicitly counted and should be reported separately from mature adaptive decisions.

## 8. Routing Margin

The routing margin avoids switching to arbitration for tiny estimated gains:

```text
--routing-margin-us=10
```

Calibration should later sweep 5, 10, and 20 microseconds.

## 9. Hysteresis / Oscillation Control

The minimal prototype records route flips as:

```text
oscillation_count
```

It does not yet enforce a hold-down interval. If oscillation is high in smoke or calibration, future work should add per-scope minimum decision hold time or separate enter/exit margins.

## 10. Failure Modes

Known failure modes:

- bad initial fallback can overuse arbitration
- aggregate observations can hide per-object differences
- queue-cost estimates can lag workload changes
- reservoir latency sampling overhead can influence observed costs
- scripted phase-change approximation restarts the process and cannot show continuous in-process adaptation

## 11. Required Metrics

The benchmark reports:

```text
adaptive_route_to_occ_count
adaptive_route_to_arbitration_count
adaptive_route_to_occ_ratio
adaptive_route_to_arbitration_ratio
adaptive_insufficient_samples_count
adaptive_bad_route_proxy_count
estimated_occ_cost_us_p50/p95/p99
estimated_arbitration_cost_us_p50/p95/p99
routing_decision_latency_us_p50/p95/p99
oscillation_count
```

`adaptive_bad_route_proxy_count` is a conservative proxy. It is not a true counterfactual misprediction metric.

## 12. Smoke-Test Plan

Smoke runs only:

```text
duration-sec=1
threads=2
latency-sampling=reservoir
latency-sample-size=5000
```

Workloads:

- `low_uniform_read95`
- `mixed_hot4_write50`
- `high_hot16_write100`

Pass conditions:

- invariant violations are zero
- duplicate commits are zero
- adaptive metrics are present
- latency fields are present
- no OOM
- no timeout

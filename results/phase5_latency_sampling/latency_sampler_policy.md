# Phase 5 Latency Sampler Policy

Date: 2026-05-28 UTC

## Scope

The Phase 5 latency sampler records prototype-relative DSM transaction latency. These samples are useful for comparing routing policies under identical local conditions, but they are not hardware RDMA latency measurements.

## Default Sampling Policy

The default latency mode remains bounded reservoir sampling:

```text
--latency-sampling=reservoir
--latency-sample-size=10000
```

The default sample size was reduced from 100,000 to 10,000 after the initial smoke check showed measurable instrumentation overhead and after a previous codex3 run hit OOM risk. A 10,000-sample default is a conservative VM-safe setting for p50/p95/p99 prototype analysis. Larger sample sizes remain available as explicit user choices.

## Full Sampling Policy

Full sampling is debug-only. It stores every sampled transaction and can create hundreds of MB of CSV output within one second in this VM environment.

The benchmark now rejects full sampling unless:

```text
duration_sec <= 2
threads <= 2
```

Longer or wider full-sampling runs require the explicit override:

```text
--allow-dangerous-full-sampling
```

Full sampling is not allowed for final matrix runs.

## Smoke Overhead Finding

The current overhead check is smoke-level only: 1-second, 2-thread runs over `low_uniform_read95`, `mixed_hot4_write50`, and `high_hot16_write100`.

Observed result:

- Reservoir sampling is bounded but still has measurable overhead.
- Full sampling reduced tx/sec by roughly 31-47% in the smoke rows.
- Full sampling reached hundreds of MB RSS within one second.

Therefore, final latency analysis should use reservoir sampling and report measurement overhead. Throughput-primary runs and latency-analysis runs should remain clearly labeled.

## Reporting Rule

Latency results must state:

- sampling mode
- sample size
- whether results are smoke, calibration, or final matrix
- that values are prototype-relative, not hardware RDMA latency

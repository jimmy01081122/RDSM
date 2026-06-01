# RDSM Next Implementation



## Goal

Improve measurement correctness and regression coverage before running another performance matrix. Do not add a distributed transport simulation and do not rewrite the architecture.

## Current State

The previous pass already fixed phantom-lock rollback, atomic latency histogram buckets, synthetic p95/p99 output, Zipfian distribution rebuilding, legacy arbitrator abort double-counting, `object_count_` atomicity, and ineffective in-mutex read spinning.

Two instrumented counters have deliberately limited meaning:

- `duplicate_commit_count` detects a repeated application of the same `tx_id` to an object. It is not a complete exactly-once proof.
- `hot_cold_interference_count` is currently a hot-candidate-to-OCC routing proxy. It is not an observed object-conflict detector.

Preserve those limitations in documentation.

## Required Task 1: Repair Transaction Counter Semantics

Files:

- `src/dsm_object.h`
- `src/occ_engine.cpp`
- `experiments/phase2_benchmark.h`
- `experiments/phase2_dsm_benchmark.cpp`
- relevant documentation and result parser scripts, if present

Problem:

- `attempted_tx` mixes OCC retry attempts with logical transactions.
- `commit_transaction()` increments `aborted_tx` for each failed OCC attempt.
- `run_occ_order()` increments `aborted_tx` again when retries are exhausted.
- `abort_rate` therefore does not have a clean logical-transaction denominator.

Implement explicit counters:

- `logical_tx`: increment exactly once for each generated order before routing.
- `occ_attempts`: increment exactly once for each OCC attempt.
- `occ_failed_attempts`: increment once for each failed OCC commit attempt.
- `final_abort_tx`: increment once only when a logical transaction ends in a non-business failure.
- Keep `business_abort_tx` separate.

Compute:

```text
abort_rate = (final_abort_tx + business_abort_tx) / logical_tx
```

Compatibility:

- Preserve existing JSON keys where practical, but document their new meaning.
- Emit the new counters as JSON fields.
- Update reports and parsers so comparisons with historical rows are explicitly marked as schema-sensitive.
- Do not silently compare old mixed-denominator `abort_rate` rows with new rows.

Acceptance:

- For each completed smoke run:

```text
logical_tx == committed_tx + final_abort_tx + business_abort_tx
occ_attempts >= occ_failed_attempts
occ_attempts == committed OCC-path transactions + occ_failed_attempts
```

- The invariants must be explained in comments or documentation.

## Required Task 2: Add Deterministic Regression Tests

Add a small CTest-backed regression executable. Use minimal test-only access where needed; do not make production internals public merely for testing.

Cover:

1. Partial OCC lock acquisition rollback: force lock acquisition to fail after at least one object was acquired and assert that earlier owned lock bits are cleared.
2. Duplicate-application detector: intentionally apply the same transaction ID twice and assert that `duplicate_commit_count` increments.
3. Counter semantics: run a deterministic OCC scenario with retry exhaustion and assert the Task 1 counter relationships.
4. Legacy `ServerArbitrator`: submit a missing-object request and assert that its abort counter increments once.
5. Latency JSON compatibility: assert that `latency_us_p50/p95/p99` and `tx_latency_us_p50/p95/p99` are sampler-backed and equal.

Acceptance:

```bash
cd /home/node1/RDSM/build
cmake ..
make -j"$(nproc)"
ctest --output-on-failure
```

must pass with compiler warnings visible and no new warnings.

## Required Task 3: Make Sampling Names Explicit

The current CLI value `reservoir` is a mutex-protected bounded rotating buffer with recency bias. Keep it as a backward-compatible alias, but expose an honest canonical name such as `bounded_rotation`.

Update:

- enum names where reasonable
- CLI parsing and help text
- JSON metadata
- README, HANDOFF, and paper caveats

Optional, only after the rename is complete:

- Add a separate `uniform_reservoir` mode implementing Algorithm R.
- Keep deterministic tests for buffer bounds and mode parsing.
- Do not relabel historical bounded-rotation results as uniform reservoir samples.

## Required Task 4: Quarantine the Dormant RDMA CAS API

Files:

- `src/rdma_conn.h`
- `src/rdma_conn.cpp`

The dormant `rdma_compare_swap()` currently posts a stack address with dummy `lkey=0` as a DMA destination. Fix the API so callers must provide a registered local result buffer and its valid `lkey`, matching the read/write API style.

Also explicitly delete copy construction and copy assignment for `RDMAConnection` to prevent accidental double-free ownership bugs. Add move support only if an actual caller needs it.

Do not claim hardware validation. Build verification is sufficient in the current environment.

## Deferred Research Task

After Tasks 1-4 pass, redesign adaptive routing updates so each object or shard state learns from route-specific observations rather than global aggregate counters. Keep this as a separate change because it affects experimental interpretation and requires a fresh focused matrix.

## Verification Commands

```bash
cd /home/node1/RDSM/build
cmake ..
make -j"$(nproc)"
ctest --output-on-failure

for algo in baseline_occ backoff_occ hot_detection_occ hybrid_arbitration_occ hybrid_adaptive_arbitration_occ; do
  ./phase2_dsm_benchmark \
    --products 4 --users 10 --threads 2 --duration-sec 3 \
    --algorithm "$algo" --hot-products 1 --hot-access-prob 0.9 \
    --arbitration-mode per_object --write-ratio 1.0
done
```

For every smoke output, verify:

- `invariant_violation_count == 0`
- `duplicate_commit_count == 0` unless a detector test intentionally triggers it
- `logical_tx == committed_tx + final_abort_tx + business_abort_tx`
- compatibility latency percentile keys equal sampler-backed keys

## Claim Boundary

Describe results as local shared-memory prototype evidence for concurrency-control strategy behavior. Do not describe benchmark throughput or latency as RDMA NIC, network, PCIe, switch, or distributed DSM measurements.


# Final Audit Bug Confirmation Report and Patch Plan

Date: 2026-05-28 UTC

`APPLY_CODE_FIXES=1` as of 2026-06-01 UTC. This report verifies the audit findings and records patch plans. The authorized code-fix pass applied the scoped repairs recorded below. Documentation-safe fixes remain in place, especially to avoid claiming statistically uniform reservoir sampling.

## Summary

| Finding | Status | Severity | Action in this pass |
|---|---|---|---|
| `duplicate_commit_count` / `hot_cold_interference_count` dead counters | Fixed 2026-06-01 | High | Minimal detectors instrumented |
| `attempted_tx` counts OCC retries rather than logical transactions | Fixed 2026-06-01 | High | Counter schema version 2 separates logical outcomes and OCC attempts |
| `reservoir` sampling is bounded rotating replacement, not Algorithm R | Fixed 2026-06-01 | High | Canonical `bounded_rotation` name added; historical alias retained |
| OCC lock acquisition failure can leave earlier lock bits set | Fixed 2026-06-01 | High | Partial acquisitions rolled back |
| Legacy fake p95/p99 latency fields | Fixed 2026-06-01 | Medium | Compatibility keys now use sampler percentiles |
| Zipfian distribution rebuilt per order | Fixed 2026-06-01 | Low/Medium | Per-thread distribution cache added |
| `server_arbitration.cpp` double-counts failed arbitrated aborts | Fixed 2026-06-01 | Medium | Abort counted once; counters made atomic |
| `parse_phase3_results.py` hardcodes `RESULTS_DIR` | Confirmed | Low | Patch plan only |

## Confirmed Findings

### 1. Dead Correctness Counters

`duplicate_commit_count` and `hot_cold_interference_count` are defined in `src/dsm_object.h` and emitted by `experiments/phase2_dsm_benchmark.cpp`, but code search found no increment sites. Current result files therefore showing zero for these fields should be read as "no implemented detector fired", not as complete duplicate/interference proof.

Patch plan:

- Implement duplicate-write/duplicate-commit detection in the object store or commit path using transaction IDs and per-object `last_writer_tx_id`.
- Define an explicit hot/cold interference condition and increment it only when a cold OCC transaction and hot arbitration path conflict under the intended invariant.
- Add smoke tests that intentionally trigger each counter.

Resolution applied 2026-06-01:

- `apply_write_set()` now increments `duplicate_commit_count` when the same transaction ID is applied to an object twice.
- Adaptive routing now increments `hot_cold_interference_count` when a known-hot candidate is routed through OCC instead of arbitration. This is a route-level interference proxy, not a proof of an object-level conflict.

### 2. `attempted_tx` Retry Semantics

`OCCEngine::begin_transaction()` increments `attempted_tx` on every OCC retry attempt (`src/occ_engine.cpp:39-50`). `run_occ_order()` calls `begin_transaction()` inside the retry loop (`experiments/phase2_dsm_benchmark.cpp:523-526`). The hot arbitration path increments `attempted_tx` once per logical transaction (`experiments/phase2_dsm_benchmark.cpp:400-403`). Therefore `attempted_tx` mixes logical transactions and retry attempts depending on path, and `abort_rate` in `get_result()` uses that mixed denominator (`experiments/phase2_dsm_benchmark.cpp:651-656`).

Patch plan:

- Split counters into `logical_tx`, `occ_attempts`, and `retry_count`.
- Compute final abort rate as final aborts divided by logical transactions.
- Keep OCC attempt/retry metrics separately for contention analysis.

Resolution applied 2026-06-01:

- Counter schema version 2 emits `logical_tx`, `occ_attempts`, `occ_failed_attempts`, and `final_abort_tx`.
- Compatibility keys remain: `attempted_tx == logical_tx` and `aborted_tx == final_abort_tx`.
- `abort_rate` now uses `(final_abort_tx + business_abort_tx) / logical_tx`.
- Historical mixed-denominator rows must not be silently compared with schema-version-2 rows.

### 3. Bounded Rotating Sample Misnamed as Reservoir

`LatencySampler::record()` replaces `samples_[index % sample_size_]` after the sample buffer is full (`src/latency_sampler.cpp:65-82`). This is bounded rotating replacement, not statistically uniform Algorithm R reservoir sampling.

Documentation-safe action applied:

- `paper.md`, `README.md`, `HANDOFF.md`, `PROJECT_PLAN_STATUS.md`, sold-counter metadata, and convergence summary now describe this as bounded rotating sampling and avoid unbiased-reservoir claims.

Patch options:

- Implement Algorithm R using a random index in `[0, seen]` after the buffer is full.
- Or rename CLI/reporting mode from `reservoir` to `bounded_rotating_sample` while keeping backward compatibility.

Resolution applied 2026-06-01:

- `bounded_rotation` is now the canonical CLI and JSON metadata name.
- The backward-compatible CLI alias `reservoir` remains accepted.
- Enum and implementation comments explicitly document deterministic bounded rotation, modulo replacement, and recency bias.

### 4. OCC Lock Acquisition Can Leave Phantom Locks

`try_acquire_locks()` sets lock bits as it iterates through the write set (`src/occ_engine.cpp:93-124`). If a later object cannot be locked, it returns `-1`. `commit_transaction()` returns immediately on that failure (`src/occ_engine.cpp:198-201`) without calling `release_locks(tx)`. Earlier acquired lock bits can therefore remain set until overwritten/reset by other logic, causing phantom lock failures.

Patch plan:

- Track acquired object IDs during `try_acquire_locks()`.
- On any failure, release only locks owned by the current transaction before returning.
- Add a unit/smoke test with a multi-object transaction where the second lock fails.

Resolution applied 2026-06-01:

- `try_acquire_locks()` now tracks acquired IDs and rolls back locks owned by the current transaction on either failure path.
- Lock-acquisition failures now increment `occ_failed_attempts`. A logical transaction increments `final_abort_tx` only after retry exhaustion.

### 5. Legacy Fake p95/p99 Latency Fields

`RunResult::get_result()` computes `latency_p95_us = latency_p50_us * 1.5` and `latency_p99_us = latency_p50_us * 2.0` (`experiments/phase2_dsm_benchmark.cpp:660-663`). These are legacy estimated fields and must not be used as final latency evidence.

Current interpretation:

- Final analysis should use `tx_latency_us_p50/p95/p99` and committed/abort/path-specific sampled latency fields.
- Legacy `latency_us_p95/p99` may remain in CSVs for backward compatibility but should be labeled as legacy estimates.

Patch plan:

- Rename these fields to `legacy_latency_estimate_*` or omit them from final summaries.
- Keep sampled transaction latency fields as the only p95/p99 evidence.

Resolution applied 2026-06-01:

- Legacy synthetic fields were removed from `RunResult`.
- Backward-compatible `latency_us_p50/p95/p99` JSON keys now emit the real `LatencySampler` transaction percentiles. Existing `tx_latency_us_p50/p95/p99` keys remain available.

### 6. Zipfian Distribution Rebuilt per Order

`InventoryWorkload::generate_order()` rebuilds the Zipfian weight vector and `std::discrete_distribution` on each generated order (`experiments/phase2_dsm_benchmark.cpp:178-185`). This is a performance overhead that can distort high-rate skewed workloads.

Patch plan:

- Precompute Zipfian distributions in `InventoryWorkload` construction based on `num_products` and theta.
- Keep per-thread RNG state but reuse the distribution object.
- Re-run a small Zipfian smoke to ensure trend stability.

Resolution applied 2026-06-01:

- Zipfian distributions are cached per thread and rebuilt only when theta or product count changes.

### 7. Server Arbitration Abort Double Count

`ServerArbitrator::process_request()` increments `arbitrated_aborts_` when an object is missing (`src/server_arbitration.cpp:120-124`) and then increments it again in the final `else` branch (`src/server_arbitration.cpp:133-137`). This double-counts failed requests in that legacy arbitrator component.

Patch plan:

- Remove the inner increment and count aborts once in the final status branch.
- Add a direct missing-object test for the arbitrator.

Resolution applied 2026-06-01:

- The inner abort increment was removed and arbitrator commit/abort counters are atomic.

## Additional Repairs Applied 2026-06-01

- Changed the shared latency histogram to fixed-size atomic buckets.
- Changed `object_count_` to an atomic counter for concurrent reads.
- Removed the ineffective `read_object()` spin loop while retaining the lock-bit retry check.
- Quarantined dormant RDMA CAS posting: callers must provide a registered local result buffer and `lkey`, and `RDMAConnection` copying is disabled. This is build-verified only; no hardware CAS validation is claimed.
- Added CTest-backed deterministic regression coverage for phantom-lock rollback, duplicate application detection, retry-exhaustion counters, legacy arbitrator abort counting, bounded-rotation parsing/bounds, and sampler-backed JSON percentile compatibility.

### 8. `parse_phase3_results.py` Hardcoded Path

`scripts/parse_phase3_results.py` sets `RESULTS_DIR = Path("./results/phase3")` and `LEGACY_STAT_DIR = Path("./stat")` (`scripts/parse_phase3_results.py:11-12`) without environment overrides. This is not a correctness bug in current outputs, but it reduces reproducibility when parsing alternate result roots.

Patch plan:

- Change to `Path(os.environ.get("RESULTS_DIR", "./results/phase3"))` and `Path(os.environ.get("LEGACY_STAT_DIR", "./stat"))`.
- Add a syntax check and parse smoke using a temporary result directory.

## Current Artifact Verification

- `results/final_focused_matrix/summary.csv`: 540 data rows.
- `results/final_focused_matrix/summary_by_config.csv`: present.
- `results/final_focused_matrix/final_summary.md`: present.
- `results/final_focused_matrix/statistical_report.md`: present.
- `results/final_focused_matrix/run_metadata.json`: present.
- `results/final_sold_counter_comparison/summary.csv`: 48 data rows.
- `results/final_sold_counter_comparison/summary_by_config.csv`: 16 grouped rows including `sold_counter_mode`.
- `results/final_sold_counter_comparison/sold_counter_comparison_summary.md`: present.
- `results/final_sold_counter_comparison/run_metadata.json`: present.

## Recommendation

For paper claims, use invariant checks, final stock/sold consistency, sampled transaction latency fields, and per-workload trend analysis. The 2026-06-01 fixes activate scoped duplicate/interference instrumentation, replace legacy fake latency percentiles, and introduce logical-transaction counter schema version 2. Historical abort-rate rows remain schema-sensitive. Do not treat bounded `bounded_rotation` samples, including historical `reservoir` alias rows, as unbiased Algorithm R evidence.

# Final Audit Bug Confirmation Report and Patch Plan

Date: 2026-05-28 UTC

`APPLY_CODE_FIXES=0` for this consolidation. This report verifies the audit findings and records patch plans. No invasive code fixes were applied in this pass. Documentation-safe fixes were applied where needed, especially to avoid claiming statistically uniform reservoir sampling.

## Summary

| Finding | Status | Severity | Action in this pass |
|---|---|---|---|
| `duplicate_commit_count` / `hot_cold_interference_count` dead counters | Confirmed | High | Patch plan only |
| `attempted_tx` counts OCC retries rather than logical transactions | Confirmed | High | Patch plan only |
| `reservoir` sampling is bounded rotating replacement, not Algorithm R | Confirmed | High | Documentation corrected |
| OCC lock acquisition failure can leave earlier lock bits set | Confirmed | High | Patch plan only |
| Legacy fake p95/p99 latency fields | Confirmed | Medium | Documentation/result interpretation corrected |
| Zipfian distribution rebuilt per order | Confirmed | Low/Medium | Patch plan only |
| `server_arbitration.cpp` double-counts failed arbitrated aborts | Confirmed | Medium | Patch plan only |
| `parse_phase3_results.py` hardcodes `RESULTS_DIR` | Confirmed | Low | Patch plan only |

## Confirmed Findings

### 1. Dead Correctness Counters

`duplicate_commit_count` and `hot_cold_interference_count` are defined in `src/dsm_object.h` and emitted by `experiments/phase2_dsm_benchmark.cpp`, but code search found no increment sites. Current result files therefore showing zero for these fields should be read as "no implemented detector fired", not as complete duplicate/interference proof.

Patch plan:

- Implement duplicate-write/duplicate-commit detection in the object store or commit path using transaction IDs and per-object `last_writer_tx_id`.
- Define an explicit hot/cold interference condition and increment it only when a cold OCC transaction and hot arbitration path conflict under the intended invariant.
- Add smoke tests that intentionally trigger each counter.

### 2. `attempted_tx` Retry Semantics

`OCCEngine::begin_transaction()` increments `attempted_tx` on every OCC retry attempt (`src/occ_engine.cpp:39-50`). `run_occ_order()` calls `begin_transaction()` inside the retry loop (`experiments/phase2_dsm_benchmark.cpp:523-526`). The hot arbitration path increments `attempted_tx` once per logical transaction (`experiments/phase2_dsm_benchmark.cpp:400-403`). Therefore `attempted_tx` mixes logical transactions and retry attempts depending on path, and `abort_rate` in `get_result()` uses that mixed denominator (`experiments/phase2_dsm_benchmark.cpp:651-656`).

Patch plan:

- Split counters into `logical_tx`, `occ_attempts`, and `retry_count`.
- Compute final abort rate as final aborts divided by logical transactions.
- Keep OCC attempt/retry metrics separately for contention analysis.

### 3. Bounded Rotating Sample Misnamed as Reservoir

`LatencySampler::record()` replaces `samples_[index % sample_size_]` after the sample buffer is full (`src/latency_sampler.cpp:65-82`). This is bounded rotating replacement, not statistically uniform Algorithm R reservoir sampling.

Documentation-safe action applied:

- `paper.md`, `README.md`, `HANDOFF.md`, `PROJECT_PLAN_STATUS.md`, sold-counter metadata, and convergence summary now describe this as bounded rotating sampling and avoid unbiased-reservoir claims.

Patch options:

- Implement Algorithm R using a random index in `[0, seen]` after the buffer is full.
- Or rename CLI/reporting mode from `reservoir` to `bounded_rotating_sample` while keeping backward compatibility.

### 4. OCC Lock Acquisition Can Leave Phantom Locks

`try_acquire_locks()` sets lock bits as it iterates through the write set (`src/occ_engine.cpp:93-124`). If a later object cannot be locked, it returns `-1`. `commit_transaction()` returns immediately on that failure (`src/occ_engine.cpp:198-201`) without calling `release_locks(tx)`. Earlier acquired lock bits can therefore remain set until overwritten/reset by other logic, causing phantom lock failures.

Patch plan:

- Track acquired object IDs during `try_acquire_locks()`.
- On any failure, release only locks owned by the current transaction before returning.
- Add a unit/smoke test with a multi-object transaction where the second lock fails.

### 5. Legacy Fake p95/p99 Latency Fields

`RunResult::get_result()` computes `latency_p95_us = latency_p50_us * 1.5` and `latency_p99_us = latency_p50_us * 2.0` (`experiments/phase2_dsm_benchmark.cpp:660-663`). These are legacy estimated fields and must not be used as final latency evidence.

Current interpretation:

- Final analysis should use `tx_latency_us_p50/p95/p99` and committed/abort/path-specific sampled latency fields.
- Legacy `latency_us_p95/p99` may remain in CSVs for backward compatibility but should be labeled as legacy estimates.

Patch plan:

- Rename these fields to `legacy_latency_estimate_*` or omit them from final summaries.
- Keep sampled transaction latency fields as the only p95/p99 evidence.

### 6. Zipfian Distribution Rebuilt per Order

`InventoryWorkload::generate_order()` rebuilds the Zipfian weight vector and `std::discrete_distribution` on each generated order (`experiments/phase2_dsm_benchmark.cpp:178-185`). This is a performance overhead that can distort high-rate skewed workloads.

Patch plan:

- Precompute Zipfian distributions in `InventoryWorkload` construction based on `num_products` and theta.
- Keep per-thread RNG state but reuse the distribution object.
- Re-run a small Zipfian smoke to ensure trend stability.

### 7. Server Arbitration Abort Double Count

`ServerArbitrator::process_request()` increments `arbitrated_aborts_` when an object is missing (`src/server_arbitration.cpp:120-124`) and then increments it again in the final `else` branch (`src/server_arbitration.cpp:133-137`). This double-counts failed requests in that legacy arbitrator component.

Patch plan:

- Remove the inner increment and count aborts once in the final status branch.
- Add a direct missing-object test for the arbitrator.

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

For paper claims, use invariant checks, final stock/sold consistency, sampled transaction latency fields, and per-workload trend analysis. Do not rely on dead counters, mixed `attempted_tx` denominator, legacy fake latency p95/p99, or statistically-uniform reservoir claims until code fixes are authorized and validated.

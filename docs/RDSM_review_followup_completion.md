# RDSM Review Follow-up Completion

Date: 2026-06-01 UTC

This note records the fixes applied after the post-pass review. The local benchmark remains a shared-memory concurrency-control prototype. No hardware RDMA, network, PCIe, switch, or distributed DSM performance claim is made.

## Completed Fixes

### 1. Dormant RDMA Wrapper Lifecycle

Files: `src/rdma_conn.cpp`, `src/rdma_conn.h`

- Explicitly initialized every `RDMAConnectionImpl` pointer and scalar field.
- Added one cleanup helper for destructor and setup-failure paths.
- Deregistered memory regions before protection-domain deallocation.
- Preserved the CAS API requirement for a caller-provided registered local result buffer and valid `lkey`.
- Rejected null CAS result buffers and zero CAS result `lkey` values.
- Retained deleted copy construction and copy assignment; added compile-time regression checks.

Boundary: these are source-level lifecycle repairs only. The dormant wrapper is not considered usable until Phase 6 performs two-node Soft-RoCE READ/WRITE/CAS and CQ validation.

### 2. Stale Documentation

Files: `READING_GUIDE.md`, `results/final_audit_bug_report.md`, `HANDOFF.md`

- Replaced stale statements that described fixed counters, phantom locks, fake percentiles, Zipfian rebuilding, and arbitrator double counting as unresolved.
- Corrected lock-failure accounting documentation: failed OCC commit attempts increment `occ_failed_attempts`; retry exhaustion increments `final_abort_tx`.
- Preserved the deliberately limited meaning of duplicate and hot/cold instrumentation.

### 3. High-contention Counter Regression

Files: `CMakeLists.txt`, `tests/check_benchmark_json.py`, `tests/rdsm_regression_tests.cpp`

- Added CTest case `benchmark_high_contention_schema`.
- Runs baseline OCC with 4 threads, 100% writes, and one hot product.
- Requires `occ_failed_attempts > 0`.
- Verifies:

```text
logical_tx == committed_tx + final_abort_tx + business_abort_tx
occ_attempts >= occ_failed_attempts
occ_attempts == cold_path_tx + occ_failed_attempts
```

The existing deterministic engine-level retry-exhaustion test remains useful for isolated accounting behavior. The new integration CTest covers the benchmark retry loop.

## Intentional Workspace Cleanup

The removal of these files was confirmed as intentional before this follow-up:

- `docs/RESEARCH_DIRECTION_ANALYSIS.md`
- `docs/RESEARCH_MEMO.md`
- `docs/RESEARCH_ROADMAP_INDEX.md`
- `docs/TEMP_RESPOND.md`

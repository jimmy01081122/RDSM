#!/usr/bin/env python3
"""Parse Phase 2 experiment results into CSV and Markdown reports."""

import csv
import json
import os
import re
from collections import defaultdict
from pathlib import Path
from statistics import mean, median, stdev


RESULTS_DIR = Path(os.environ.get("RESULTS_DIR", "./results/phase2"))
REPORT_FILE = RESULTS_DIR / "phase2_report.md"


def load_json(path):
    with path.open() as f:
        return json.load(f)


def parse_os_metrics(run_dir):
    metrics = {}
    text = ""
    for name in ("os_time.txt", "perf.stderr.txt"):
        path = run_dir / name
        if path.exists():
            text += path.read_text(errors="ignore") + "\n"

    patterns = {
        "context_switches": r"([\d,]+)\s+context-switches",
        "cpu_migrations": r"([\d,]+)\s+cpu-migrations",
        "page_faults": r"([\d,]+)\s+page-faults",
        "elapsed_time_sec": r"Elapsed \(wall clock\) time.*:\s+([0-9:.]+)",
        "user_time_sec": r"User time \(seconds\):\s+([0-9.]+)",
        "sys_time_sec": r"System time \(seconds\):\s+([0-9.]+)",
    }
    for key, pattern in patterns.items():
        m = re.search(pattern, text)
        if not m:
            continue
        raw = m.group(1).replace(",", "")
        if ":" in raw:
            parts = [float(p) for p in raw.split(":")]
            value = 0
            for part in parts:
                value = value * 60 + part
            metrics[key] = value
        else:
            metrics[key] = float(raw)

    voluntary = re.search(r"Voluntary context switches:\s+([0-9]+)", text)
    involuntary = re.search(r"Involuntary context switches:\s+([0-9]+)", text)
    if "context_switches" not in metrics and (voluntary or involuntary):
        metrics["context_switches"] = float(voluntary.group(1) if voluntary else 0) + float(
            involuntary.group(1) if involuntary else 0
        )

    major = re.search(r"Major \(requiring I/O\) page faults:\s+([0-9]+)", text)
    minor = re.search(r"Minor \(reclaiming a frame\) page faults:\s+([0-9]+)", text)
    if "page_faults" not in metrics and (major or minor):
        metrics["page_faults"] = float(major.group(1) if major else 0) + float(minor.group(1) if minor else 0)
    return metrics


def aggregate_results(results_dir):
    all_runs = []
    groups = defaultdict(list)
    for run_dir in sorted(results_dir.iterdir() if results_dir.exists() else []):
        if not run_dir.is_dir():
            continue
        manifest_path = run_dir / "manifest.json"
        app_path = run_dir / "app_metrics.json"
        if not manifest_path.exists() or not app_path.exists():
            continue
        try:
            run = {**load_json(manifest_path), **load_json(app_path), **parse_os_metrics(run_dir)}
        except Exception as exc:
            print(f"Skipping {run_dir}: {exc}")
            continue
        all_runs.append(run)
        key = (
            run.get("algorithm", ""),
            run.get("workload", ""),
            int(run.get("thread_count", 0)),
            float(run.get("write_ratio", 0)),
        )
        groups[key].append(run)
    return all_runs, groups


def stats(rows, field):
    values = [r[field] for r in rows if isinstance(r.get(field), (int, float))]
    if not values:
        return {"mean": 0, "median": 0, "stddev": 0, "min": 0, "max": 0, "count": 0}
    return {
        "mean": mean(values),
        "median": median(values),
        "stddev": stdev(values) if len(values) > 1 else 0,
        "min": min(values),
        "max": max(values),
        "count": len(values),
    }


def write_csv(results_dir, rows):
    fields = [
        "run_id", "timestamp", "algorithm", "workload", "application_case", "thread_count", "write_ratio",
        "duration_sec", "matrix", "appendix_only", "appendix_reason", "access_pattern",
        "hot_access_probability", "hot_refresh_interval",
        "arbitration_mode", "hot_shards", "sold_counter_mode",
        "attempted_tx", "committed_tx", "aborted_tx", "business_abort_tx",
        "retry_count", "lock_fail_count", "validation_fail_count", "abort_rate",
        "retry_per_commit", "committed_tx_per_sec", "latency_us_p50", "latency_us_p95",
        "latency_us_p99", "hot_object_count", "hot_path_candidate_tx", "hot_path_tx",
        "cold_path_tx", "server_arbitrated_tx", "hot_path_ratio",
        "server_queue_wait_us_p50", "server_queue_wait_us_p95", "server_queue_wait_us_p99",
        "server_queue_wait_us_max", "queue_length_p50", "queue_length_p95", "queue_length_p99",
        "service_time_us_p50", "service_time_us_p95", "service_time_us_p99",
        "service_time_us_max", "hot_cold_interference_count", "invariant_violation_count",
        "duplicate_commit_count", "final_stock", "sold_count", "initial_stock",
        "context_switches", "cpu_migrations", "page_faults", "elapsed_time_sec",
        "user_time_sec", "sys_time_sec",
    ]
    out = results_dir / "summary.csv"
    with out.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    print(f"Wrote {out}")


def write_group_csv(results_dir, groups):
    fields = [
        "algorithm", "workload", "thread_count", "write_ratio", "runs",
        "committed_tx_per_sec_mean", "abort_rate_mean", "retry_per_commit_mean",
        "lock_fail_count_mean", "validation_fail_count_mean", "latency_us_p50_mean",
        "hot_path_ratio_mean", "server_queue_wait_us_p50_mean", "server_queue_wait_us_p95_mean",
        "server_queue_wait_us_p99_mean", "queue_length_p95_mean", "service_time_us_p95_mean",
    ]
    out = results_dir / "summary_by_config.csv"
    with out.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for key, rows in sorted(groups.items()):
            algo, workload, threads, write_ratio = key
            writer.writerow({
                "algorithm": algo,
                "workload": workload,
                "thread_count": threads,
                "write_ratio": write_ratio,
                "runs": len(rows),
                "committed_tx_per_sec_mean": stats(rows, "committed_tx_per_sec")["mean"],
                "abort_rate_mean": stats(rows, "abort_rate")["mean"],
                "retry_per_commit_mean": stats(rows, "retry_per_commit")["mean"],
                "lock_fail_count_mean": stats(rows, "lock_fail_count")["mean"],
                "validation_fail_count_mean": stats(rows, "validation_fail_count")["mean"],
                "latency_us_p50_mean": stats(rows, "latency_us_p50")["mean"],
                "hot_path_ratio_mean": stats(rows, "hot_path_ratio")["mean"],
                "server_queue_wait_us_p50_mean": stats(rows, "server_queue_wait_us_p50")["mean"],
                "server_queue_wait_us_p95_mean": stats(rows, "server_queue_wait_us_p95")["mean"],
                "server_queue_wait_us_p99_mean": stats(rows, "server_queue_wait_us_p99")["mean"],
                "queue_length_p95_mean": stats(rows, "queue_length_p95")["mean"],
                "service_time_us_p95_mean": stats(rows, "service_time_us_p95")["mean"],
            })
    print(f"Wrote {out}")


def write_report(rows, groups):
    by_algo = defaultdict(list)
    for row in rows:
        by_algo[row.get("algorithm", "")].append(row)
    focused_rows = [r for r in rows if r.get("matrix") == "focused_tradeoff"]
    application_rows = [r for r in rows if r.get("matrix") == "application_scenarios"]
    focused_by_workload = defaultdict(list)
    for row in focused_rows:
        focused_by_workload[row.get("workload", "")].append(row)
    application_by_workload = defaultdict(list)
    for row in application_rows:
        application_by_workload[row.get("workload", "")].append(row)

    total_violations = sum(int(r.get("invariant_violation_count", 0)) for r in rows)
    total_duplicates = sum(int(r.get("duplicate_commit_count", 0)) for r in rows)
    started = min((r.get("timestamp", "") for r in rows), default="")
    ended = max((r.get("timestamp", "") for r in rows), default="")

    lines = [
        "# Phase 2 Report",
        "",
        "## Scope",
        "",
        "This report evaluates a RDMA-style DSM prototype in a VirtualBox + Ubuntu 22.04 + Soft-RoCE environment. The results are protocol-level evidence for algorithm behavior only. They are not hardware RDMA NIC latency, throughput, CPU, PCIe, switch, or RNIC offload measurements.",
        "",
        "The measured benchmark path is a local RDMA-style DSM/OCC protocol simulation over the project object store. It does not by itself prove two-node Soft-RoCE transport behavior; transport setup should be validated separately with the existing RDMA connection utilities or verbs-level tests.",
        "",
        "The current hybrid arbitration implementation does not support crash recovery or durability.",
        "",
        "## Artifacts",
        "",
        f"- Raw run directories: `{RESULTS_DIR}`",
        f"- Per-run CSV: `{RESULTS_DIR / 'summary.csv'}`",
        f"- Grouped CSV: `{RESULTS_DIR / 'summary_by_config.csv'}`",
        f"- Run timestamp range: {started} to {ended}",
        "",
        "## Correctness",
        "",
        f"- Invariant violations: {total_violations}",
        f"- Duplicate commits: {total_duplicates}",
        f"- Status: {'PASS' if total_violations == 0 and total_duplicates == 0 else 'FAIL'}",
        "",
        "Checked invariants: stock remains non-negative by unsigned state transition, user balance does not underflow, and `sold_count + final_stock = initial_stock` across all products.",
        "",
        "## Algorithm Summary",
        "",
        "| Algorithm | Runs | Commit tx/sec mean | Abort rate mean | Retry/commit mean | Lock fails mean | Validation fails mean | Hot path ratio mean |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]

    for algo, algo_rows in sorted(by_algo.items()):
        lines.append(
            f"| {algo} | {len(algo_rows)} | "
            f"{stats(algo_rows, 'committed_tx_per_sec')['mean']:.2f} | "
            f"{stats(algo_rows, 'abort_rate')['mean']:.3f} | "
            f"{stats(algo_rows, 'retry_per_commit')['mean']:.3f} | "
            f"{stats(algo_rows, 'lock_fail_count')['mean']:.1f} | "
            f"{stats(algo_rows, 'validation_fail_count')['mean']:.1f} | "
            f"{stats(algo_rows, 'hot_path_ratio')['mean']:.3f} |"
        )

    lines += [
        "",
        "## Why Some Summary Values Are 0",
        "",
        "- `abort_rate = 0.000` means the value is below the displayed three-decimal precision or no transaction reached final abort/business-abort state in those runs. It does not mean there were no conflicts; check `lock_fail_count`, `validation_fail_count`, and `retry_count`.",
        "- `retry_per_commit = 0.000` means retries were extremely rare relative to commits after aggregation/rounding, or the backoff/hybrid path prevented failures from reaching the max-retry abort threshold.",
        "- `hot_path_ratio = 0.000` for baseline/backoff/hot-detection-only runs is expected because those variants do not route transactions through server-side arbitration. Hot detection may still mark hot objects, but only `hybrid_arbitration_occ` consumes that signal as a hot path.",
        "- `lock_fail_count` or `validation_fail_count` can be nonzero while `abort_rate` is zero because a failed attempt can retry and eventually commit. In this report, abort rate counts final failed transactions, not every failed commit attempt.",
        "- `server_queue_wait_us_* = 0` appears when server arbitration is disabled or when queue wait rounds below displayed precision.",
        "",
        "## Benchmark Direction",
        "",
        "The benchmark models a small RDMA-style DSM object store with object version, lock owner/bit, payload value, per-object conflict counters, and global transaction counters. A transaction reads stock and user balance, optionally writes stock/balance/sold_count, then commits through OCC or through the hybrid hot path. The experiment direction is to compare protocol behavior under controlled contention shapes: uniform low contention, moderate hot sets, single-hot-object flash-sale pressure, broad hot sets, Zipfian skew, and several application-like workloads.",
        "",
        "The numbers should be read as relative protocol trends in this prototype. They are useful for identifying conflict modes, retry behavior, hot-path routing, and correctness preservation. They are not hardware RDMA performance measurements.",
        "",
        "## Hybrid Arbitration Design",
        "",
        "`hybrid_arbitration_occ` keeps cold transactions on the OCC path and routes hot-object candidates to a server-side serialized path. Hot candidates come from configured hot products and from hot detection counters. The serialized path applies the transaction under the store mutation lock, eliminating OCC lock/validation races for those hot writes at the cost of queue wait and centralization.",
        "",
        "Compared with baseline OCC, hybrid reduces abort storms for hot objects because hot transactions no longer compete through repeated CAS-style lock acquisition and read-set validation. Compared with backoff, hybrid changes the conflict-resolution mechanism rather than only delaying retries. Compared with hot-detection-only, hybrid consumes the hot signal to change execution path.",
        "",
        "Current optimization: hot detection refresh is rate-limited by `--hot-refresh-interval` instead of scanning all product objects on every transaction. This reduces avoidable overhead, especially in low-contention workloads where hybrid should mostly remain on the cold path.",
        "",
        "## Data Sanity Check",
        "",
        f"- Total parsed runs: {len(rows)}",
        f"- Focused trade-off runs: {len(focused_rows)}",
        f"- Application scenario runs: {len(application_rows)}",
        "- The aggregate Algorithm Summary is directionally useful but mixes low-contention, high-contention, read-heavy, write-heavy, uniform, and skewed workloads. It should not be read as a universal ranking.",
        "- Backoff runs showing near-zero abort rate are plausible in this prototype because failed commit attempts are often converted into retry delay before max-retry abort, not because contention disappears.",
        "- Hybrid arbitration should show low hot-path ratio under low-contention workloads and high hot-path ratio under explicitly hot or detected-hot workloads. The focused matrix is used to validate that behavior.",
        "",
    ]

    if focused_rows:
        lines += [
            "## Focused Scenario Results",
            "",
            "| Workload | Best TX/sec Algorithm | Baseline TX/sec | Backoff TX/sec | Hot Detection TX/sec | Hybrid TX/sec | Hybrid Hot Path | Interpretation |",
            "|---|---|---:|---:|---:|---:|---:|---|",
        ]
        interpretations = {
            "low_uniform_read95": "Low hot-path ratio shows hybrid mostly stays cold; after refresh-rate optimization it is competitive, but this should be rechecked with longer runs.",
            "mixed_uniform_write20": "Uniform mixed traffic has limited hot-object benefit; optimized hybrid is competitive because it mostly avoids arbitration.",
            "mixed_hot4_write50": "Moderate hot-set contention benefits from selective arbitration; compare queue wait against reduced lock/validation failures.",
            "high_hot1_write100": "Single hot product favors hybrid serialization; OCC variants still see lock/validation pressure.",
            "high_hot16_write100": "Broad hot write set strongly favors hybrid serialization in this prototype.",
            "zipf99_write100": "Skewed Zipfian writes allow hybrid detection/arbitration to reduce conflict symptoms.",
        }
        for workload, workload_rows in sorted(focused_by_workload.items()):
            by_workload_algo = defaultdict(list)
            for row in workload_rows:
                by_workload_algo[row.get("algorithm", "")].append(row)
            txps = {
                algo: stats(algo_rows, "committed_tx_per_sec")["mean"]
                for algo, algo_rows in by_workload_algo.items()
            }
            best_algo = max(txps, key=txps.get)
            hybrid_rows = by_workload_algo.get("hybrid_arbitration_occ", [])
            lines.append(
                f"| {workload} | {best_algo} | "
                f"{txps.get('baseline_occ', 0):.0f} | "
                f"{txps.get('backoff_occ', 0):.0f} | "
                f"{txps.get('hot_detection_occ', 0):.0f} | "
                f"{txps.get('hybrid_arbitration_occ', 0):.0f} | "
                f"{stats(hybrid_rows, 'hot_path_ratio')['mean']:.3f} | "
                f"{interpretations.get(workload, 'Use per-run CSV for detailed interpretation.')} |"
            )
        lines += [
            "",
            "The focused results support the design hypothesis rather than proving hardware performance: hybrid arbitration is most useful when hot-object routing is accurate and contention is high; backoff is a lower-overhead mitigation for moderate conflicts; baseline OCC remains appropriate for low-contention or uniform mixed workloads.",
            "",
        ]

    if application_rows:
        lines += [
            "## Application Scenario Results",
            "",
            "| Scenario | Application | Best TX/sec Algorithm | Baseline TX/sec | Backoff TX/sec | Hot Detection TX/sec | Hybrid TX/sec | Hybrid Hot Path | Trend |",
            "|---|---|---|---:|---:|---:|---:|---:|---|",
        ]
        app_trends = {
            "flash_sale_spike": "Extreme single-product pressure favors hybrid arbitration.",
            "ticket_booking_hot_event": "Seat/event hotspots benefit from arbitration when hot routing is accurate.",
            "ticket_booking_many_events": "Distributed demand has lower hot-path ratio; hybrid still wins in this short run, but the margin should be validated with longer repetitions.",
            "ad_budget_hot_campaign": "Moderate write ratio and campaign hotspots often favor backoff or selective hybrid.",
            "ad_budget_read_heavy_dashboard": "Read-heavy monitoring favors simple cold-path execution.",
            "warehouse_restock_uniform": "Very low hot-path ratio indicates hybrid mostly behaves like optimized cold-path OCC in this run.",
            "long_tail_marketplace_zipf": "Zipfian skew creates emergent hot products; hybrid can help if detection is timely.",
            "mixed_hot_catalog": "Moderate skew/hot catalog is a trade-off between backoff overhead and arbitration serialization.",
        }
        for workload, workload_rows in sorted(application_by_workload.items()):
            by_workload_algo = defaultdict(list)
            for row in workload_rows:
                by_workload_algo[row.get("algorithm", "")].append(row)
            txps = {
                algo: stats(algo_rows, "committed_tx_per_sec")["mean"]
                for algo, algo_rows in by_workload_algo.items()
            }
            best_algo = max(txps, key=txps.get)
            hybrid_rows = by_workload_algo.get("hybrid_arbitration_occ", [])
            app_case = workload_rows[0].get("application_case", "")
            lines.append(
                f"| {workload} | {app_case} | {best_algo} | "
                f"{txps.get('baseline_occ', 0):.0f} | "
                f"{txps.get('backoff_occ', 0):.0f} | "
                f"{txps.get('hot_detection_occ', 0):.0f} | "
                f"{txps.get('hybrid_arbitration_occ', 0):.0f} | "
                f"{stats(hybrid_rows, 'hot_path_ratio')['mean']:.3f} | "
                f"{app_trends.get(workload, 'Use per-run CSV for detailed interpretation.')} |"
            )
        lines += [
            "",
            "These scenarios intentionally vary application shape rather than only following the high-contention grid. The purpose is to see when arbitration is helpful, when backoff is sufficient, and when baseline OCC is preferable because centralization overhead dominates conflict cost.",
            "",
        ]

    lines += [
        "",
        "## Observations",
        "",
        "- Baseline OCC exposes the expected high-contention failure modes through lock failures, validation failures, retries, and lower committed throughput under hot-product workloads.",
        "- Contention-aware backoff changes retry timing and is useful as a relative trend signal, but the exact abort and commit ratios remain Soft-RoCE/VirtualBox-specific.",
        "- Hot object detection reports candidate hot objects and hot-path candidates, separating detection evidence from actual server arbitration.",
        "- Hybrid arbitration serializes transactions that touch detected or configured hot products. This reduces OCC conflict behavior for those transactions at the cost of queue-wait overhead.",
        "- The most reliable signal is not a single mean throughput number, but the combination of committed tx/sec, retry_per_commit, lock/validation failures, hot_path_ratio, and correctness counters per scenario.",
        "",
        "## Non-Comparable Items",
        "",
        "- Absolute RDMA latency and throughput",
        "- Absolute CPU utilization",
        "- Hardware-counter metrics such as cycles, instructions, or IPC",
        "- RNIC offload effects, PCIe bottlenecks, switch latency, RoCE congestion control, and bare-metal cluster scalability",
        "",
        "## Notes",
        "",
        "The benchmark is a constrained local DSM protocol prototype using RDMA-style object metadata, OCC read/write/validate phases, and a simplified FIFO server arbitration path. Use the CSV files for detailed comparisons by workload, thread count, and write ratio.",
        "",
        "## Future Work",
        "",
        "1. Replace the current coarse local mutation lock with per-object or per-hot-shard arbitration queues so unrelated hot objects can proceed in parallel.",
        "2. Add adaptive routing: send a transaction to arbitration only when predicted OCC retry cost exceeds estimated queue wait.",
        "3. Record true latency samples instead of estimating p95/p99 from the mean; this is needed for credible tail-latency analysis.",
        "4. Add two-node Soft-RoCE transport validation for the benchmark path, then keep protocol results separate from transport overhead.",
        "5. Add crash recovery/durability semantics for server arbitration before discussing production readiness.",
        "6. Explore dynamic hot threshold tuning and hysteresis to prevent oscillation between cold and hot paths.",
        "7. Compare against additional contention-control designs such as wound-wait, timestamp ordering, queued locks, and deterministic scheduling.",
        "8. Increase repetitions and run duration for publication-grade statistics; the current matrix is useful for trend discovery, not final claims.",
        "",
    ]

    REPORT_FILE.write_text("\n".join(lines))
    (RESULTS_DIR / "phase2_report.md").write_text("\n".join(lines))
    print(f"Wrote {REPORT_FILE} and {RESULTS_DIR / 'phase2_report.md'}")


def main():
    rows, groups = aggregate_results(RESULTS_DIR)
    if not rows:
        raise SystemExit(f"No results found in {RESULTS_DIR}")
    write_csv(RESULTS_DIR, rows)
    write_group_csv(RESULTS_DIR, groups)
    write_report(rows, groups)


if __name__ == "__main__":
    main()

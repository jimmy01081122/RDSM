#!/usr/bin/env python3
import json
import subprocess
import sys


def main() -> int:
    benchmark = sys.argv[1]
    high_contention = "--high-contention" in sys.argv[2:]
    options = [
        "--products", "4",
        "--users", "10",
        "--threads", "4" if high_contention else "1",
        "--duration-sec", "2" if high_contention else "1",
        "--algorithm", "baseline_occ",
        "--latency-sampling", "reservoir",
        "--latency-sample-size", "1000",
    ]
    if high_contention:
        options.extend([
            "--write-ratio", "1.0",
            "--hot-products", "1",
            "--hot-access-prob", "0.95",
        ])
    completed = subprocess.run(
        [benchmark, *options],
        check=True,
        capture_output=True,
        text=True,
    )
    data = json.loads(completed.stdout)

    assert data["counter_schema_version"] == 2
    assert data["latency_sampling_mode"] == "bounded_rotation"
    assert data["attempted_tx"] == data["logical_tx"]
    assert data["aborted_tx"] == data["final_abort_tx"]
    assert data["logical_tx"] == (
        data["committed_tx"] + data["final_abort_tx"] + data["business_abort_tx"]
    )
    assert data["occ_attempts"] >= data["occ_failed_attempts"]
    assert data["occ_attempts"] == data["cold_path_tx"] + data["occ_failed_attempts"]
    if high_contention:
        assert data["occ_failed_attempts"] > 0
    for percentile in ("p50", "p95", "p99"):
        assert data[f"latency_us_{percentile}"] == data[f"tx_latency_us_{percentile}"]
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

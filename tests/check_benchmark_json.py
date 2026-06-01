#!/usr/bin/env python3
import json
import subprocess
import sys


def main() -> int:
    benchmark = sys.argv[1]
    completed = subprocess.run(
        [
            benchmark,
            "--products", "4",
            "--users", "10",
            "--threads", "1",
            "--duration-sec", "1",
            "--algorithm", "baseline_occ",
            "--latency-sampling", "reservoir",
            "--latency-sample-size", "1000",
        ],
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
    for percentile in ("p50", "p95", "p99"):
        assert data[f"latency_us_{percentile}"] == data[f"tx_latency_us_{percentile}"]
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

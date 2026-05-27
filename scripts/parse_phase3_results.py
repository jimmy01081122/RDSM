#!/usr/bin/env python3
"""Parse Phase 3 two-node Soft-RoCE validation results."""

import csv
import json
import re
from pathlib import Path
from statistics import mean, stdev


RESULTS_DIR = Path("./results/phase3")
LEGACY_STAT_DIR = Path("./stat")
SUMMARY_CSV = RESULTS_DIR / "two_node_soft_roce_summary.csv"
REPORT_MD = RESULTS_DIR / "phase3_two_node_soft_roce_report.md"


def read_text(path):
    return path.read_text(errors="replace") if path.exists() else ""


def load_json(path):
    return json.loads(read_text(path))


def parse_common(stdout):
    row = {}
    for key in ("Transport type", "Connection type", "Link type", "Mtu", "GID index"):
        m = re.search(rf"{re.escape(key)}\s*:\s*([^\n]+?)(?:\s{{2,}}|$)", stdout)
        if m:
            row[key.lower().replace(" ", "_")] = m.group(1).strip()

    gids = re.findall(r"GID:\s*([0-9a-fA-F:]+)", stdout)
    if gids:
        row["local_gid"] = gids[0]
    if len(gids) > 1:
        row["remote_gid"] = gids[1]

    qpn = re.findall(r"QPN\s+0x([0-9a-fA-F]+)", stdout)
    if qpn:
        row["local_qpn"] = qpn[0]
    if len(qpn) > 1:
        row["remote_qpn"] = qpn[1]
    return row


def parse_perftest_stdout(stdout):
    row = parse_common(stdout)
    latency = re.search(
        r"^\s*(\d+)\s+(\d+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)",
        stdout,
        re.MULTILINE,
    )
    if latency:
        row.update({
            "payload_size_bytes_observed": int(latency.group(1)),
            "iterations": int(latency.group(2)),
            "lat_min_us": float(latency.group(3)),
            "lat_max_us": float(latency.group(4)),
            "lat_typical_us": float(latency.group(5)),
            "lat_avg_us": float(latency.group(6)),
            "lat_stdev_us": float(latency.group(7)),
            "lat_p99_us": float(latency.group(8)),
            "lat_p999_us": float(latency.group(9)),
        })

    bandwidth = re.search(
        r"^\s*(\d+)\s+(\d+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)",
        stdout,
        re.MULTILINE,
    )
    if bandwidth and "lat_avg_us" not in row:
        row.update({
            "payload_size_bytes_observed": int(bandwidth.group(1)),
            "iterations": int(bandwidth.group(2)),
            "bw_peak_mb_sec": float(bandwidth.group(3)),
            "bw_avg_mb_sec": float(bandwidth.group(4)),
            "msg_rate_mpps": float(bandwidth.group(5)),
        })
    return row


def legacy_test_name(path):
    name = path.parent.name.lower()
    if "write bandwidth" in name:
        return "ib_write_bw"
    if "write latency" in name:
        return "ib_write_lat"
    if "read latency" in name:
        return "ib_read_lat"
    if "send latency" in name:
        return "ib_send_lat"
    return name.replace(" ", "_")


def collect_rows():
    rows = []

    for path in sorted(RESULTS_DIR.glob("two_node_soft_roce_*/client/*.json")):
        data = load_json(path)
        row = {
            "source": "phase3_rerun",
            "run_dir": str(path.parents[1]),
            "json_file": str(path),
            "test": data.get("test", ""),
            "payload_size_bytes": data.get("payload_size_bytes", 0),
            "exit_code": data.get("exit_code", 999),
            "start_time": data.get("start_time", ""),
            "end_time": data.get("end_time", ""),
            "client_hostname": data.get("client_hostname", ""),
            "server_ip": data.get("server_ip", ""),
            "rdma_device": data.get("rdma_device", ""),
        }
        row.update(parse_perftest_stdout(data.get("stdout", "")))
        row["success"] = row["exit_code"] == 0 and (
            "lat_avg_us" in row or "bw_avg_mb_sec" in row
        )
        rows.append(row)

    for path in sorted(LEGACY_STAT_DIR.glob("*/output.json")):
        text = read_text(path)
        row = {
            "source": "phase1_stat",
            "run_dir": str(path.parent),
            "json_file": str(path),
            "test": legacy_test_name(path),
            "payload_size_bytes": 0,
            "exit_code": 0,
            "start_time": "",
            "end_time": "",
            "client_hostname": "",
            "server_ip": "192.168.56.101",
            "rdma_device": "rxe0",
        }
        row.update(parse_perftest_stdout(text))
        row["payload_size_bytes"] = row.get("payload_size_bytes_observed", 0)
        row["success"] = "lat_avg_us" in row or "bw_avg_mb_sec" in row
        rows.append(row)

    return rows


def write_csv(rows):
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    fields = [
        "source", "run_dir", "json_file", "success", "test", "payload_size_bytes",
        "payload_size_bytes_observed", "iterations", "exit_code", "start_time", "end_time",
        "client_hostname", "server_ip", "rdma_device", "transport_type", "connection_type",
        "link_type", "mtu", "gid_index", "local_gid", "remote_gid", "local_qpn", "remote_qpn",
        "lat_min_us", "lat_max_us", "lat_typical_us", "lat_avg_us", "lat_stdev_us",
        "lat_p99_us", "lat_p999_us", "bw_peak_mb_sec", "bw_avg_mb_sec", "msg_rate_mpps",
    ]
    with SUMMARY_CSV.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    print(f"Wrote {SUMMARY_CSV}")


def avg(rows, field):
    vals = [float(r[field]) for r in rows if isinstance(r.get(field), (int, float))]
    return mean(vals) if vals else 0.0


def sd(rows, field):
    vals = [float(r[field]) for r in rows if isinstance(r.get(field), (int, float))]
    return stdev(vals) if len(vals) > 1 else 0.0


def fmt(value, digits=2):
    return f"{value:.{digits}f}"


def write_report(rows):
    successful = [r for r in rows if r.get("success")]
    failed = [r for r in rows if not r.get("success")]
    rerun = [r for r in successful if r.get("source") == "phase3_rerun"]
    legacy = [r for r in successful if r.get("source") == "phase1_stat"]

    lines = [
        "# Phase 3 Two-Node Soft-RoCE Validation Report",
        "",
        "## Scope",
        "",
        "This phase validates the two-node Soft-RoCE verbs transport path between node2 (client, 192.168.56.102) and node1 (server, 192.168.56.101). It is a transport sanity check and calibration step, not a hardware RDMA performance claim and not an end-to-end distributed DSM benchmark.",
        "",
        "The validation uses perftest RC queue-pair tests over `rxe0`: RDMA WRITE latency, RDMA READ latency, SEND latency, and RDMA WRITE bandwidth. Phase 1 legacy `/stat` results are parsed beside the new Phase 3 rerun so the report can compare historical single-shot measurements with the reproducible sweep.",
        "",
        "## Artifacts",
        "",
        f"- Summary CSV: `{SUMMARY_CSV}`",
        f"- Parsed successful rows: {len(successful)}",
        f"- Phase 3 rerun rows: {len(rerun)}",
        f"- Phase 1 `/stat` rows: {len(legacy)}",
        f"- Failed or unparsable rows: {len(failed)}",
        "",
        "## Transport Evidence",
        "",
        "Successful rows expose RC transport metadata: `Transport type: IB`, `Connection type: RC`, `Link type: Ethernet`, GID index, local/remote GID, and local/remote QPN. In this environment the GIDs encode the host-only network addresses, which confirms that the observed path is node2 -> node1 over Soft-RoCE rather than an in-process benchmark.",
        "",
        "## Phase 3 Rerun Summary",
        "",
    ]

    if rerun:
        lines += [
            "| Test | Rows | Size range | Mean latency us | Mean p99 us | Mean BW MB/s | Mean MsgRate Mpps |",
            "|---|---:|---:|---:|---:|---:|---:|",
        ]
        for test in sorted({r["test"] for r in rerun}):
            group = [r for r in rerun if r["test"] == test]
            sizes = sorted(int(r.get("payload_size_bytes", 0)) for r in group)
            lines.append(
                f"| {test} | {len(group)} | {sizes[0]}-{sizes[-1]} | "
                f"{fmt(avg(group, 'lat_avg_us'))} | {fmt(avg(group, 'lat_p99_us'))} | "
                f"{fmt(avg(group, 'bw_avg_mb_sec'))} | {fmt(avg(group, 'msg_rate_mpps'), 6)} |"
            )
    else:
        lines.append("No Phase 3 rerun rows were found.")

    lines += [
        "",
        "## Legacy `/stat` Summary",
        "",
    ]

    if legacy:
        lines += [
            "| Test | Payload bytes | Avg latency us | p99 us | p999 us | Avg BW MB/s | MsgRate Mpps |",
            "|---|---:|---:|---:|---:|---:|---:|",
        ]
        for row in sorted(legacy, key=lambda r: r["test"]):
            lines.append(
                f"| {row.get('test', '')} | {row.get('payload_size_bytes', 0)} | "
                f"{fmt(float(row.get('lat_avg_us', 0) or 0))} | "
                f"{fmt(float(row.get('lat_p99_us', 0) or 0))} | "
                f"{fmt(float(row.get('lat_p999_us', 0) or 0))} | "
                f"{fmt(float(row.get('bw_avg_mb_sec', 0) or 0))} | "
                f"{fmt(float(row.get('msg_rate_mpps', 0) or 0), 6)} |"
            )
    else:
        lines.append("No legacy `/stat` rows were found.")

    lines += [
        "",
        "## Interpretation",
        "",
        "- The two-node validation is useful because it proves the Soft-RoCE stack, RC QP setup, address exchange, and CQ completion path are operational across two VMs.",
        "- The measurements should not be used as hardware RDMA latency/bandwidth numbers. They include VirtualBox networking, Linux RXE software processing, guest scheduling, and host-only Ethernet effects.",
        "- These results are intentionally kept separate from Phase 2 local DSM/OCC throughput. Phase 2 compares contention-control protocol trends; Phase 3 validates that the transport substrate exists and gives a calibration boundary.",
        "- The legacy `/stat` latency rows use a 2-byte default payload and 1000 iterations, while the Phase 3 rerun sweeps explicit payload sizes. They are comparable as environment evidence, not as a stable latency distribution.",
        "",
        "## Review-Grade Limitations",
        "",
        "- This phase does not run the DSM/OCC benchmark over remote verbs; it only validates standalone verbs-level transport.",
        "- No CAS/atomic perftest row is included yet, so the validation covers READ/WRITE/SEND but not remote atomic behavior.",
        "- Soft-RoCE in VirtualBox has high jitter; tail values should be treated as diagnostic noise unless repetitions, pinning, warm-up, and host-load control are added.",
        "- Latency-related DSM claims remain intentionally out of scope for the current project phase.",
        "",
    ]
    REPORT_MD.write_text("\n".join(lines))
    print(f"Wrote {REPORT_MD}")


def main():
    rows = collect_rows()
    if not rows:
        raise SystemExit("No Phase 3 or legacy stat rows found.")
    write_csv(rows)
    write_report(rows)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
import csv
import json
import os
import re
import sys
from pathlib import Path


LATENCY_TESTS = {"ib_write_lat", "ib_read_lat", "ib_send_lat"}
BANDWIDTH_TESTS = {"ib_write_bw"}

EXPECTED_LATENCY_SIZES = {64, 1024, 4096}
EXPECTED_BANDWIDTH_SIZES = {4096, 16384, 65536}

EXCLUDED_LATENCY_SIZE = 65536


def read_text(path):
    try:
        return Path(path).read_text(errors="replace")
    except Exception:
        return ""


def read_json(path):
    try:
        text = Path(path).read_text(errors="replace").strip()
        if not text:
            return None
        return json.loads(text)
    except Exception:
        return None


def safe_float(value):
    try:
        return float(value)
    except Exception:
        return ""


def safe_int(value):
    try:
        return int(value)
    except Exception:
        return ""


def parse_perf(perf_text):
    metrics = {
        "cycles": "",
        "instructions": "",
        "context_switches": "",
        "cpu_migrations": "",
        "page_faults": "",
        "seconds_elapsed": "",
        "seconds_user": "",
        "seconds_sys": "",
        "cycles_supported": "unknown",
        "instructions_supported": "unknown",
    }

    if "<not supported>" in perf_text and "cycles" in perf_text:
        metrics["cycles_supported"] = "false"

    if "<not supported>" in perf_text and "instructions" in perf_text:
        metrics["instructions_supported"] = "false"

    patterns = {
        "cycles": r"^\s*([\d,]+)\s+cycles\b",
        "instructions": r"^\s*([\d,]+)\s+instructions\b",
        "context_switches": r"^\s*([\d,]+)\s+context-switches\b",
        "cpu_migrations": r"^\s*([\d,]+)\s+cpu-migrations\b",
        "page_faults": r"^\s*([\d,]+)\s+page-faults\b",
        "seconds_elapsed": r"^\s*([\d.]+)\s+seconds time elapsed\b",
        "seconds_user": r"^\s*([\d.]+)\s+seconds user\b",
        "seconds_sys": r"^\s*([\d.]+)\s+seconds sys\b",
    }

    for line in perf_text.splitlines():
        for key, pat in patterns.items():
            m = re.search(pat, line)
            if m:
                metrics[key] = m.group(1).replace(",", "")

    if metrics["cycles"]:
        metrics["cycles_supported"] = "true"

    if metrics["instructions"]:
        metrics["instructions_supported"] = "true"

    return metrics


def parse_iperf3(path):
    data = read_json(path)
    stderr_path = Path(str(path).replace(".json", ".stderr.txt"))
    stderr_text = read_text(stderr_path)

    row = {
        "file": str(path),
        "direction": Path(path).stem,
        "valid": "false",
        "error": "",
        "sender_throughput_mbps": "",
        "receiver_throughput_mbps": "",
        "retransmits": "",
        "sender_cpu_total_percent": "",
        "sender_cpu_user_percent": "",
        "sender_cpu_system_percent": "",
        "receiver_cpu_total_percent": "",
        "receiver_cpu_user_percent": "",
        "receiver_cpu_system_percent": "",
        "stderr_preview": stderr_text[:500].replace("\n", " "),
    }

    if not data:
        row["error"] = "invalid_or_empty_json"
        if stderr_text.strip():
            row["error"] += ": " + stderr_text.strip().replace("\n", " ")[:300]
        return row

    if "error" in data:
        row["error"] = str(data.get("error", "iperf3_json_error"))
        return row

    end = data.get("end", {})
    sent = end.get("sum_sent", {})
    recv = end.get("sum_received", {})
    cpu = end.get("cpu_utilization_percent", {})

    sent_bps = sent.get("bits_per_second", "")
    recv_bps = recv.get("bits_per_second", "")

    row["sender_throughput_mbps"] = sent_bps / 1_000_000 if isinstance(sent_bps, (int, float)) else ""
    row["receiver_throughput_mbps"] = recv_bps / 1_000_000 if isinstance(recv_bps, (int, float)) else ""
    row["retransmits"] = sent.get("retransmits", "")

    row["sender_cpu_total_percent"] = cpu.get("host_total", "")
    row["sender_cpu_user_percent"] = cpu.get("host_user", "")
    row["sender_cpu_system_percent"] = cpu.get("host_system", "")

    row["receiver_cpu_total_percent"] = cpu.get("remote_total", "")
    row["receiver_cpu_user_percent"] = cpu.get("remote_user", "")
    row["receiver_cpu_system_percent"] = cpu.get("remote_system", "")

    if row["sender_throughput_mbps"] != "" and row["receiver_throughput_mbps"] != "":
        row["valid"] = "true"

    return row


def classify_rdma_test(test, size, exit_code):
    if test in LATENCY_TESTS:
        if size == EXCLUDED_LATENCY_SIZE:
            return "invalid_unexpected_65536_latency"

        if size in EXPECTED_LATENCY_SIZES:
            return "valid_latency" if exit_code == 0 else "failed_latency"

        return "unexpected_latency_size"

    if test in BANDWIDTH_TESTS:
        if size in EXPECTED_BANDWIDTH_SIZES:
            return "valid_bandwidth" if exit_code == 0 else "failed_bandwidth"

        return "unexpected_bandwidth_size"

    return "unknown_test"


def parse_rdma_stdout_basic(test, stdout):
    result = {
        "last_numeric_line": "",
        "parse_note": "raw_stdout_preserved",
    }

    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    numeric_lines = []

    for line in lines:
        if re.match(r"^[\d\s\.,]+$", line):
            numeric_lines.append(line)

    if numeric_lines:
        result["last_numeric_line"] = numeric_lines[-1]
        result["parse_note"] = "last_numeric_line_extracted"

    return result


def parse_rdma_json(path):
    data = read_json(path)
    if not data:
        return None

    test = data.get("test", "")
    size = safe_int(data.get("payload_size_bytes", -1))
    exit_code = safe_int(data.get("exit_code", -1))

    perf_text = data.get("perf_stat", "")
    perf = parse_perf(perf_text)

    stdout = data.get("stdout", "")
    stderr = data.get("stderr", "")

    parsed_stdout = parse_rdma_stdout_basic(test, stdout)
    validity = classify_rdma_test(test, size, exit_code)

    return {
        "file": str(path),
        "test": test,
        "payload_size_bytes": size,
        "exit_code": exit_code,
        "validity": validity,
        "command": data.get("command", ""),
        "cycles": perf["cycles"],
        "instructions": perf["instructions"],
        "cycles_supported": perf["cycles_supported"],
        "instructions_supported": perf["instructions_supported"],
        "context_switches": perf["context_switches"],
        "cpu_migrations": perf["cpu_migrations"],
        "page_faults": perf["page_faults"],
        "seconds_elapsed": perf["seconds_elapsed"],
        "seconds_user": perf["seconds_user"],
        "seconds_sys": perf["seconds_sys"],
        "stdout_parsed_metric": parsed_stdout["last_numeric_line"],
        "stdout_parse_note": parsed_stdout["parse_note"],
        "stdout_preview": stdout[:500].replace("\n", " "),
        "stderr_preview": stderr[:500].replace("\n", " "),
    }


def write_csv(path, rows, fields):
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def load_manifest(root):
    manifest_path = root / "manifest.json"
    if manifest_path.exists():
        return read_json(manifest_path) or {}
    return {}


def load_measurement_note(root):
    note_path = root / "notes" / "measurement_decision.md"
    if note_path.exists():
        return read_text(note_path)
    return ""


def collect_tcp_rows(root):
    """
    TCP result selection policy:

    1. If valid tcp_fix results exist, use only tcp_fix results for the main TCP baseline.
    2. If no valid tcp_fix exists, use primary tcp/ results.
    3. Invalid primary TCP results should not pollute the final TCP baseline table
       when a valid tcp_fix exists.
    """

    def collect_from_dir(tcp_dir, source_label):
        rows = []
        if not tcp_dir.exists():
            return rows

        for p in sorted(tcp_dir.glob("*.json")):
            row = parse_iperf3(p)
            if row:
                row["source_dir"] = str(tcp_dir)
                row["source_label"] = source_label
                rows.append(row)

        return rows

    primary_tcp = root / "tcp"
    local_tcp_fix = root / "tcp_fix"
    home_tcp_fix = Path.home() / "rdma-calibration" / "results" / "final" / "tcp_fix"

    tcp_fix_rows = []

    tcp_fix_rows.extend(collect_from_dir(local_tcp_fix, "tcp_fix_in_result_dir"))

    if home_tcp_fix.exists() and home_tcp_fix != local_tcp_fix:
        tcp_fix_rows.extend(collect_from_dir(home_tcp_fix, "tcp_fix_global"))

    valid_tcp_fix_rows = [r for r in tcp_fix_rows if r.get("valid") == "true"]

    if valid_tcp_fix_rows:
        return valid_tcp_fix_rows

    primary_rows = collect_from_dir(primary_tcp, "primary_tcp")
    return primary_rows


def collect_rdma_rows(root):
    rdma_rows = []
    rdma_dir = root / "rdma_os"

    if not rdma_dir.exists():
        return rdma_rows

    for p in sorted(rdma_dir.glob("*/result.json")):
        row = parse_rdma_json(p)
        if row:
            rdma_rows.append(row)

    return rdma_rows


def validate_expected_tests(rdma_rows):
    observed = {(r["test"], int(r["payload_size_bytes"])) for r in rdma_rows}

    expected = set()

    for size in EXPECTED_LATENCY_SIZES:
        for test in LATENCY_TESTS:
            expected.add((test, size))

    for size in EXPECTED_BANDWIDTH_SIZES:
        for test in BANDWIDTH_TESTS:
            expected.add((test, size))

    missing = sorted(expected - observed)
    unexpected = sorted(observed - expected)

    invalid_65536_latency = sorted(
        (test, size)
        for test, size in observed
        if test in LATENCY_TESTS and size == EXCLUDED_LATENCY_SIZE
    )

    failed = sorted(
        (r["test"], int(r["payload_size_bytes"]), int(r["exit_code"]), r["validity"])
        for r in rdma_rows
        if int(r["exit_code"]) != 0
    )

    return {
        "missing": missing,
        "unexpected": unexpected,
        "invalid_65536_latency": invalid_65536_latency,
        "failed": failed,
    }


def detect_perf_limitations(rdma_rows):
    cycles_all_missing = all(not str(r.get("cycles", "")).strip() for r in rdma_rows) if rdma_rows else False
    instructions_all_missing = all(not str(r.get("instructions", "")).strip() for r in rdma_rows) if rdma_rows else False

    cycles_not_supported = any(r.get("cycles_supported") == "false" for r in rdma_rows)
    instructions_not_supported = any(r.get("instructions_supported") == "false" for r in rdma_rows)

    return {
        "cycles_all_missing": cycles_all_missing,
        "instructions_all_missing": instructions_all_missing,
        "cycles_not_supported": cycles_not_supported,
        "instructions_not_supported": instructions_not_supported,
        "hardware_counters_unusable": (
            cycles_all_missing
            and instructions_all_missing
            and (cycles_not_supported or instructions_not_supported)
        ),
    }


def detect_tcp_limitations(tcp_rows):
    if not tcp_rows:
        return {
            "tcp_available": False,
            "tcp_valid_count": 0,
            "tcp_invalid_count": 0,
            "tcp_has_invalid": True,
            "reason": "no_tcp_json_found",
        }

    valid_count = sum(1 for r in tcp_rows if r.get("valid") == "true")
    invalid_count = len(tcp_rows) - valid_count

    reason = ""
    if invalid_count > 0:
        errors = [r.get("error", "") or r.get("stderr_preview", "") for r in tcp_rows if r.get("valid") != "true"]
        reason = " | ".join(e for e in errors if e)[:500]

    return {
        "tcp_available": True,
        "tcp_valid_count": valid_count,
        "tcp_invalid_count": invalid_count,
        "tcp_has_invalid": invalid_count > 0,
        "reason": reason,
    }


def write_markdown_report(root, processed, tcp_rows, rdma_rows, manifest, note):
    report = processed / "phase1_final_report.md"
    validation = validate_expected_tests(rdma_rows)
    perf_limit = detect_perf_limitations(rdma_rows)
    tcp_limit = detect_tcp_limitations(tcp_rows)

    with open(report, "w", encoding="utf-8") as f:
        f.write("# Phase 1 Final Measurement Report\n\n")
        f.write(f"Result directory: `{root}`\n\n")

        f.write("## Measurement Scope\n\n")
        f.write("This report summarizes the final Phase 1 calibration measurement under VirtualBox + Ubuntu 22.04 + Soft-RoCE.\n\n")
        f.write("The measurement does not claim real hardware RDMA performance. The purpose is to classify which Soft-RoCE simulation data is reliable, which is only useful as a trend, and which should not be compared with real RDMA NIC results.\n\n")

        f.write("## Measurement Decision\n\n")
        f.write("65536-byte payload is excluded from latency tests and retained only for bandwidth tests.\n\n")
        f.write("Reason: prior measurements showed repeated timeout with exit code 124 for 65536-byte latency tests under VirtualBox + Soft-RoCE.\n\n")

        if note:
            f.write("Detailed note is available in:\n\n")
            f.write("`notes/measurement_decision.md`\n\n")

        f.write("## Test Plan\n\n")
        f.write("### Latency Tests\n\n")
        f.write("- Tests: `ib_write_lat`, `ib_read_lat`, `ib_send_lat`\n")
        f.write("- Payload sizes: `64`, `1024`, `4096` bytes\n\n")

        f.write("### Bandwidth Tests\n\n")
        f.write("- Test: `ib_write_bw`\n")
        f.write("- Payload sizes: `4096`, `16384`, `65536` bytes\n\n")

        f.write("## TCP Baseline\n\n")
        f.write("| Direction/File | Valid | Sender Mbps | Receiver Mbps | Retransmits | Sender CPU % | Receiver CPU % | Error |\n")
        f.write("|---|---|---:|---:|---:|---:|---:|---|\n")

        if tcp_rows:
            for r in tcp_rows:
                f.write(
                    f"| {Path(r['file']).name} | "
                    f"{r.get('valid', '')} | "
                    f"{r.get('sender_throughput_mbps', '')} | "
                    f"{r.get('receiver_throughput_mbps', '')} | "
                    f"{r.get('retransmits', '')} | "
                    f"{r.get('sender_cpu_total_percent', '')} | "
                    f"{r.get('receiver_cpu_total_percent', '')} | "
                    f"{str(r.get('error', '') or r.get('stderr_preview', '')).replace('|', '/')} |\n"
                )
        else:
            f.write("| no TCP result | false |  |  |  |  |  | no TCP JSON found |\n")

        f.write("\n## RDMA Soft-RoCE OS Overhead Summary\n\n")
        f.write("| Test | Size | Exit | Validity | Cycles | Instructions | Context Switches | CPU Migrations | Page Faults | Elapsed Seconds | User Seconds | Sys Seconds |\n")
        f.write("|---|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|\n")

        for r in rdma_rows:
            f.write(
                f"| {r['test']} | "
                f"{r['payload_size_bytes']} | "
                f"{r['exit_code']} | "
                f"{r['validity']} | "
                f"{r['cycles']} | "
                f"{r['instructions']} | "
                f"{r['context_switches']} | "
                f"{r['cpu_migrations']} | "
                f"{r['page_faults']} | "
                f"{r['seconds_elapsed']} | "
                f"{r['seconds_user']} | "
                f"{r['seconds_sys']} |\n"
            )

        f.write("\n## Validation Check\n\n")

        if validation["missing"]:
            f.write("### Missing Expected Tests\n\n")
            for test, size in validation["missing"]:
                f.write(f"- `{test}` size `{size}`\n")
            f.write("\n")
        else:
            f.write("- No expected test is missing.\n")

        if validation["unexpected"]:
            f.write("\n### Unexpected Tests\n\n")
            for test, size in validation["unexpected"]:
                f.write(f"- `{test}` size `{size}`\n")
            f.write("\n")
        else:
            f.write("- No unexpected test was found.\n")

        if validation["invalid_65536_latency"]:
            f.write("\n### Invalid 65536-byte Latency Tests Found\n\n")
            for test, size in validation["invalid_65536_latency"]:
                f.write(f"- `{test}` size `{size}` should not be part of final Phase 1 latency results.\n")
            f.write("\n")
        else:
            f.write("- No 65536-byte latency test was found. This matches the final measurement decision.\n")

        if validation["failed"]:
            f.write("\n### Failed RDMA Tests\n\n")
            for test, size, exit_code, validity in validation["failed"]:
                f.write(f"- `{test}` size `{size}` exit `{exit_code}` validity `{validity}`\n")
            f.write("\n")
        else:
            f.write("- No RDMA test failed.\n")

        f.write("\n## Measurement Limitations Observed in This Run\n\n")

        if perf_limit["hardware_counters_unusable"]:
            f.write("### perf Hardware Counter Limitation\n\n")
            f.write("In this run, `perf stat` did not provide usable `cycles` and `instructions` values. The raw `perf` output reported hardware events as `<not supported>` in the current VirtualBox Ubuntu guest environment.\n\n")
            f.write("Therefore, this report excludes `cycles`, `instructions`, and derived IPC from Phase 1 conclusions. The usable OS overhead metrics are `context-switches`, `cpu-migrations`, `page-faults`, elapsed time, user time, system time, `pidstat`, `vmstat`, and `sar` outputs.\n\n")
        else:
            f.write("### perf Hardware Counter Status\n\n")
            f.write("This run did not detect a complete hardware counter limitation. If cycles and instructions are available, they may be included as auxiliary metrics. They should still be interpreted carefully under VirtualBox.\n\n")

        if not tcp_limit["tcp_available"]:
            f.write("### TCP Baseline Limitation\n\n")
            f.write("No valid TCP JSON result was found. TCP baseline is unavailable for this run. This run should not be used to claim TCP versus Soft-RoCE performance differences.\n\n")
        elif tcp_limit["tcp_has_invalid"]:
            f.write("### TCP Baseline Limitation\n\n")
            f.write("The TCP baseline in this run is incomplete or partially invalid. At least one TCP result lacks throughput, retransmission, or CPU utilization values.\n\n")
            if tcp_limit["reason"]:
                f.write("Observed TCP issue:\n\n")
                f.write("```text\n")
                f.write(tcp_limit["reason"])
                f.write("\n```\n\n")
            f.write("As a result, this run should not be used to claim TCP versus Soft-RoCE performance differences unless a separate valid TCP fix result is used.\n\n")
        else:
            f.write("### TCP Baseline Status\n\n")
            f.write("TCP baseline contains valid iperf3 JSON results. These results are valid only as a VirtualBox Host-only TCP baseline, not as a physical Ethernet or hardware RDMA comparison.\n\n")

        f.write("## Data Validity Classification\n\n")

        f.write("### Reliable in this phase\n\n")
        f.write("- RDMA Verbs / Soft-RoCE 是否可在雙 VM 環境跑通。\n")
        f.write("- 同一 VirtualBox 環境下的相對趨勢。\n")
        f.write("- 不同 payload size 對 Soft-RoCE perftest 的相對趨勢。\n")
        f.write("- `context-switches`、`cpu-migrations`、`page-faults`、elapsed time、user time、system time。\n")
        f.write("- `pidstat`、`vmstat`、`sar` 原始輸出。\n\n")

        f.write("### Trend only\n\n")
        f.write("- latency / throughput 隨 payload size 的變化。\n")
        f.write("- CPU 使用率與 context switch 隨 workload 的變化。\n")
        f.write("- TCP 與 Soft-RoCE 的相對差異，前提是 TCP baseline 有效。\n\n")

        f.write("### Not comparable to hardware RDMA\n\n")
        f.write("- 絕對 RDMA latency。\n")
        f.write("- 絕對 RDMA throughput。\n")
        f.write("- RNIC hardware offload 效果。\n")
        f.write("- PCIe、switch、RoCE congestion control、多實體節點 scaling。\n")
        f.write("- VirtualBox guest 中不可用的 `cycles`、`instructions`、IPC。\n\n")

        f.write("## Conclusion\n\n")
        f.write("The final Phase 1 data should be used as Soft-RoCE simulation calibration data. It is suitable for functionality checks, relative trends, and protocol-level preparation for the next DSM phase. It should not be used as direct evidence of real hardware RDMA performance.\n")

    return report


def write_validation_json(path, validation, perf_limit, tcp_limit):
    data = {
        "validation": validation,
        "perf_limitations": perf_limit,
        "tcp_limitations": tcp_limit,
    }

    path.write_text(json.dumps(data, indent=2, ensure_ascii=False))


def main():
    if len(sys.argv) != 2:
        print("Usage:")
        print("  phase1_parse_results.py <phase1_final_result_dir>")
        sys.exit(1)

    root = Path(sys.argv[1]).expanduser().resolve()

    if not root.exists():
        print(f"Directory not found: {root}")
        sys.exit(1)

    processed = root / "processed"
    processed.mkdir(exist_ok=True)

    manifest = load_manifest(root)
    note = load_measurement_note(root)

    tcp_rows = collect_tcp_rows(root)
    rdma_rows = collect_rdma_rows(root)

    tcp_fields = [
        "file",
        "source_dir",
        "source_label",
        "direction",
        "valid",
        "error",
        "sender_throughput_mbps",
        "receiver_throughput_mbps",
        "retransmits",
        "sender_cpu_total_percent",
        "sender_cpu_user_percent",
        "sender_cpu_system_percent",
        "receiver_cpu_total_percent",
        "receiver_cpu_user_percent",
        "receiver_cpu_system_percent",
        "stderr_preview",
    ]

    rdma_fields = [
        "file",
        "test",
        "payload_size_bytes",
        "exit_code",
        "validity",
        "command",
        "cycles",
        "instructions",
        "cycles_supported",
        "instructions_supported",
        "context_switches",
        "cpu_migrations",
        "page_faults",
        "seconds_elapsed",
        "seconds_user",
        "seconds_sys",
        "stdout_parsed_metric",
        "stdout_parse_note",
        "stdout_preview",
        "stderr_preview",
    ]

    write_csv(processed / "tcp_summary.csv", tcp_rows, tcp_fields)
    write_csv(processed / "rdma_os_summary.csv", rdma_rows, rdma_fields)

    validation = validate_expected_tests(rdma_rows)
    perf_limit = detect_perf_limitations(rdma_rows)
    tcp_limit = detect_tcp_limitations(tcp_rows)

    write_validation_json(processed / "validation.json", validation, perf_limit, tcp_limit)

    report = write_markdown_report(root, processed, tcp_rows, rdma_rows, manifest, note)

    print(f"Wrote: {processed / 'tcp_summary.csv'}")
    print(f"Wrote: {processed / 'rdma_os_summary.csv'}")
    print(f"Wrote: {processed / 'validation.json'}")
    print(f"Wrote: {report}")

    if validation["invalid_65536_latency"]:
        print("WARNING: 65536-byte latency tests were found. These should be excluded from final Phase 1 latency analysis.")

    if validation["failed"]:
        print("WARNING: Some RDMA tests failed or timed out:")
        for test, size, exit_code, validity in validation["failed"]:
            print(f"  {test} size={size} exit={exit_code} validity={validity}")

    if perf_limit["hardware_counters_unusable"]:
        print("NOTE: perf hardware counters cycles/instructions are unavailable in this environment.")

    if tcp_limit["tcp_has_invalid"]:
        print("WARNING: TCP baseline is incomplete or invalid. Do not use this run for TCP-vs-Soft-RoCE comparison unless tcp_fix contains valid results.")


if __name__ == "__main__":
    main()
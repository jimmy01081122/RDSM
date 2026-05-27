#!/usr/bin/env bash
set -u

###############################################################################
# Phase 1 Final Measurement Script
#
# Purpose:
#   This script performs the final Phase 1 calibration measurements for a
#   VirtualBox + Ubuntu 22.04 + Soft-RoCE two-node environment.
#
# Phase 1 goal:
#   Establish a calibration methodology between Soft-RoCE simulation results
#   and real hardware RDMA expectations. The goal is not to claim hardware RDMA
#   performance, but to identify which measurements are reliable, which are only
#   useful as relative trends, and which are not comparable to real RDMA NICs.
#
# Important observation from prior runs:
#   In the current VirtualBox + Soft-RoCE environment, 65536-byte latency tests
#   repeatedly timed out with exit code 124. This was observed for latency-style
#   tests such as ib_write_lat, ib_read_lat, and ib_send_lat.
#
# Decision:
#   65536 bytes is excluded from latency tests and retained only for bandwidth
#   testing. Large payload latency under VirtualBox + Soft-RoCE is treated as
#   unstable for Phase 1 calibration and should not be used as primary latency
#   evidence.
#
# Final test plan:
#   Latency tests:
#     ib_write_lat, ib_read_lat, ib_send_lat
#     payload sizes: 64, 1024, 4096 bytes
#
#   Bandwidth tests:
#     ib_write_bw
#     payload sizes: 4096, 16384, 65536 bytes
#
# Data validity:
#   Reliable:
#     - Whether Soft-RoCE and RDMA Verbs can run in this two-VM setup.
#     - Relative trends under the same VirtualBox environment.
#     - OS overhead observed through perf, pidstat, vmstat, and sar.
#
#   Trend only:
#     - Latency and throughput changes across payload sizes.
#     - TCP vs Soft-RoCE relative comparison under the same VM environment.
#
#   Not comparable to hardware RDMA:
#     - Absolute RDMA latency.
#     - Absolute RDMA throughput.
#     - RNIC hardware offload effect.
#     - PCIe, switch, congestion control, and bare-metal cluster scalability.
###############################################################################

SERVER_HOST="${SERVER_HOST:-node1@192.168.56.101}"
SERVER_IP="${SERVER_IP:-192.168.56.101}"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/node2_to_node1}"

RDMA_DEV="${RDMA_DEV:-rxe0}"
RESULT_ROOT="${RESULT_ROOT:-$HOME/rdma-calibration/results/final}"
RUN_ID="${RUN_ID:-$(date +%Y%m%d_%H%M%S)}"
OUT_DIR="$RESULT_ROOT/phase1_final_$RUN_ID"

TCP_DURATION="${TCP_DURATION:-30}"
LAT_ITERS="${LAT_ITERS:-10000}"
BW_DURATION="${BW_DURATION:-10}"

CLIENT_TIMEOUT_SEC="${CLIENT_TIMEOUT_SEC:-300}"
SERVER_TIMEOUT_SEC="${SERVER_TIMEOUT_SEC:-420}"
SERVER_START_DELAY_SEC="${SERVER_START_DELAY_SEC:-2}"

# 65536 bytes is intentionally excluded from latency tests.
LAT_SIZES=(64 1024 4096)

# 65536 bytes is retained only for bandwidth testing.
BW_SIZES=(4096 16384 65536)

LAT_TESTS=(ib_write_lat ib_read_lat ib_send_lat)
BW_TESTS=(ib_write_bw)

mkdir -p "$OUT_DIR"
mkdir -p "$OUT_DIR/env"
mkdir -p "$OUT_DIR/tcp"
mkdir -p "$OUT_DIR/rdma_os"
mkdir -p "$OUT_DIR/server_raw"
mkdir -p "$OUT_DIR/notes"

ssh_cmd() {
  if [ -f "$SSH_KEY" ]; then
    ssh -i "$SSH_KEY" -o BatchMode=yes -o StrictHostKeyChecking=accept-new "$SERVER_HOST" "$@"
  else
    ssh -o StrictHostKeyChecking=accept-new "$SERVER_HOST" "$@"
  fi
}

stop_remote_perftest() {
  ssh_cmd "pkill -f ib_write_lat || true; pkill -f ib_read_lat || true; pkill -f ib_send_lat || true; pkill -f ib_write_bw || true"
}

stop_local_perftest() {
  pkill -f ib_write_lat || true
  pkill -f ib_read_lat || true
  pkill -f ib_send_lat || true
  pkill -f ib_write_bw || true
}

write_notes() {
  cat > "$OUT_DIR/notes/measurement_decision.md" <<'EOF'
# Phase 1 Measurement Decision

## Observation

During prior Phase 1 measurements under the VirtualBox + Ubuntu 22.04 + Soft-RoCE environment, 65536-byte latency tests repeatedly timed out with exit code 124.

The affected tests included latency-style RDMA perftest commands such as:

- ib_write_lat -s 65536
- ib_read_lat -s 65536
- ib_send_lat -s 65536

This behavior indicates that large-payload latency tests are unstable in the current software RDMA simulation environment when combined with VirtualBox virtualization and OS overhead monitoring.

## Decision

65536-byte payload is excluded from latency measurements and retained only for bandwidth measurement.

Final Phase 1 measurement plan:

Latency tests:

- ib_write_lat
- ib_read_lat
- ib_send_lat

Latency payload sizes:

- 64 bytes
- 1024 bytes
- 4096 bytes

Bandwidth test:

- ib_write_bw

Bandwidth payload sizes:

- 4096 bytes
- 16384 bytes
- 65536 bytes

## Interpretation

This adjustment is not an attempt to hide failed results. It is part of the calibration process. The purpose of Phase 1 is to determine which measurements are reliable and useful under the Soft-RoCE simulation environment.

The 65536-byte latency timeout is treated as evidence that large-payload latency data is not reliable in the current setup.

## Validity Boundary

The resulting data should be interpreted as:

- Valid for Soft-RoCE functionality checks.
- Useful for relative trend analysis within the same VM environment.
- Useful for comparing TCP baseline and Soft-RoCE baseline under identical VirtualBox conditions.
- Not valid as evidence of real hardware RDMA latency or throughput.

Absolute RDMA latency, RNIC offload behavior, PCIe effects, switch behavior, and real cluster scalability remain outside the scope of this Phase 1 measurement.
EOF
}

collect_env_local() {
  local out="$OUT_DIR/env/node2_env.txt"

  {
    echo "hostname: $(hostname)"
    echo "date: $(date -Iseconds)"
    echo "role: client"
    echo "server_host: $SERVER_HOST"
    echo "server_ip: $SERVER_IP"
    echo "rdma_dev: $RDMA_DEV"
    echo

    echo "===== OS ====="
    lsb_release -a 2>/dev/null || cat /etc/os-release
    echo

    echo "===== Kernel ====="
    uname -a
    echo

    echo "===== CPU ====="
    lscpu
    echo

    echo "===== Memory ====="
    free -h
    echo

    echo "===== Network ====="
    ip -br addr
    ip route
    echo

    echo "===== RDMA ====="
    rdma link show 2>&1 || true
    ibv_devices 2>&1 || true
    ibv_devinfo 2>&1 || true
    echo

    echo "===== Modules ====="
    lsmod | grep -E 'rdma|ib_|rxe' || true
    echo

    echo "===== Tools ====="
    iperf3 --version 2>/dev/null | head -n 1 || true
    perf --version 2>/dev/null || true
    gcc --version 2>/dev/null | head -n 1 || true
    cmake --version 2>/dev/null | head -n 1 || true
    python3 --version 2>/dev/null || true
  } > "$out" 2>&1
}

collect_env_remote() {
  ssh_cmd "mkdir -p '$OUT_DIR/env'; {
    echo 'hostname:' \$(hostname)
    echo 'date:' \$(date -Iseconds)
    echo 'role: server'
    echo 'rdma_dev: $RDMA_DEV'
    echo

    echo '===== OS ====='
    lsb_release -a 2>/dev/null || cat /etc/os-release
    echo

    echo '===== Kernel ====='
    uname -a
    echo

    echo '===== CPU ====='
    lscpu
    echo

    echo '===== Memory ====='
    free -h
    echo

    echo '===== Network ====='
    ip -br addr
    ip route
    echo

    echo '===== RDMA ====='
    rdma link show 2>&1 || true
    ibv_devices 2>&1 || true
    ibv_devinfo 2>&1 || true
    echo

    echo '===== Modules ====='
    lsmod | grep -E 'rdma|ib_|rxe' || true
    echo

    echo '===== Tools ====='
    iperf3 --version 2>/dev/null | head -n 1 || true
    perf --version 2>/dev/null || true
    gcc --version 2>/dev/null | head -n 1 || true
    cmake --version 2>/dev/null | head -n 1 || true
    python3 --version 2>/dev/null || true
  } > '$OUT_DIR/env/node1_env.txt' 2>&1"
}

start_remote_iperf3_server() {
  ssh_cmd "pkill -f 'iperf3 -s' || true; nohup iperf3 -s > '$OUT_DIR/tcp/node1_iperf3_server.log' 2>&1 & echo \$!"
}

stop_remote_iperf3_server() {
  ssh_cmd "pkill -f 'iperf3 -s' || true"
}

run_tcp_tests() {
  echo "[$(date -Iseconds)] TCP baseline start"

  start_remote_iperf3_server >/dev/null
  sleep 2

  iperf3 -c "$SERVER_IP" -t "$TCP_DURATION" -J \
    > "$OUT_DIR/tcp/tcp_node2_to_node1.json" \
    2> "$OUT_DIR/tcp/tcp_node2_to_node1.stderr.txt"

  iperf3 -c "$SERVER_IP" -t "$TCP_DURATION" -R -J \
    > "$OUT_DIR/tcp/tcp_node1_to_node2_reverse.json" \
    2> "$OUT_DIR/tcp/tcp_node1_to_node2_reverse.stderr.txt"

  stop_remote_iperf3_server

  echo "[$(date -Iseconds)] TCP baseline done"
}

remote_server_cmd_for() {
  local test_name="$1"
  local size="$2"

  case "$test_name" in
    ib_write_lat)
      echo "timeout $SERVER_TIMEOUT_SEC ib_write_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS"
      ;;
    ib_read_lat)
      echo "timeout $SERVER_TIMEOUT_SEC ib_read_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS"
      ;;
    ib_send_lat)
      echo "timeout $SERVER_TIMEOUT_SEC ib_send_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS"
      ;;
    ib_write_bw)
      echo "timeout $SERVER_TIMEOUT_SEC ib_write_bw -d $RDMA_DEV -F -s $size -D $BW_DURATION"
      ;;
    *)
      echo "unknown"
      ;;
  esac
}

client_cmd_for() {
  local test_name="$1"
  local size="$2"

  case "$test_name" in
    ib_write_lat)
      echo "ib_write_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS $SERVER_IP"
      ;;
    ib_read_lat)
      echo "ib_read_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS $SERVER_IP"
      ;;
    ib_send_lat)
      echo "ib_send_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS $SERVER_IP"
      ;;
    ib_write_bw)
      echo "ib_write_bw -d $RDMA_DEV -F -s $size -D $BW_DURATION $SERVER_IP"
      ;;
    *)
      echo "unknown"
      ;;
  esac
}

write_rdmadata_json() {
  local json_file="$1"
  local test_name="$2"
  local size="$3"
  local cmd="$4"
  local start_time="$5"
  local end_time="$6"
  local exit_code="$7"
  local stdout_file="$8"
  local stderr_file="$9"
  local perf_file="${10}"

  python3 - "$json_file" "$test_name" "$size" "$cmd" "$start_time" "$end_time" "$exit_code" "$stdout_file" "$stderr_file" "$perf_file" "$SERVER_IP" <<'PY'
import json
import socket
import sys
from pathlib import Path

json_file, test_name, size, cmd, start_time, end_time, exit_code, stdout_file, stderr_file, perf_file, server_ip = sys.argv[1:]

def read(path):
    p = Path(path)
    if not p.exists():
        return ""
    return p.read_text(errors="replace")

data = {
    "role": "client",
    "hostname": socket.gethostname(),
    "server_ip": server_ip,
    "test": test_name,
    "payload_size_bytes": int(size),
    "command": cmd,
    "start_time": start_time,
    "end_time": end_time,
    "exit_code": int(exit_code),
    "stdout": read(stdout_file),
    "stderr": read(stderr_file),
    "perf_stat": read(perf_file),
}
Path(json_file).write_text(json.dumps(data, indent=2, ensure_ascii=False))
PY
}

run_one_rdma_os_test() {
  local test_name="$1"
  local size="$2"

  local base="${test_name}_s${size}"
  local local_dir="$OUT_DIR/rdma_os/$base"
  mkdir -p "$local_dir"

  local server_stdout="$OUT_DIR/server_raw/${base}.server.stdout.txt"
  local server_stderr="$OUT_DIR/server_raw/${base}.server.stderr.txt"
  local server_exit="$OUT_DIR/server_raw/${base}.server.exit"

  local client_stdout="$local_dir/benchmark.stdout.txt"
  local client_stderr="$local_dir/benchmark.stderr.txt"
  local perf_stderr="$local_dir/perf.stderr.txt"
  local pidstat_out="$local_dir/pidstat.txt"
  local vmstat_out="$local_dir/vmstat.txt"
  local sar_cpu_out="$local_dir/sar_cpu.txt"
  local sar_net_out="$local_dir/sar_net.txt"
  local json_out="$local_dir/result.json"
  local command_file="$local_dir/command.txt"

  local server_cmd
  server_cmd="$(remote_server_cmd_for "$test_name" "$size")"

  local client_cmd
  client_cmd="$(client_cmd_for "$test_name" "$size")"

  echo "$client_cmd" > "$command_file"

  echo "[$(date -Iseconds)] RDMA OS start: $test_name size=$size"

  stop_remote_perftest
  stop_local_perftest
  sleep 1

  ssh_cmd "mkdir -p '$OUT_DIR/server_raw'; rm -f '$server_exit'; nohup bash -lc '$server_cmd > \"$server_stdout\" 2> \"$server_stderr\"; echo \$? > \"$server_exit\"' >/dev/null 2>&1 & echo \$!" >/dev/null

  sleep "$SERVER_START_DELAY_SEC"

  pidstat -u -w -r 1 > "$pidstat_out" 2>&1 &
  local pidstat_pid=$!

  vmstat 1 > "$vmstat_out" 2>&1 &
  local vmstat_pid=$!

  sar -u 1 > "$sar_cpu_out" 2>&1 &
  local sar_cpu_pid=$!

  sar -n DEV 1 > "$sar_net_out" 2>&1 &
  local sar_net_pid=$!

  local start_time
  local end_time
  local exit_code

  start_time="$(date -Iseconds)"

  timeout "$CLIENT_TIMEOUT_SEC" \
    perf stat -e cycles,instructions,context-switches,cpu-migrations,page-faults \
    -- bash -lc "$client_cmd" \
    > "$client_stdout" \
    2> >(tee "$perf_stderr" "$client_stderr" >/dev/null)

  exit_code=$?
  end_time="$(date -Iseconds)"

  kill "$pidstat_pid" "$vmstat_pid" "$sar_cpu_pid" "$sar_net_pid" 2>/dev/null || true
  sleep 1
  kill -9 "$pidstat_pid" "$vmstat_pid" "$sar_cpu_pid" "$sar_net_pid" 2>/dev/null || true

  stop_remote_perftest
  stop_local_perftest

  write_rdmadata_json "$json_out" "$test_name" "$size" "$client_cmd" "$start_time" "$end_time" "$exit_code" "$client_stdout" "$client_stderr" "$perf_stderr"

  echo "[$(date -Iseconds)] RDMA OS done: $test_name size=$size exit=$exit_code"

  if [ "$exit_code" -ne 0 ]; then
    echo "WARN: failed or timed out: $test_name size=$size"
    echo "Check: $local_dir"
  fi

  echo
}

run_rdma_os_tests() {
  echo "[$(date -Iseconds)] RDMA OS overhead tests start"

  echo "Latency tests:"
  echo "  tests: ${LAT_TESTS[*]}"
  echo "  sizes: ${LAT_SIZES[*]}"
  echo "Bandwidth tests:"
  echo "  tests: ${BW_TESTS[*]}"
  echo "  sizes: ${BW_SIZES[*]}"
  echo "Note: 65536 bytes is intentionally excluded from latency tests."
  echo

  for size in "${LAT_SIZES[@]}"; do
    for test_name in "${LAT_TESTS[@]}"; do
      run_one_rdma_os_test "$test_name" "$size"
    done
  done

  for size in "${BW_SIZES[@]}"; do
    for test_name in "${BW_TESTS[@]}"; do
      run_one_rdma_os_test "$test_name" "$size"
    done
  done

  echo "[$(date -Iseconds)] RDMA OS overhead tests done"
}

write_manifest() {
  cat > "$OUT_DIR/manifest.json" <<EOF
{
  "run_id": "$RUN_ID",
  "date": "$(date -Iseconds)",
  "server_host": "$SERVER_HOST",
  "server_ip": "$SERVER_IP",
  "rdma_dev": "$RDMA_DEV",
  "tcp_duration_sec": $TCP_DURATION,
  "lat_iters": $LAT_ITERS,
  "bw_duration_sec": $BW_DURATION,
  "client_timeout_sec": $CLIENT_TIMEOUT_SEC,
  "server_timeout_sec": $SERVER_TIMEOUT_SEC,
  "latency_tests": ["ib_write_lat", "ib_read_lat", "ib_send_lat"],
  "latency_sizes": [$(IFS=,; echo "${LAT_SIZES[*]}")],
  "bandwidth_tests": ["ib_write_bw"],
  "bandwidth_sizes": [$(IFS=,; echo "${BW_SIZES[*]}")],
  "excluded_from_latency": [65536],
  "exclusion_reason": "65536-byte latency tests repeatedly timed out with exit code 124 under VirtualBox + Soft-RoCE. The value is retained only for bandwidth testing.",
  "output_dir": "$OUT_DIR"
}
EOF
}

echo "Output directory: $OUT_DIR"
echo "Server host: $SERVER_HOST"
echo "Server IP: $SERVER_IP"
echo "RDMA device: $RDMA_DEV"
echo

write_manifest
write_notes

echo "[$(date -Iseconds)] Collecting environment"
collect_env_local
collect_env_remote

echo "[$(date -Iseconds)] Checking SSH and remote RDMA"
ssh_cmd "hostname && rdma link show && ibv_devices" > "$OUT_DIR/env/node1_connectivity_check.txt" 2>&1

echo "[$(date -Iseconds)] Checking local RDMA"
{
  hostname
  rdma link show
  ibv_devices
} > "$OUT_DIR/env/node2_connectivity_check.txt" 2>&1

run_tcp_tests
run_rdma_os_tests

echo "Phase 1 final measurement completed."
echo "Result directory:"
echo "$OUT_DIR"
echo
echo "Measurement decision note:"
echo "$OUT_DIR/notes/measurement_decision.md"
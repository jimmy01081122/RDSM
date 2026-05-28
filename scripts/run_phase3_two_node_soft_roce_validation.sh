#!/usr/bin/env bash
set -u

SERVER_HOST="${SERVER_HOST:-node1@192.168.56.101}"
SERVER_IP="${SERVER_IP:-192.168.56.101}"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/node2_to_node1}"
RDMA_DEV="${RDMA_DEV:-rxe0}"

RESULTS_DIR="${RESULTS_DIR:-./results/phase3}"
RUN_ID="${RUN_ID:-$(date +%Y%m%d_%H%M%S)}"
OUT_DIR="$RESULTS_DIR/two_node_soft_roce_$RUN_ID"
SERVER_OUT_DIR="$OUT_DIR/server"
CLIENT_OUT_DIR="$OUT_DIR/client"

SIZES_STR="${SIZES:-8 64 256 1024 4096 16384 65536}"
TESTS_STR="${TESTS:-ib_write_lat ib_read_lat ib_send_lat ib_write_bw}"
LAT_ITERS="${LAT_ITERS:-1000}"
BW_DURATION="${BW_DURATION:-3}"
CLIENT_TIMEOUT_SEC="${CLIENT_TIMEOUT_SEC:-120}"
SERVER_TIMEOUT_SEC="${SERVER_TIMEOUT_SEC:-150}"
SERVER_START_DELAY_SEC="${SERVER_START_DELAY_SEC:-2}"

mkdir -p "$CLIENT_OUT_DIR" "$SERVER_OUT_DIR"

ssh_cmd() {
  if [ -f "$SSH_KEY" ]; then
    ssh -i "$SSH_KEY" -o BatchMode=yes -o StrictHostKeyChecking=accept-new "$SERVER_HOST" "$@"
  else
    ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new "$SERVER_HOST" "$@"
  fi
}

write_json() {
  local json_file="$1"
  local test_name="$2"
  local size="$3"
  local cmd="$4"
  local start_time="$5"
  local end_time="$6"
  local exit_code="$7"
  local stdout_file="$8"
  local stderr_file="$9"

  python3 - "$json_file" "$test_name" "$size" "$cmd" "$start_time" "$end_time" "$exit_code" "$stdout_file" "$stderr_file" "$SERVER_IP" "$RDMA_DEV" <<'PY'
import json
import socket
import sys
from pathlib import Path

json_file, test_name, size, cmd, start_time, end_time, exit_code, stdout_file, stderr_file, server_ip, rdma_dev = sys.argv[1:]

def read_file(path):
    p = Path(path)
    if not p.exists():
        return ""
    return p.read_text(errors="replace")

data = {
    "phase": "phase3_two_node_soft_roce_validation",
    "role": "client",
    "client_hostname": socket.gethostname(),
    "server_ip": server_ip,
    "rdma_device": rdma_dev,
    "test": test_name,
    "payload_size_bytes": int(size),
    "command": cmd,
    "start_time": start_time,
    "end_time": end_time,
    "exit_code": int(exit_code),
    "stdout": read_file(stdout_file),
    "stderr": read_file(stderr_file),
}
Path(json_file).write_text(json.dumps(data, indent=2, ensure_ascii=False))
PY
}

collect_env() {
  {
    echo "hostname: $(hostname)"
    echo "date: $(date -Iseconds)"
    echo "server_host: $SERVER_HOST"
    echo "server_ip: $SERVER_IP"
    echo "rdma_dev: $RDMA_DEV"
    echo
    echo "===== ip ====="
    ip -br addr
    echo
    echo "===== rdma link ====="
    rdma link show 2>&1 || true
    echo
    echo "===== ibv_devices ====="
    ibv_devices 2>&1 || true
    echo
    echo "===== ibv_devinfo ====="
    ibv_devinfo 2>&1 || true
  } > "$OUT_DIR/client_env.txt" 2>&1

  ssh_cmd "mkdir -p '$SERVER_OUT_DIR' && {
    echo 'hostname:' \$(hostname)
    echo 'date:' \$(date -Iseconds)
    echo 'rdma_dev: $RDMA_DEV'
    echo
    echo '===== ip ====='
    ip -br addr
    echo
    echo '===== rdma link ====='
    rdma link show 2>&1 || true
    echo
    echo '===== ibv_devices ====='
    ibv_devices 2>&1 || true
    echo
    echo '===== ibv_devinfo ====='
    ibv_devinfo 2>&1 || true
  }" > "$OUT_DIR/server_env.txt" 2>&1
}

stop_server_processes() {
  ssh_cmd "pkill -f ibv_rc_pingpong || true; pkill -f ib_write_lat || true; pkill -f ib_read_lat || true; pkill -f ib_send_lat || true; pkill -f ib_write_bw || true; pkill -f ib_read_bw || true"
}

server_command() {
  local test_name="$1"
  local size="$2"
  case "$test_name" in
    ibv_rc_pingpong) echo "timeout $SERVER_TIMEOUT_SEC ibv_rc_pingpong -d $RDMA_DEV -g 1 -s $size -n $LAT_ITERS" ;;
    ib_write_lat) echo "timeout $SERVER_TIMEOUT_SEC ib_write_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS" ;;
    ib_read_lat) echo "timeout $SERVER_TIMEOUT_SEC ib_read_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS" ;;
    ib_send_lat) echo "timeout $SERVER_TIMEOUT_SEC ib_send_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS" ;;
    ib_write_bw) echo "timeout $SERVER_TIMEOUT_SEC ib_write_bw -d $RDMA_DEV -F -s $size -D $BW_DURATION" ;;
    ib_read_bw) echo "timeout $SERVER_TIMEOUT_SEC ib_read_bw -d $RDMA_DEV -F -s $size -D $BW_DURATION" ;;
    *) echo "unknown" ;;
  esac
}

client_command() {
  local test_name="$1"
  local size="$2"
  case "$test_name" in
    ibv_rc_pingpong) echo "ibv_rc_pingpong -d $RDMA_DEV -g 1 -s $size -n $LAT_ITERS $SERVER_IP" ;;
    ib_write_lat) echo "ib_write_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS $SERVER_IP" ;;
    ib_read_lat) echo "ib_read_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS $SERVER_IP" ;;
    ib_send_lat) echo "ib_send_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS $SERVER_IP" ;;
    ib_write_bw) echo "ib_write_bw -d $RDMA_DEV -F -s $size -D $BW_DURATION $SERVER_IP" ;;
    ib_read_bw) echo "ib_read_bw -d $RDMA_DEV -F -s $size -D $BW_DURATION $SERVER_IP" ;;
    *) echo "unknown" ;;
  esac
}

run_one() {
  local test_name="$1"
  local size="$2"
  local base="${test_name}_s${size}"
  local stdout_file="$CLIENT_OUT_DIR/${base}.stdout.txt"
  local stderr_file="$CLIENT_OUT_DIR/${base}.stderr.txt"
  local json_file="$CLIENT_OUT_DIR/${base}.json"
  local server_stdout="$SERVER_OUT_DIR/${base}.stdout.txt"
  local server_stderr="$SERVER_OUT_DIR/${base}.stderr.txt"
  local server_exit="$SERVER_OUT_DIR/${base}.exit"
  local server_cmd
  local cmd

  server_cmd="$(server_command "$test_name" "$size")"
  cmd="$(client_command "$test_name" "$size")"
  if [ "$server_cmd" = "unknown" ] || [ "$cmd" = "unknown" ]; then
    echo "Unknown test: $test_name" >&2
    return 2
  fi

  echo "[$(date -Iseconds)] START $test_name size=$size"
  stop_server_processes
  sleep 1
  ssh_cmd "mkdir -p '$SERVER_OUT_DIR'; rm -f '$server_exit'; nohup bash -lc '$server_cmd > \"$server_stdout\" 2> \"$server_stderr\"; echo \$? > \"$server_exit\"' >/dev/null 2>&1 & echo \$!" > "$CLIENT_OUT_DIR/${base}.server_pid.txt"
  sleep "$SERVER_START_DELAY_SEC"

  local start_time
  local end_time
  local exit_code
  start_time="$(date -Iseconds)"
  timeout "$CLIENT_TIMEOUT_SEC" bash -lc "$cmd" > "$stdout_file" 2> "$stderr_file"
  exit_code=$?
  end_time="$(date -Iseconds)"
  stop_server_processes

  write_json "$json_file" "$test_name" "$size" "$cmd" "$start_time" "$end_time" "$exit_code" "$stdout_file" "$stderr_file"
  echo "[$(date -Iseconds)] DONE $test_name size=$size exit=$exit_code"
  echo
}

cat > "$OUT_DIR/manifest.json" <<EOF
{
  "phase": "phase3",
  "run_id": "$RUN_ID",
  "server_host": "$SERVER_HOST",
  "server_ip": "$SERVER_IP",
  "rdma_device": "$RDMA_DEV",
  "sizes": "$SIZES_STR",
  "tests": "$TESTS_STR",
  "lat_iters": $LAT_ITERS,
  "bw_duration_sec": $BW_DURATION
}
EOF

echo "Phase 3 output directory: $OUT_DIR"
collect_env

for size in $SIZES_STR; do
  for test_name in $TESTS_STR; do
    run_one "$test_name" "$size"
  done
done

echo "All Phase 3 two-node Soft-RoCE validation tests finished."
echo "Results: $OUT_DIR"

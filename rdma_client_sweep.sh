#!/usr/bin/env bash
set -u

SERVER_HOST="${SERVER_HOST:-node1@192.168.56.101}"
SERVER_IP="${SERVER_IP:-192.168.56.101}"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/node2_to_node1}"

RDMA_DEV="${RDMA_DEV:-rxe0}"
RESULT_ROOT="${RESULT_ROOT:-$HOME/rdma-calibration/results/raw}"
RUN_ID="${RUN_ID:-$(date +%Y%m%d_%H%M%S)}"

CLIENT_OUT_DIR="$RESULT_ROOT/orchestrated_client_$RUN_ID"
SERVER_OUT_DIR="$RESULT_ROOT/orchestrated_server_$RUN_ID"

SIZES=(8 64 256 1024 4096 16384 65536)

LAT_ITERS="${LAT_ITERS:-10000}"
BW_DURATION="${BW_DURATION:-10}"

CLIENT_TIMEOUT_SEC="${CLIENT_TIMEOUT_SEC:-180}"
SERVER_TIMEOUT_SEC="${SERVER_TIMEOUT_SEC:-240}"
SERVER_START_DELAY_SEC="${SERVER_START_DELAY_SEC:-2}"

mkdir -p "$CLIENT_OUT_DIR"

ssh_cmd() {
  if [ -f "$SSH_KEY" ]; then
    ssh -i "$SSH_KEY" -o BatchMode=yes -o StrictHostKeyChecking=accept-new "$SERVER_HOST" "$@"
  else
    ssh -o StrictHostKeyChecking=accept-new "$SERVER_HOST" "$@"
  fi
}

write_client_json() {
  local json_file="$1"
  local test_name="$2"
  local size="$3"
  local cmd="$4"
  local start_time="$5"
  local end_time="$6"
  local exit_code="$7"
  local stdout_file="$8"
  local stderr_file="$9"

  python3 - "$json_file" "$test_name" "$size" "$cmd" "$start_time" "$end_time" "$exit_code" "$stdout_file" "$stderr_file" "$SERVER_IP" <<'PY'
import json
import socket
import sys
from pathlib import Path

json_file, test_name, size, cmd, start_time, end_time, exit_code, stdout_file, stderr_file, server_ip = sys.argv[1:]

def read_file(path):
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
    "stdout": read_file(stdout_file),
    "stderr": read_file(stderr_file),
}
Path(json_file).write_text(json.dumps(data, indent=2, ensure_ascii=False))
PY
}

collect_local_env() {
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
  } > "$CLIENT_OUT_DIR/client_env.txt" 2>&1
}

collect_server_env() {
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
  } > '$SERVER_OUT_DIR/server_env.txt' 2>&1"
}

start_server() {
  local test_name="$1"
  local size="$2"

  local server_cmd=""
  local base="${test_name}_s${size}"
  local stdout_file="$SERVER_OUT_DIR/${base}.stdout.txt"
  local stderr_file="$SERVER_OUT_DIR/${base}.stderr.txt"
  local exit_file="$SERVER_OUT_DIR/${base}.exit"

  case "$test_name" in
    ib_write_lat)
      server_cmd="timeout $SERVER_TIMEOUT_SEC ib_write_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS"
      ;;
    ib_read_lat)
      server_cmd="timeout $SERVER_TIMEOUT_SEC ib_read_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS"
      ;;
    ib_send_lat)
      server_cmd="timeout $SERVER_TIMEOUT_SEC ib_send_lat -d $RDMA_DEV -F -s $size -n $LAT_ITERS"
      ;;
    ib_write_bw)
      server_cmd="timeout $SERVER_TIMEOUT_SEC ib_write_bw -d $RDMA_DEV -F -s $size -D $BW_DURATION"
      ;;
    *)
      echo "Unknown test: $test_name"
      exit 1
      ;;
  esac

  ssh_cmd "mkdir -p '$SERVER_OUT_DIR'; rm -f '$exit_file'; nohup bash -lc '$server_cmd > \"$stdout_file\" 2> \"$stderr_file\"; echo \$? > \"$exit_file\"' >/dev/null 2>&1 & echo \$!"
}

stop_server_processes() {
  ssh_cmd "pkill -f ib_write_lat || true; pkill -f ib_read_lat || true; pkill -f ib_send_lat || true; pkill -f ib_write_bw || true"
}

run_client_test() {
  local test_name="$1"
  local size="$2"

  local cmd=()
  local base="${test_name}_s${size}"
  local stdout_file="$CLIENT_OUT_DIR/${base}.stdout.txt"
  local stderr_file="$CLIENT_OUT_DIR/${base}.stderr.txt"
  local json_file="$CLIENT_OUT_DIR/${base}.json"

  case "$test_name" in
    ib_write_lat)
      cmd=(ib_write_lat -d "$RDMA_DEV" -F -s "$size" -n "$LAT_ITERS" "$SERVER_IP")
      ;;
    ib_read_lat)
      cmd=(ib_read_lat -d "$RDMA_DEV" -F -s "$size" -n "$LAT_ITERS" "$SERVER_IP")
      ;;
    ib_send_lat)
      cmd=(ib_send_lat -d "$RDMA_DEV" -F -s "$size" -n "$LAT_ITERS" "$SERVER_IP")
      ;;
    ib_write_bw)
      cmd=(ib_write_bw -d "$RDMA_DEV" -F -s "$size" -D "$BW_DURATION" "$SERVER_IP")
      ;;
    *)
      echo "Unknown test: $test_name"
      exit 1
      ;;
  esac

  echo "[$(date -Iseconds)] START $test_name size=$size"

  stop_server_processes
  sleep 1

  local server_pid
  server_pid="$(start_server "$test_name" "$size")"

  echo "[$(date -Iseconds)] server pid=$server_pid"
  sleep "$SERVER_START_DELAY_SEC"

  local start_time
  local end_time
  local exit_code

  start_time="$(date -Iseconds)"
  timeout "$CLIENT_TIMEOUT_SEC" "${cmd[@]}" > "$stdout_file" 2> "$stderr_file"
  exit_code=$?
  end_time="$(date -Iseconds)"

  stop_server_processes

  write_client_json \
    "$json_file" \
    "$test_name" \
    "$size" \
    "${cmd[*]}" \
    "$start_time" \
    "$end_time" \
    "$exit_code" \
    "$stdout_file" \
    "$stderr_file"

  echo "[$(date -Iseconds)] DONE $test_name size=$size exit=$exit_code"

  if [ "$exit_code" -ne 0 ]; then
    echo "WARN: $test_name size=$size failed or timed out. Check:"
    echo "  $stdout_file"
    echo "  $stderr_file"
  fi

  echo
}

echo "Client output directory: $CLIENT_OUT_DIR"
echo "Server output directory on node1: $SERVER_OUT_DIR"
echo "Server host: $SERVER_HOST"
echo "Server IP: $SERVER_IP"
echo

collect_local_env
collect_server_env

for size in "${SIZES[@]}"; do
  run_client_test "ib_write_lat" "$size"
  run_client_test "ib_read_lat" "$size"
  run_client_test "ib_send_lat" "$size"
  run_client_test "ib_write_bw" "$size"
done

echo "All tests finished."
echo "Client results: $CLIENT_OUT_DIR"
echo "Server results on node1: $SERVER_OUT_DIR"
# RDSM: RDMA-based Distributed Shared Memory

A high-performance distributed shared memory system with Optimistic Concurrency Control (OCC) and RDMA support.

## Project Overview

This project implements a Phase 2 DSM (Distributed Shared Memory) benchmark system with multiple OCC algorithms:
- **baseline_occ**: Standard Optimistic Concurrency Control
- **backoff_occ**: OCC with backoff strategies (NO_BACKOFF, FIXED_BACKOFF, EXPONENTIAL_BACKOFF, CONTENTION_AWARE_BACKOFF)
- **hot_detection_occ**: OCC with hot data detection
- **hybrid_arbitration_occ**: Server-based arbitration hybrid approach

## Requirements

- CMake 3.10+
- C++17 compiler (g++ 11+)
- RDMA libraries:
  - libibverbs (1.14+)
  - librdmacm (1.3+)
- Threads library
- Python 3 (for result parsing)

### Install Dependencies (Ubuntu/Debian)

```bash
sudo apt-get install -y cmake build-essential
sudo apt-get install -y libibverbs-dev librdmacm-dev
```

## Building

```bash
cd RDSM
mkdir build
cd build
cmake ..
make -j4
```

The compiled executable will be at `build/phase2_dsm_benchmark`.

## Running

### Basic Usage

```bash
./build/phase2_dsm_benchmark [options]
```

### Common Options

- `--products N`: Number of products (default: 100)
- `--users N`: Number of users (default: 10)
- `--threads N`: Number of threads
- `--write-ratio RATIO`: Write percentage (0.0-1.0)
- `--access-pattern`: uniform, zipfian_0.8, or zipfian_0.99
- `--duration-sec N`: Benchmark duration in seconds
- `--algorithm`: Algorithm to test
- `--backoff-policy`: Backoff strategy (if applicable)
- `--output-file FILE`: Output metrics to JSON file

### Example

```bash
./build/phase2_dsm_benchmark \
  --products 1000 \
  --users 100 \
  --threads 8 \
  --write-ratio 0.5 \
  --access-pattern zipfian_0.8 \
  --duration-sec 60 \
  --algorithm hot_detection_occ \
  --output-file results.json
```

## Project Structure

```
RDSM/
├── src/                 # Core implementation
│   ├── rdma_conn.*     # RDMA connection handling
│   ├── dsm_object.*    # Distributed shared memory objects
│   ├── occ_engine.*    # OCC transaction engine
│   ├── backoff.*       # Backoff strategies
│   ├── hot_detection.* # Hot data detection
│   └── server_arbitration.* # Server arbitration logic
├── include/            # Header files
│   └── json_utils.h    # JSON output utilities
├── experiments/        # Benchmark drivers
├── scripts/            # Utility scripts
└── stat/               # Historical results and data
```

## Scripts

- `scripts/run_phase2_experiments.sh`: Run full experiment suite
- `scripts/parse_phase2_results.py`: Parse and aggregate results
- `phase1_final_measure.sh`: Phase 1 calibration measurements
- `phase1_parse_results.py`: Phase 1 result parsing

## Performance Analysis

Results are generated in JSON format containing:
- Transaction throughput
- Latency metrics
- Contention-aware performance data
- Algorithm comparison metrics

Use `scripts/parse_phase2_results.py` to aggregate and analyze results.

## License

Research project - see project documentation for terms.

## Authors

NTU Research Team

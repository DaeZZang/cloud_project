# PrediCache

This repository contains the PrediCache prototype for the paper "Predictive Translation: High-Performance Buffer Management Without the Trade-Offs"  (accepted at SIGMOD 2026).

## Building and Running the Prototype

The prototype can be built using `make` and run by executing the built `predicache` binary.

## Configuration

The prototype can be configured using the following environment variables:

- BLOCK: storage block device (e.g. /dev/nvme0n1 or /dev/md0); default=/tmp/bm
- POOLGB: size of the buffer pool in GB; default=1
- BATCH: batch size for eviction in pages; default=64
- RUNFOR: benchmark run duration in seconds; default=30
- WORKLOAD: one of 'tpc-c', 'random-read', 'ycsb'; default='tpc-c'
- THREADS: number of threads; default=1
- DATASIZE: number of warehouses for TPC-C, number of tuples for random read/ycsb benchmark; default=10

### YCSB Specific Configuration
The payload size is fixed at 120 bytes and can only be changed by modifying the source code.

- ZIPF_FACTOR: zipf factor for YCSB workload, default=0.9
- OPS_PER_TX: number of operations per transaction for YCSB workload, default=1
- READ_RATIO: read ratio for YCSB workload, default=100 (100% reads)

## Example Usage

To run the prototype with a TPC-C workload with 100 warehouses using 4 threads and a 2GB buffer pool, you can use the following command:

```bash
make
BLOCK=/dev/nvme0n1 POOLGB=2 THREADS=4 WORKLOAD=tpc-c DATASIZE=100 ./predicache
```

To run the prototype with a random read workload with 1000000 entries on a 128GB buffer pool using 64 threads, you can use the following command:

```bash
make
BLOCK=/dev/nvme0n1 POOLGB=128 THREADS=64 WORKLOAD=random-read DATASIZE=1000000 ./predicache
```

## Dependencies
The prototype requires the following dependencies:
- `gcc` for compilation (clang might work but wasn't tested)
- `make` for building the project
- `libaio-dev` for asynchronous I/O operations
- `liburing-dev` for IO-uing support (optional, build with `make uring`)
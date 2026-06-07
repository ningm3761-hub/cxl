# CXL NUMA CSMA-like Request Injection Project

## 1. Purpose

This project evaluates Random, Basic CSMA-like, and AIMD-based request injection policies on a QEMU-provided Linux NUMA topology.

Compared with the earlier single-file simulation, this version uses the real Linux NUMA nodes visible in the guest:

```bash
numactl -H
```

The test memory buffer is allocated on `mem_node`, normally node1:

```c
numa_alloc_onnode(size, mem_node)
```

Worker threads are optionally pinned to `cpu_node`, normally node0.

## 2. Build

Install dependencies:

```bash
sudo apt update
sudo apt install -y build-essential libnuma-dev numactl python3 python3-pandas python3-matplotlib
```

Build:

```bash
make
```

Smoke test:

```bash
make run-smoke
```

## 3. Run a single experiment

Format:

```bash
./cxl_numa_csma <mode> <load> <threads> <seconds> [mem_node] [cpu_node] [seed] [mem_mb] [touches_per_req]
```

Examples:

```bash
./cxl_numa_csma 0 50 4 5 1 0 1 512 4096
./cxl_numa_csma 1 50 4 5 1 0 1 512 4096
./cxl_numa_csma 2 50 4 5 1 0 1 512 4096
```

Modes:

```text
0 = random
1 = csma
2 = aimd
```

## 4. Batch experiment

Run:

```bash
chmod +x scripts/run_batch.sh
./scripts/run_batch.sh
```

Output:

```text
results/results_numa_raw.csv
results/results_numa_clean.csv
```

Plot:

```bash
python3 scripts/plot_results.py
```

Figures:

```text
results/goodput_vs_load.png
results/p99_vs_load.png
```

## 5. CSV fields

The output begins with assignment-compatible fields:

```csv
track,load,seed,attempts,success,retry,backoff,delay_p50,delay_p95,delay_p99
```

Additional fields are also included:

```csv
goodput,threads,seconds,mem_node,cpu_node,mem_mb,touches_per_req,latency_samples,backend
```

## 6. Important interpretation

This version no longer uses a fixed artificial service time to imitate CXL latency. It allocates memory on a real NUMA node visible to the guest and performs actual memory touches.

However, the Random/CSMA/AIMD contention control is still implemented in software. The measured latency is end-to-end request latency of this request generator, including lock contention, scheduling, backoff, and memory access.

#!/usr/bin/env bash
set -euo pipefail

# Batch experiment script for cxl_numa_csma.
#
# Usage:
#   ./scripts/run_batch.sh
#
# Default configuration:
#   modes: random/csma/aimd
#   loads: 10,30,50,70,90
#   seeds: 1,2,3
#   threads: 4
#   seconds: 5
#   mem_node: 1
#   cpu_node: 0
#   mem_mb: 512
#   touches_per_req: 4096

BIN="${BIN:-./cxl_numa_csma}"
OUT_DIR="${OUT_DIR:-results}"
RAW="${RAW:-$OUT_DIR/results_numa_raw.csv}"
CLEAN="${CLEAN:-$OUT_DIR/results_numa_clean.csv}"

THREADS="${THREADS:-4}"
SECONDS="${SECONDS:-5}"
MEM_NODE="${MEM_NODE:-1}"
CPU_NODE="${CPU_NODE:-0}"
MEM_MB="${MEM_MB:-512}"
TOUCHES="${TOUCHES:-4096}"

LOADS="${LOADS:-10 30 50 70 90}"
SEEDS="${SEEDS:-1 2 3}"
MODES="${MODES:-0 1 2}"

mkdir -p "$OUT_DIR"
rm -f "$RAW" "$CLEAN"

echo "[INFO] Checking NUMA topology"
numactl -H

echo "[INFO] Running batch experiments"
for seed in $SEEDS; do
  for load in $LOADS; do
    for mode in $MODES; do
      echo "[RUN] mode=$mode load=$load seed=$seed"
      "$BIN" "$mode" "$load" "$THREADS" "$SECONDS" "$MEM_NODE" "$CPU_NODE" "$seed" "$MEM_MB" "$TOUCHES" >> "$RAW"
    done
  done
done

# Keep the first header and remove duplicated headers.
awk 'NR==1 || $1 !~ /^track/' "$RAW" > "$CLEAN"

echo "[INFO] Raw results:   $RAW"
echo "[INFO] Clean results: $CLEAN"

echo "[INFO] Checking percentile order: p50 <= p95 <= p99"
BAD=$(awk -F, 'NR>1 && ($8>$9 || $9>$10) {print $0}' "$CLEAN" || true)
if [[ -n "$BAD" ]]; then
  echo "[WARN] Bad percentile rows found:"
  echo "$BAD"
else
  echo "[OK] Percentile order is valid"
fi

#!/usr/bin/env bash
# End-to-end demonstration script for Process Scheduler Profiler (/proc/sched_profiler).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WORKLOAD_DIR="$ROOT_DIR/workloads"
KO_PATH="$ROOT_DIR/kernel/sched_profiler.ko"
PROC_FILE="/proc/sched_profiler"

SAVE_LOG=0
if [ "${1:-}" = "--save" ]; then
    SAVE_LOG=1
    mkdir -p "$ROOT_DIR/result"
    LOG_FILE="$ROOT_DIR/result/demo_output.txt"
    exec > >(tee "$LOG_FILE") 2>&1
    echo "Saving demo transcript to $LOG_FILE"
fi

BG_PID=""
cleanup() {
    if [ -n "$BG_PID" ] && kill -0 "$BG_PID" 2>/dev/null; then
        kill "$BG_PID" 2>/dev/null || true
        wait "$BG_PID" 2>/dev/null || true
    fi
    if [ -w "$PROC_FILE" ]; then
        echo stop > "$PROC_FILE" 2>/dev/null || true
    fi
}
trap cleanup EXIT

run_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

echo "=== CPE333 Mini-Project 2: Process Scheduler Profiler Demo ==="
echo "Date   : $(date)"
echo "Kernel : $(uname -r) | CPUs: $(nproc)"
echo ""

# Ensure userspace binaries are built
if [ ! -x "$WORKLOAD_DIR/sched_profiler_ctl" ] || [ ! -x "$WORKLOAD_DIR/cpu_workload" ]; then
    echo "[*] Building userspace workloads..."
    make -C "$WORKLOAD_DIR" all
fi

# Demo 0: Module loading & CLI parameter check
if [ ! -e "$PROC_FILE" ]; then
    if [ ! -f "$KO_PATH" ]; then
        echo "[*] Building kernel module $KO_PATH..."
        make -C "$ROOT_DIR/kernel" all
    fi
    echo "[Demo 0] Loading module with CLI parameter pid=$$ ..."
    run_root insmod "$KO_PATH" pid=$$
    cat "$PROC_FILE"
    echo ""
else
    echo "[Demo 0] /proc/sched_profiler is already active."
    echo "         (CLI parameter alternative: sudo insmod kernel/sched_profiler.ko pid=<PID>)"
    echo stop > "$PROC_FILE" 2>/dev/null || run_root sh -c "echo stop > $PROC_FILE"
fi

CTL="$WORKLOAD_DIR/sched_profiler_ctl"

echo "=================================================================="
echo "[Demo 1] CPU-Bound Workload (Preemption & Time-Slice Context Switch)"
echo "Command: $CTL --run $WORKLOAD_DIR/cpu_workload 4 --compete"
echo "=================================================================="
"$CTL" --run "$WORKLOAD_DIR/cpu_workload" 4 --compete
echo ""

echo "=================================================================="
echo "[Demo 2] I/O-Bound / Sleep Workload (Woken Up & Voluntary Switch)"
echo "Command: $CTL --run $WORKLOAD_DIR/sleep_workload 40 50"
echo "=================================================================="
"$CTL" --run "$WORKLOAD_DIR/sleep_workload" 40 50
echo ""

echo "=================================================================="
echo "[Demo 3A] CPU Migration Workload (sched_setaffinity Across Cores)"
echo "Command: $CTL --run $WORKLOAD_DIR/migrate_workload 25 40 --mode affinity"
echo "=================================================================="
"$CTL" --run "$WORKLOAD_DIR/migrate_workload" 25 40 --mode affinity
echo ""

echo "=================================================================="
echo "[Demo 3B] External taskset Migration on Running CPU Workload"
echo "=================================================================="
"$WORKLOAD_DIR/cpu_workload" 0 >/dev/null &
BG_PID=$!
echo "Started background cpu_workload pid=$BG_PID"
"$CTL" -p "$BG_PID" >/dev/null

NCPUS="$(nproc)"
if [ "$NCPUS" -ge 2 ]; then
    for hop in $(seq 1 10); do
        target_cpu=$((hop % NCPUS))
        taskset -cp "$target_cpu" "$BG_PID" >/dev/null
        sleep 0.15
    done
else
    echo "Single CPU host detected; skipping multi-core taskset loop."
    sleep 1
fi

kill "$BG_PID" 2>/dev/null || true
wait "$BG_PID" 2>/dev/null || true
BG_PID=""

"$CTL" --status
echo ""
echo "Demo completed successfully."

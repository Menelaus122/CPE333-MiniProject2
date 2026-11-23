#!/usr/bin/env bash
# Cross-validate /proc/sched_profiler against tracefs (ftrace) scheduler events.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WORKLOAD_DIR="$ROOT_DIR/workloads"
PROC_FILE="/proc/sched_profiler"

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root (sudo $0)" >&2
    exit 1
fi

if [ ! -e "$PROC_FILE" ]; then
    echo "Error: $PROC_FILE not found. Load module first: sudo insmod kernel/sched_profiler.ko" >&2
    exit 1
fi

TRACEFS=""
for cand in /sys/kernel/tracing /sys/kernel/debug/tracing; do
    if [ -d "$cand/events/sched" ]; then
        TRACEFS="$cand"
        break
    fi
done

if [ -z "$TRACEFS" ]; then
    echo "Error: tracefs sched events not found under /sys/kernel/tracing or /sys/kernel/debug/tracing" >&2
    exit 1
fi

if [ ! -x "$WORKLOAD_DIR/cpu_workload" ] || [ ! -x "$WORKLOAD_DIR/sleep_workload" ] || [ ! -x "$WORKLOAD_DIR/migrate_workload" ]; then
    make -C "$WORKLOAD_DIR" all
fi

TMPDIR="$(mktemp -d)"
BG_PID=""
# tracing_on is 1 on a normal system; put back whatever it was when we exit.
ORIG_TRACING_ON="$(cat "$TRACEFS/tracing_on" 2>/dev/null || echo 1)"

reset_ftrace() {
    echo 0 > "$TRACEFS/tracing_on" 2>/dev/null || true
    echo 0 > "$TRACEFS/events/sched/sched_switch/enable" 2>/dev/null || true
    echo 0 > "$TRACEFS/events/sched/sched_wakeup/enable" 2>/dev/null || true
    echo 0 > "$TRACEFS/events/sched/sched_migrate_task/enable" 2>/dev/null || true
    echo 0 > "$TRACEFS/events/sched/sched_switch/filter" 2>/dev/null || true
    echo 0 > "$TRACEFS/events/sched/sched_wakeup/filter" 2>/dev/null || true
    echo 0 > "$TRACEFS/events/sched/sched_migrate_task/filter" 2>/dev/null || true
}

cleanup() {
    reset_ftrace
    echo "$ORIG_TRACING_ON" > "$TRACEFS/tracing_on" 2>/dev/null || true
    if [ -n "$BG_PID" ] && kill -0 "$BG_PID" 2>/dev/null; then
        kill "$BG_PID" 2>/dev/null || true
        wait "$BG_PID" 2>/dev/null || true
    fi
    echo stop > "$PROC_FILE" 2>/dev/null || true
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

parse_proc_metrics() {
    awk '
        /^Scheduled In/    { si = $4 }
        /^Scheduled Out/   { so = $4 }
        /^Woken Up/        { wu = $4 }
        /^CPU Migrations/  { mg = $4 }
        END { printf "%d %d %d %d\n", si+0, so+0, wu+0, mg+0 }
    ' "$PROC_FILE"
}

parse_ftrace_metrics() {
    local target="$1"
    local trace_file="$2"
    awk -v tgt="$target" '
        function extract_num(line, key,   pos, rest) {
            pos = index(line, key)
            if (pos == 0) return -1
            rest = substr(line, pos + length(key))
            sub(/[^0-9].*$/, "", rest)
            if (rest == "") return -1
            return rest + 0
        }
        /^#/ { next }
        /sched_switch:/ {
            if (extract_num($0, " prev_pid=") == tgt) so++
            if (extract_num($0, " next_pid=") == tgt) si++
            next
        }
        /sched_wakeup:/ {
            if (extract_num($0, " pid=") == tgt) wu++
            next
        }
        /sched_migrate_task:/ {
            mpid = extract_num($0, " pid=")
            oc   = extract_num($0, " orig_cpu=")
            dc   = extract_num($0, " dest_cpu=")
            if (mpid == tgt && oc >= 0 && dc >= 0 && oc != dc) mg++
            next
        }
        END { printf "%d %d %d %d\n", si+0, so+0, wu+0, mg+0 }
    ' "$trace_file"
}

run_case() {
    local label="$1"
    shift
    local fifo="$TMPDIR/go.fifo"

    rm -f "$fifo"
    mkfifo "$fifo"
    reset_ftrace

    bash -c 'read -r _ < "$1" && exec "${@:2}"' _ "$fifo" "$@" >/dev/null &
    BG_PID=$!
    local tgt_pid="$BG_PID"

    sleep 0.15

    # Kernel trace filter syntax only supports field-to-constant expressions.
    echo "prev_pid == $tgt_pid || next_pid == $tgt_pid" > "$TRACEFS/events/sched/sched_switch/filter"
    echo "pid == $tgt_pid" > "$TRACEFS/events/sched/sched_wakeup/filter"
    echo "pid == $tgt_pid" > "$TRACEFS/events/sched/sched_migrate_task/filter"

    : > "$TRACEFS/trace"
    echo 1 > "$TRACEFS/events/sched/sched_switch/enable"
    echo 1 > "$TRACEFS/events/sched/sched_wakeup/enable"
    echo 1 > "$TRACEFS/events/sched/sched_migrate_task/enable"

    echo "$tgt_pid" > "$PROC_FILE"
    echo 1 > "$TRACEFS/tracing_on"
    echo go > "$fifo"

    wait "$BG_PID" || true
    BG_PID=""

    echo 0 > "$TRACEFS/tracing_on"

    local p_in p_out p_wake p_mig f_in f_out f_wake f_mig
    read -r p_in p_out p_wake p_mig < <(parse_proc_metrics)
    read -r f_in f_out f_wake f_mig < <(parse_ftrace_metrics "$tgt_pid" "$TRACEFS/trace")

    reset_ftrace

    echo "--- Case: $label (PID=$tgt_pid) ---"
    printf "%-18s | %14s | %14s | %10s\n" "Metric" "sched_profiler" "ftrace" "Diff"
    printf "%-18s-+-%14s-+-%14s-+-%10s\n" "------------------" "--------------" "--------------" "----------"
    printf "%-18s | %14d | %14d | %+10d\n" "Scheduled In"   "$p_in"   "$f_in"   "$((p_in - f_in))"
    printf "%-18s | %14d | %14d | %+10d\n" "Scheduled Out"  "$p_out"  "$f_out"  "$((p_out - f_out))"
    printf "%-18s | %14d | %14d | %+10d\n" "Woken Up"       "$p_wake" "$f_wake" "$((p_wake - f_wake))"
    printf "%-18s | %14d | %14d | %+10d\n" "CPU Migrations" "$p_mig"  "$f_mig"  "$((p_mig - f_mig))"
    echo ""
}

echo "=== Cross-Validation: /proc/sched_profiler vs ftrace ($TRACEFS) ==="
echo ""

run_case "1. CPU-Bound (2s --compete)" "$WORKLOAD_DIR/cpu_workload" 2 --compete
run_case "2. Sleep/Wakeup (30 x 40ms)" "$WORKLOAD_DIR/sleep_workload" 30 40
run_case "3. CPU Migration (20 hops)"  "$WORKLOAD_DIR/migrate_workload" 20 30 --mode affinity

echo "Validation complete."

#!/usr/bin/env bash
# Cross-validate /proc/sched_profiler against system-wide `perf record -a` + payload filters.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WORKLOAD_DIR="$ROOT_DIR/workloads"
PROC_FILE="/proc/sched_profiler"

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root (sudo $0)" >&2
    exit 1
fi

# /usr/bin/perf from linux-tools-common is only a wrapper that runs
# /usr/lib/linux-tools/$(uname -r)/perf. There is no linux-tools package for
# the self-built 7.0.0-999 kernel, so the wrapper refuses to run there. perf
# talks to the kernel through the stable perf_event_open() ABI, so the newest
# installed perf binary (e.g. from linux-tools-7.0.0-31-generic) works fine.
PERF="${PERF:-}"
if [ -z "$PERF" ]; then
    if command -v perf >/dev/null 2>&1 && perf version >/dev/null 2>&1; then
        PERF="perf"
    else
        PERF="$(ls -1 /usr/lib/linux-tools/*/perf 2>/dev/null | sort -V | tail -n 1)"
    fi
fi
if [ -z "$PERF" ] || ! "$PERF" version >/dev/null 2>&1; then
    echo "Error: no working perf binary. Install linux-tools-common and" >&2
    echo "       linux-tools-generic-hwe-24.04, or set PERF=/path/to/perf" >&2
    exit 1
fi

if [ ! -e "$PROC_FILE" ]; then
    echo "Error: $PROC_FILE not found. Load module first: sudo insmod kernel/sched_profiler.ko" >&2
    exit 1
fi

if [ ! -x "$WORKLOAD_DIR/cpu_workload" ] || [ ! -x "$WORKLOAD_DIR/sleep_workload" ] || [ ! -x "$WORKLOAD_DIR/migrate_workload" ]; then
    make -C "$WORKLOAD_DIR" all
fi

TMPDIR="$(mktemp -d)"
BG_PID=""
PERF_PID=""

cleanup() {
    if [ -n "$PERF_PID" ] && kill -0 "$PERF_PID" 2>/dev/null; then
        kill -INT "$PERF_PID" 2>/dev/null || true
        wait "$PERF_PID" 2>/dev/null || true
    fi
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

# perf script prints sched_switch and sched_wakeup through libtraceevent
# plugins, e.g.
#   sched:sched_switch: cpu_workload:4691 [120] R ==> cpu_workload:4692 [120]
#   sched:sched_wakeup: sleep_workload:4705 [120] CPU:003
# while events without a plugin (sched_migrate_task) keep the raw
# "pid=... orig_cpu=... dest_cpu=..." form. first_pid() accepts both.
parse_perf_metrics() {
    local target="$1"
    local script_file="$2"
    awk -v tgt="$target" '
        function after(line, key,   pos) {
            pos = index(line, key)
            return pos ? substr(line, pos + length(key)) : ""
        }
        function num_after(s, key,   rest) {
            rest = after(" " s, " " key)
            if (rest == "") return -1
            sub(/[^0-9].*$/, "", rest)
            return rest == "" ? -1 : rest + 0
        }
        function first_pid(s,   v) {
            v = num_after(s, "prev_pid=")
            if (v < 0) v = num_after(s, "next_pid=")
            if (v < 0) v = num_after(s, "pid=")
            if (v >= 0) return v
            if (match(s, /:[0-9]+ \[/)) return substr(s, RSTART + 1, RLENGTH - 3) + 0
            return -1
        }
        /sched:sched_switch:/ {
            body = after($0, "sched:sched_switch:")
            arrow = index(body, "==>")
            if (arrow == 0) next
            if (first_pid(substr(body, 1, arrow - 1)) == tgt) so++
            if (first_pid(substr(body, arrow + 3)) == tgt) si++
            next
        }
        /sched:sched_wakeup:/ {
            if (first_pid(after($0, "sched:sched_wakeup:")) == tgt) wu++
            next
        }
        /sched:sched_migrate_task:/ {
            body = after($0, "sched:sched_migrate_task:")
            oc = num_after(body, "orig_cpu=")
            dc = num_after(body, "dest_cpu=")
            if (first_pid(body) == tgt && oc >= 0 && dc >= 0 && oc != dc) mg++
            next
        }
        END { printf "%d %d %d %d\n", si+0, so+0, wu+0, mg+0 }
    ' "$script_file"
}

# Start dormant wrapper on FIFO so both profiler and perf capture identical window.
run_case() {
    local label="$1"
    shift
    local fifo="$TMPDIR/go.fifo"
    local perf_data="$TMPDIR/perf.data"
    local perf_txt="$TMPDIR/perf.script.txt"

    rm -f "$fifo" "$perf_data" "$perf_txt"
    mkfifo "$fifo"

    bash -c 'read -r _ < "$1" && exec "${@:2}"' _ "$fifo" "$@" >/dev/null &
    BG_PID=$!
    local tgt_pid="$BG_PID"

    sleep 0.15

    # Start perf with its events disabled (-D -1) and enable them through the
    # control FIFO. perf answers "ack" from its main loop only, i.e. after the
    # events exist and existing tasks were synthesized, so nothing the
    # workload does after "go" can be missed (a fixed sleep could be too short).
    local ctl="$TMPDIR/perf.ctl" ack="$TMPDIR/perf.ack" reply=""
    rm -f "$ctl" "$ack"
    mkfifo "$ctl" "$ack"
    "$PERF" record -a -q -D -1 --control "fifo:$ctl,$ack" -o "$perf_data" \
        -e sched:sched_switch --filter "prev_pid == $tgt_pid || next_pid == $tgt_pid" \
        -e sched:sched_wakeup --filter "pid == $tgt_pid" \
        -e sched:sched_migrate_task --filter "pid == $tgt_pid" \
        >/dev/null 2>&1 &
    PERF_PID=$!

    exec 8<>"$ctl" 9<>"$ack"     # read-write opens never block on a FIFO
    echo enable >&8
    read -r -t 20 reply <&9 || true
    exec 8>&- 9>&-
    if [ "$reply" != "ack" ]; then
        echo "WARNING: perf did not confirm 'enable' (got '$reply')" >&2
    fi

    echo "$tgt_pid" > "$PROC_FILE"
    echo go > "$fifo"

    wait "$BG_PID" || true
    BG_PID=""

    sleep 0.25
    kill -INT "$PERF_PID" 2>/dev/null || true
    wait "$PERF_PID" 2>/dev/null || true
    PERF_PID=""

    "$PERF" script -i "$perf_data" > "$perf_txt" 2>/dev/null || true

    local p_in p_out p_wake p_mig f_in f_out f_wake f_mig
    read -r p_in p_out p_wake p_mig < <(parse_proc_metrics)
    read -r f_in f_out f_wake f_mig < <(parse_perf_metrics "$tgt_pid" "$perf_txt")

    echo "--- Case: $label (PID=$tgt_pid) ---"
    printf "%-18s | %14s | %14s | %10s\n" "Metric" "sched_profiler" "perf record -a" "Diff"
    printf "%-18s-+-%14s-+-%14s-+-%10s\n" "------------------" "--------------" "--------------" "----------"
    printf "%-18s | %14d | %14d | %+10d\n" "Scheduled In"   "$p_in"   "$f_in"   "$((p_in - f_in))"
    printf "%-18s | %14d | %14d | %+10d\n" "Scheduled Out"  "$p_out"  "$f_out"  "$((p_out - f_out))"
    printf "%-18s | %14d | %14d | %+10d\n" "Woken Up"       "$p_wake" "$f_wake" "$((p_wake - f_wake))"
    printf "%-18s | %14d | %14d | %+10d\n" "CPU Migrations" "$p_mig"  "$f_mig"  "$((p_mig - f_mig))"
    echo ""
}

echo "=== Cross-Validation: /proc/sched_profiler vs perf record -a ==="
echo "Note: Using system-wide 'perf record -a' with event payload filters instead"
echo "      of 'perf stat -p \$PID', because '-p' only captures events while the"
echo "      target is 'current' and therefore misses off-CPU wakeups/migrations."
echo "perf binary: $PERF ($("$PERF" version 2>/dev/null))"
echo ""

run_case "1. CPU-Bound (2s --compete)" "$WORKLOAD_DIR/cpu_workload" 2 --compete
run_case "2. Sleep/Wakeup (30 x 40ms)" "$WORKLOAD_DIR/sleep_workload" 30 40
run_case "3. CPU Migration (20 hops)"  "$WORKLOAD_DIR/migrate_workload" 20 30 --mode affinity

echo "Validation complete."

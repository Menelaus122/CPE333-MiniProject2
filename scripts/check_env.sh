#!/usr/bin/env bash
# Check kernel build environment, tracepoints, and userspace validation tools.

set -u

PASS_CNT=0
WARN_CNT=0
FAIL_CNT=0

pass() { printf "[PASS] %s\n" "$1"; PASS_CNT=$((PASS_CNT + 1)); }
warn() { printf "[WARN] %s\n" "$1"; WARN_CNT=$((WARN_CNT + 1)); }
fail() { printf "[FAIL] %s\n" "$1"; FAIL_CNT=$((FAIL_CNT + 1)); }

KVER="$(uname -r)"
ARCH="$(uname -m)"
NCPUS="$(nproc 2>/dev/null || echo 1)"

echo "=== CPE333 Mini-Project 2: Environment Check ==="
echo "Kernel Release : $KVER ($ARCH)"
echo "Online CPUs    : $NCPUS"
if [ -f /etc/os-release ]; then
    OS_PRETTY="$(. /etc/os-release && printf '%s' "${PRETTY_NAME:-unknown}")"
    echo "OS             : $OS_PRETTY"
fi
echo ""

if [ "$NCPUS" -ge 2 ]; then
    pass "Multi-CPU system ($NCPUS CPUs) suitable for CPU migration tests"
else
    warn "Single-CPU environment detected; CPU migration cannot occur across cores"
fi

# Kernel configuration checks
check_kconfig() {
    local cfg="$1"
    local val=""

    if [ -r "/boot/config-$KVER" ]; then
        val="$(grep -E "^${cfg}=" "/boot/config-$KVER" 2>/dev/null | head -n1 | cut -d= -f2)"
    elif [ -r /proc/config.gz ]; then
        val="$(zcat /proc/config.gz 2>/dev/null | grep -E "^${cfg}=" | head -n1 | cut -d= -f2)"
    else
        warn "Cannot inspect kernel config ($cfg): neither /boot/config-$KVER nor /proc/config.gz readable"
        return
    fi

    if [ "$val" = "y" ] || [ "$val" = "m" ]; then
        pass "$cfg=$val"
    else
        fail "$cfg is not enabled (required for kernel module & tracepoints)"
    fi
}

check_kconfig "CONFIG_MODULES"
check_kconfig "CONFIG_TRACEPOINTS"
check_kconfig "CONFIG_FTRACE"
check_kconfig "CONFIG_EVENT_TRACING"

# Kernel headers / Kbuild tree
KBUILD="/lib/modules/$KVER/build"
if [ -d "$KBUILD" ]; then
    pass "Kernel build tree present: $KBUILD"
else
    warn "Missing $KBUILD (install linux-headers-$KVER or build inside VMware CPE333-kernel VM)"
fi

# Locate include/trace/events/sched.h
SCHED_H=""
for cand in \
    "$KBUILD/include/trace/events/sched.h" \
    "/usr/src/linux-headers-$KVER/include/trace/events/sched.h" \
    "$HOME"/kernel/linux-*/include/trace/events/sched.h; do
    if [ -r "$cand" ]; then
        SCHED_H="$cand"
        break
    fi
done

if [ -n "$SCHED_H" ]; then
    pass "Found sched tracepoint header: $SCHED_H"
    echo "  -> Tracepoint prototypes in sched.h:"
    awk '/^(TRACE_EVENT|DEFINE_EVENT)\((sched_wakeup_template, )?(sched_switch|sched_wakeup|sched_migrate_task),/,/\);/' "$SCHED_H" \
        | grep -E 'TRACE_EVENT|DEFINE_EVENT|TP_PROTO|struct task_struct|unsigned int|int dest_cpu' \
        | sed 's/^/     /'
else
    warn "sched.h not found locally (expected on target Ubuntu VM with linux-headers/source)"
fi

# Toolchain & validation utilities
for tool in gcc make taskset; do
    if command -v "$tool" >/dev/null 2>&1; then
        pass "Tool available: $tool ($(command -v "$tool"))"
    else
        fail "Missing required tool: $tool"
    fi
done

# /usr/bin/perf is a wrapper that fails on kernels without a matching
# linux-tools package (such as 7.0.0-999); validate_perf.sh then falls back
# to the newest /usr/lib/linux-tools/*/perf, so check the same way here.
PERF_BIN=""
if command -v perf >/dev/null 2>&1 && perf version >/dev/null 2>&1; then
    PERF_BIN="$(command -v perf)"
else
    PERF_BIN="$(ls -1 /usr/lib/linux-tools/*/perf 2>/dev/null | sort -V | tail -n 1)"
fi
if [ -n "$PERF_BIN" ]; then
    pass "Tool available: perf ($PERF_BIN)"
else
    warn "perf not installed (optional for cross-validation; install linux-tools-common linux-tools-generic-hwe-24.04)"
fi

# Tracefs check
TRACEFS=""
for p in /sys/kernel/tracing /sys/kernel/debug/tracing; do
    if [ -d "$p/events/sched" ] || [ -d "$p" ]; then
        TRACEFS="$p"
        break
    fi
done

if [ -n "$TRACEFS" ]; then
    pass "Tracefs path detected: $TRACEFS"
else
    warn "Tracefs not mounted/accessible (normal inside unprivileged container/WSL without tracefs; check with sudo on VM)"
fi

echo ""
echo "Summary: PASS=$PASS_CNT WARN=$WARN_CNT FAIL=$FAIL_CNT"
exit 0

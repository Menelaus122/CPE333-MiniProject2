#!/usr/bin/env bash
# Put sched_profiler into the Ubuntu kernel source tree from Mini-Project 1 as an
# in-tree driver: drivers/misc/sched_profiler.c, Kconfig symbol SCHED_PROFILER
# (tristate, default m) and one line in drivers/misc/Makefile. If the tree still
# has its build directory (debian/build/build-generic), CONFIG_SCHED_PROFILER=m
# is also set there, so the next `debian/rules build-arch` compiles the module.
#
# Usage: bash kernel/intree_patch.sh [kernel source dir]   (default ~/kernel/linux-*/)
# Safe to run again: nothing is added twice. Pristine copies of the two edited
# files are kept in ~/kernel/orig-drivers-misc/ for `diff -u`.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${1:-$(ls -d "$HOME"/kernel/linux-*/ | head -n 1)}"
SRC="$(cd "$SRC" && pwd)"
MISC="$SRC/drivers/misc"
ORIG="$HOME/kernel/orig-drivers-misc"
BUILD="$SRC/debian/build/build-generic"

if [ ! -f "$MISC/Kconfig" ] || [ ! -f "$MISC/Makefile" ]; then
    echo "Not a kernel source tree: $SRC" >&2
    exit 1
fi

mkdir -p "$ORIG"
[ -f "$ORIG/Kconfig" ] || cp "$MISC/Kconfig" "$ORIG/Kconfig"
[ -f "$ORIG/Makefile" ] || cp "$MISC/Makefile" "$ORIG/Makefile"

install -m 644 "$HERE/sched_profiler.c" "$MISC/sched_profiler.c"

if ! grep -q '^config SCHED_PROFILER$' "$MISC/Kconfig"; then
    snippet="$(mktemp)"
    cat > "$snippet" <<'KCONFIG'

config SCHED_PROFILER
	tristate "CPE333 Process Scheduler Profiler (/proc/sched_profiler)"
	depends on TRACEPOINTS && PROC_FS
	default m
	help
	  Counts how often one PID is scheduled in and out, woken up and
	  moved between CPUs, using the sched_switch, sched_wakeup and
	  sched_migrate_task tracepoints. Results are read from
	  /proc/sched_profiler. CPE333 Operating Systems, Mini-Project 2.

KCONFIG
    # drivers/misc/Kconfig is one `menu "Misc devices"`; insert before its endmenu
    last="$(grep -n '^endmenu' "$MISC/Kconfig" | tail -n 1 | cut -d: -f1)"
    sed -i "$((last - 1))r $snippet" "$MISC/Kconfig"
    rm -f "$snippet"
fi

if ! grep -q 'CONFIG_SCHED_PROFILER' "$MISC/Makefile"; then
    printf 'obj-$(CONFIG_SCHED_PROFILER)\t+= sched_profiler.o\n' >> "$MISC/Makefile"
fi

cd "$SRC"
echo "drivers/misc/sched_profiler.c: $(wc -l < drivers/misc/sched_profiler.c) lines (from $HERE)"
diff -u --label a/drivers/misc/Kconfig --label b/drivers/misc/Kconfig "$ORIG/Kconfig" drivers/misc/Kconfig || true
diff -u --label a/drivers/misc/Makefile --label b/drivers/misc/Makefile "$ORIG/Makefile" drivers/misc/Makefile || true

if [ -f "$BUILD/.config" ]; then
    # Setting the value before the build keeps syncconfig from stopping to
    # ask about the new symbol.
    bash scripts/config --file "$BUILD/.config" --module SCHED_PROFILER
    echo "$BUILD/.config:"
    grep -n 'CONFIG_SCHED_PROFILER' "$BUILD/.config"
fi

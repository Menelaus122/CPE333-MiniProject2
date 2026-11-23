#!/usr/bin/env bash
# Rebuild the Mini-Project 1 Ubuntu kernel tree after intree_patch.sh.
#
# Ubuntu's debian/rules is used instead of calling make by hand, so the make
# variables (compiler, KERNELVERSION=7.0.0-999-generic, Rust toolchain, ...)
# are exactly those of the original build. The build directory
# debian/build/build-generic is reused, so only objects whose inputs changed
# are compiled: the new drivers/misc/sched_profiler.o, plus whatever depends on
# the regenerated config.
#
# Usage: bash kernel/intree_build.sh [kernel source dir]   (default ~/kernel/linux-*/)

set -uo pipefail

SRC="${1:-$(ls -d "$HOME"/kernel/linux-*/ | head -n 1)}"
cd "$SRC" || exit 1
LOG="$HOME/build_mp2.log"
KO=debian/build/build-generic/drivers/misc/sched_profiler.ko

if ! grep -q '^CONFIG_SCHED_PROFILER=m' debian/build/build-generic/.config 2>/dev/null; then
    echo "Run intree_patch.sh first (CONFIG_SCHED_PROFILER is not set)" >&2
    exit 1
fi

# The rule is "$(stampdir)/stamp-build-%"; removing the stamp makes debian/rules
# run the flavour's build recipe (kmake ... vmlinux modules) again.
rm -f debian/stamps/stamp-build-generic
start=$(date +%s)
rc=1
for target in "$PWD/debian/stamps/stamp-build-generic" debian/stamps/stamp-build-generic; do
    echo "+ debian/rules $target  > $LOG"
    debian/rules "$target" > "$LOG" 2>&1
    rc=$?
    grep -q 'No rule to make target' "$LOG" || break
done
end=$(date +%s)

echo "build exit status = $rc ($(( (end - start) / 60 ))m $(( (end - start) % 60 ))s)"
echo "objects compiled : $(grep -cE '^ +(CC|RUSTC|AS)( \[M\])? ' "$LOG")"
grep -E 'sched_profiler' "$LOG"
echo "last lines of the log:"
tail -n 2 "$LOG"
ls -l "$KO" && modinfo "$KO" | grep -E '^(name|intree|vermagic)'

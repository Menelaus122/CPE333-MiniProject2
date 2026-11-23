.PHONY: all module workloads clean load unload check demo validate-perf validate-ftrace help

all: workloads
	@if [ -d "/lib/modules/$$(uname -r)/build" ]; then \
		$(MAKE) module; \
	else \
		echo "[NOTE] Skipping kernel module build: /lib/modules/$$(uname -r)/build not found."; \
		echo "       Build the .ko on the target Ubuntu VM (CPE333-kernel) with 'make module'."; \
	fi

module:
	$(MAKE) -C kernel all

workloads:
	$(MAKE) -C workloads all

clean:
	$(MAKE) -C workloads clean
	@if [ -d "/lib/modules/$$(uname -r)/build" ]; then \
		$(MAKE) -C kernel clean; \
	else \
		rm -f kernel/*.o kernel/*.ko kernel/*.mod* kernel/*.order kernel/*.symvers; \
	fi

load: module
	sudo insmod kernel/sched_profiler.ko

unload:
	sudo rmmod sched_profiler

check:
	bash scripts/check_env.sh

demo: workloads
	bash scripts/run_demo.sh

validate-perf: workloads
	sudo bash scripts/validate_perf.sh

validate-ftrace: workloads
	sudo bash scripts/validate_ftrace.sh

help:
	@echo "CPE333 Mini-Project 2 Targets:"
	@echo "  make all             Build workloads (and kernel module if headers exist)"
	@echo "  make module          Build kernel/sched_profiler.ko"
	@echo "  make workloads       Build userspace workload & CLI binaries"
	@echo "  make check           Inspect kernel config, headers, and tools"
	@echo "  make load / unload   Load or unload sched_profiler.ko"
	@echo "  make demo            Run full workload demonstration"
	@echo "  make validate-perf   Cross-validate metrics with perf record -a"
	@echo "  make validate-ftrace Cross-validate metrics with tracefs (ftrace)"
	@echo "  make clean           Clean build artifacts"

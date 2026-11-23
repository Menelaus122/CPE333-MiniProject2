CC     := gcc
CFLAGS := -O2 -Wall -Wextra -Werror -D_GNU_SOURCE

BINS := cpu_workload sleep_workload migrate_workload sched_profiler_ctl

.PHONY: all clean

all: $(BINS)

cpu_workload: cpu_workload.c self_stats.h
	$(CC) $(CFLAGS) -o $@ $<

sleep_workload: sleep_workload.c self_stats.h
	$(CC) $(CFLAGS) -o $@ $<

migrate_workload: migrate_workload.c self_stats.h
	$(CC) $(CFLAGS) -o $@ $<

sched_profiler_ctl: sched_profiler_ctl.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BINS)

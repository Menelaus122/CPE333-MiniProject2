/*
 * self_stats.h - a workload's own view of its scheduling counters.
 *
 * Printed at exit so the numbers from /proc/sched_profiler can be compared
 * with what the kernel keeps per task, without perf or ftrace:
 *   nvcsw / nivcsw  getrusage(RUSAGE_SELF): voluntary / involuntary
 *                   context switches (the same rule the profiler uses to
 *                   split "Scheduled Out")
 *   nr_migrations   se.nr_migrations from /proc/self/sched, bumped in
 *                   set_task_cpu() only when the CPU really changes
 *                   (-1 if the file is not available)
 * The counters cover the whole life of the process, so they can differ from
 * the profiler by one or two switches made before the profiler started or
 * after this line was printed (the final switch-out at exit).
 */
#ifndef SELF_STATS_H
#define SELF_STATS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static inline long long self_nr_migrations(void)
{
	char line[256];
	long long val = -1;
	FILE *f = fopen("/proc/self/sched", "r");

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "se.nr_migrations", 16) == 0) {
			char *colon = strchr(line, ':');

			if (colon)
				val = atoll(colon + 1);
			break;
		}
	}
	fclose(f);
	return val;
}

static inline void print_self_stats(const char *prog)
{
	struct rusage ru;

	if (getrusage(RUSAGE_SELF, &ru) != 0)
		return;
	printf("%s: self-check pid=%d nvcsw=%ld nivcsw=%ld nr_migrations=%lld\n",
	       prog, (int)getpid(), ru.ru_nvcsw, ru.ru_nivcsw,
	       self_nr_migrations());
	fflush(stdout);
}

#endif /* SELF_STATS_H */

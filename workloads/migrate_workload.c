#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "self_stats.h"

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void busy_burn_ms(long ms)
{
	uint64_t deadline = now_ns() + (uint64_t)ms * 1000000ULL;
	volatile uint64_t x = 1;

	while (!stop_requested && now_ns() < deadline) {
		int k;

		for (k = 0; k < 10000; k++)
			x += (x << 1) ^ 0x517cc1b727220a95ULL;
	}
}

static int get_allowed_cpus(int *cpus, int max_cpus)
{
	cpu_set_t set;
	int count = 0;
	int c;

	CPU_ZERO(&set);
	if (sched_getaffinity(0, sizeof(set), &set) != 0)
		return -1;

	for (c = 0; c < CPU_SETSIZE && count < max_cpus; c++) {
		if (CPU_ISSET(c, &set))
			cpus[count++] = c;
	}
	return count;
}

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [hops] [burn_ms] [--mode affinity|load]\n"
		"  hops     Number of CPU migration steps (default: 30)\n"
		"  burn_ms  Busy-burn time per step in milliseconds (default: 50)\n"
		"  --mode   affinity: cycle through allowed CPUs via sched_setaffinity\n"
		"           load:     spawn load disturbers to trigger organic CFS/EEVDF migrations\n",
		prog);
}

int main(int argc, char *argv[])
{
	long hops = 30;
	long burn_ms = 50;
	int load_mode = 0;
	int cpus[CPU_SETSIZE];
	int ncpus;
	struct sigaction sa;
	long pos_arg = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--mode") == 0) {
			if (i + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			i++;
			if (strcmp(argv[i], "affinity") == 0) {
				load_mode = 0;
			} else if (strcmp(argv[i], "load") == 0) {
				load_mode = 1;
			} else {
				print_usage(argv[0]);
				return 1;
			}
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return 0;
		} else {
			char *endptr = NULL;
			long val = strtol(argv[i], &endptr, 10);

			if (endptr == argv[i] || *endptr != '\0' || val <= 0) {
				print_usage(argv[0]);
				return 1;
			}
			if (pos_arg == 0)
				hops = val;
			else if (pos_arg == 1)
				burn_ms = val;
			else {
				print_usage(argv[0]);
				return 1;
			}
			pos_arg++;
		}
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	ncpus = get_allowed_cpus(cpus, CPU_SETSIZE);
	if (ncpus <= 0) {
		fprintf(stderr, "migrate_workload: could not determine allowed CPUs: %s\n",
			strerror(errno));
		return 1;
	}

	printf("migrate_workload: pid=%d mode=%s allowed_cpus=%d hops=%ld burn_ms=%ld\n",
	       (int)getpid(), load_mode ? "load" : "affinity", ncpus, hops, burn_ms);
	if (ncpus < 2) {
		fprintf(stderr, "migrate_workload: WARNING only 1 CPU allowed; migration requires >= 2 CPUs\n");
	}
	fflush(stdout);

	if (!load_mode) {
		int prev_cpu = sched_getcpu();
		long observed_moves = 0;
		long step;

		for (step = 0; step < hops && !stop_requested; step++) {
			int target = cpus[step % ncpus];
			cpu_set_t mask;
			int cur_cpu;

			CPU_ZERO(&mask);
			CPU_SET(target, &mask);
			if (sched_setaffinity(0, sizeof(mask), &mask) != 0) {
				fprintf(stderr, "sched_setaffinity(cpu=%d) failed: %s\n",
					target, strerror(errno));
				return 1;
			}

			cur_cpu = sched_getcpu();
			if (prev_cpu >= 0 && cur_cpu >= 0 && cur_cpu != prev_cpu)
				observed_moves++;
			prev_cpu = cur_cpu;

			busy_burn_ms(burn_ms);
		}

		printf("migrate_workload: done pid=%d observed_cpu_changes=%ld\n",
		       (int)getpid(), observed_moves);
	} else {
		int num_workers = ncpus < 16 ? ncpus : 16;
		pid_t *workers = calloc((size_t)num_workers, sizeof(pid_t));
		int spawned = 0;
		int prev_cpu = sched_getcpu();
		long observed_moves = 0;
		long step;

		if (!workers) {
			perror("calloc");
			return 1;
		}

		for (i = 0; i < num_workers; i++) {
			pid_t pid = fork();

			if (pid < 0)
				break;
			if (pid == 0) {
				while (!stop_requested) {
					busy_burn_ms(20);
					usleep(10000);
				}
				_exit(0);
			}
			workers[spawned++] = pid;
		}

		for (step = 0; step < hops && !stop_requested; step++) {
			int cur_cpu;

			busy_burn_ms(burn_ms);
			usleep(5000);
			cur_cpu = sched_getcpu();
			if (prev_cpu >= 0 && cur_cpu >= 0 && cur_cpu != prev_cpu)
				observed_moves++;
			prev_cpu = cur_cpu;
		}

		for (i = 0; i < spawned; i++)
			kill(workers[i], SIGTERM);
		for (i = 0; i < spawned; i++) {
			int st = 0;

			while (waitpid(workers[i], &st, 0) < 0 && errno == EINTR)
				;
		}
		free(workers);

		printf("migrate_workload: done pid=%d disturbers=%d observed_cpu_changes=%ld\n",
		       (int)getpid(), spawned, observed_moves);
	}
	print_self_stats("migrate_workload");

	return 0;
}

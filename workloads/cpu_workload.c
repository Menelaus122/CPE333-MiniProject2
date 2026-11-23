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
#include <sys/prctl.h>
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

static double now_sec(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int pick_first_allowed_cpu(void)
{
	cpu_set_t set;
	int cpu;

	CPU_ZERO(&set);
	if (sched_getaffinity(0, sizeof(set), &set) != 0)
		return 0;

	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (CPU_ISSET(cpu, &set))
			return cpu;
	}
	return 0;
}

static int pin_to_cpu(int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	return sched_setaffinity(0, sizeof(set), &set);
}

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [seconds] [--compete] [--cpu N]\n"
		"  seconds   Duration in seconds (default: 5, 0 = run until SIGINT/SIGTERM)\n"
		"  --compete Fork a competing CPU-burner child pinned to the same core\n"
		"            to force frequent time-slice preemptions on multi-core hosts\n"
		"  --cpu N   Pin workload (and competitor) to CPU core N\n",
		prog);
}

int main(int argc, char *argv[])
{
	double duration = 5.0;
	int compete = 0;
	int pinned_cpu = -1;
	pid_t child_pid = -1;
	struct sigaction sa;
	double start_t, end_t;
	volatile uint64_t acc = 0;
	uint64_t iters = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--compete") == 0) {
			compete = 1;
		} else if (strcmp(argv[i], "--cpu") == 0) {
			if (i + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			pinned_cpu = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return 0;
		} else {
			char *endptr = NULL;
			double val = strtod(argv[i], &endptr);

			if (endptr == argv[i] || *endptr != '\0' || val < 0.0) {
				print_usage(argv[0]);
				return 1;
			}
			duration = val;
		}
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if (compete && pinned_cpu < 0)
		pinned_cpu = pick_first_allowed_cpu();

	if (pinned_cpu >= 0) {
		if (pin_to_cpu(pinned_cpu) != 0) {
			fprintf(stderr, "cpu_workload: failed to pin to CPU %d: %s\n",
				pinned_cpu, strerror(errno));
			return 1;
		}
	}

	if (compete) {
		pid_t parent = getpid();

		child_pid = fork();
		if (child_pid < 0) {
			perror("fork");
			return 1;
		}
		if (child_pid == 0) {
			/* Competing CPU-bound child pinned to the same CPU */
			volatile uint64_t cacc = 1;

			/* Do not keep burning a core if the parent is SIGKILLed */
			(void)prctl(PR_SET_PDEATHSIG, SIGTERM);
			if (getppid() != parent)
				_exit(0);
			if (pinned_cpu >= 0)
				(void)pin_to_cpu(pinned_cpu);
			while (!stop_requested) {
				cacc += (cacc << 1) ^ 0x9e3779b97f4a7c15ULL;
			}
			_exit(0);
		}
	}

	printf("cpu_workload: pid=%d duration=%.2fs compete=%s",
	       (int)getpid(), duration, compete ? "yes" : "no");
	if (pinned_cpu >= 0)
		printf(" pinned_cpu=%d", pinned_cpu);
	if (child_pid > 0)
		printf(" competitor_pid=%d", (int)child_pid);
	printf("\n");
	fflush(stdout);

	start_t = now_sec();
	while (!stop_requested) {
		uint64_t batch;

		for (batch = 0; batch < 1000000ULL; batch++) {
			acc += (batch * 2654435761ULL) ^ (acc >> 3);
		}
		iters += batch;

		if (duration > 0.0 && (now_sec() - start_t) >= duration)
			break;
	}
	end_t = now_sec();

	if (child_pid > 0) {
		int status = 0;

		kill(child_pid, SIGTERM);
		while (waitpid(child_pid, &status, 0) < 0 && errno == EINTR)
			;
	}

	printf("cpu_workload: done pid=%d elapsed=%.3fs iterations=%llu checksum=0x%llx\n",
	       (int)getpid(), end_t - start_t,
	       (unsigned long long)iters,
	       (unsigned long long)acc);
	print_self_stats("cpu_workload");

	return 0;
}

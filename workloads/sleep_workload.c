#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "self_stats.h"

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [count] [sleep_ms]\n"
		"  count     Number of sleep-wakeup cycles (default: 50)\n"
		"  sleep_ms  Duration per sleep in milliseconds (default: 50)\n",
		prog);
}

int main(int argc, char *argv[])
{
	long count = 50;
	long sleep_ms = 50;
	long completed = 0;
	struct sigaction sa;
	struct timespec req, rem;
	long i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return 0;
		}
	}

	if (argc >= 2) {
		char *endptr = NULL;

		count = strtol(argv[1], &endptr, 10);
		if (endptr == argv[1] || *endptr != '\0' || count <= 0) {
			print_usage(argv[0]);
			return 1;
		}
	}
	if (argc >= 3) {
		char *endptr = NULL;

		sleep_ms = strtol(argv[2], &endptr, 10);
		if (endptr == argv[2] || *endptr != '\0' || sleep_ms <= 0) {
			print_usage(argv[0]);
			return 1;
		}
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	printf("sleep_workload: pid=%d cycles=%ld sleep_ms=%ld\n",
	       (int)getpid(), count, sleep_ms);
	fflush(stdout);

	for (i = 0; i < count && !stop_requested; i++) {
		req.tv_sec = sleep_ms / 1000;
		req.tv_nsec = (sleep_ms % 1000) * 1000000L;

		while (clock_nanosleep(CLOCK_MONOTONIC, 0, &req, &rem) == EINTR) {
			if (stop_requested)
				break;
			req = rem;
		}
		if (!stop_requested)
			completed++;
	}

	printf("sleep_workload: done pid=%d completed_cycles=%ld\n",
	       (int)getpid(), completed);
	print_self_stats("sleep_workload");
	return 0;
}

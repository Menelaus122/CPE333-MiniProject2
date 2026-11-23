#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROC_PATH "/proc/sched_profiler"

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s -p <PID> [-w <seconds>] [-i <interval_ms>] [--tgid]\n"
		"      Monitor an existing PID (optionally polling for <seconds>)\n"
		"  %s --run [--tgid] <command> [args...]\n"
		"      Launch <command>, attach profiler BEFORE exec(), wait, and print stats\n"
		"  %s --status\n"
		"      Print current /proc/sched_profiler statistics\n"
		"  %s --reset\n"
		"      Reset counters for the active target\n"
		"  %s --stop\n"
		"      Stop monitoring (set profiler idle)\n"
		"  %s -h | --help\n"
		"      Show this help message\n",
		prog, prog, prog, prog, prog, prog);
}

static int write_proc_cmd(const char *cmd)
{
	int fd;
	size_t len = strlen(cmd);
	ssize_t n;

	fd = open(PROC_PATH, O_WRONLY);
	if (fd < 0) {
		if (errno == ENOENT) {
			fprintf(stderr,
				"Error: %s not found. Load kernel module first:\n"
				"  sudo insmod kernel/sched_profiler.ko\n",
				PROC_PATH);
		} else {
			fprintf(stderr, "Error opening %s for write: %s\n",
				PROC_PATH, strerror(errno));
		}
		return -1;
	}

	n = write(fd, cmd, len);
	if (n < 0) {
		fprintf(stderr, "Error writing '%s' to %s: %s\n",
			cmd, PROC_PATH, strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int read_proc_status(void)
{
	int fd;
	char buf[4096];
	ssize_t n;

	fd = open(PROC_PATH, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT) {
			fprintf(stderr,
				"Error: %s not found. Load kernel module first:\n"
				"  sudo insmod kernel/sched_profiler.ko\n",
				PROC_PATH);
		} else {
			fprintf(stderr, "Error opening %s for read: %s\n",
				PROC_PATH, strerror(errno));
		}
		return -1;
	}

	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n)
			break;
	}
	if (n < 0) {
		fprintf(stderr, "Error reading %s: %s\n",
			PROC_PATH, strerror(errno));
		close(fd);
		return -1;
	}
	fflush(stdout);
	close(fd);
	return 0;
}

static void sleep_ms(long ms)
{
	struct timespec req, rem;

	req.tv_sec = ms / 1000;
	req.tv_nsec = (ms % 1000) * 1000000L;
	while (clock_nanosleep(CLOCK_MONOTONIC, 0, &req, &rem) == EINTR)
		req = rem;
}

static int run_wrapped(int tgid_mode, char *const cmd_argv[])
{
	int pipefd[2];
	int readyfd[2];
	pid_t child;
	char cmd[64];
	char ack = '1';
	char ready = 0;
	ssize_t rr;
	int status = 0;

	if (pipe(pipefd) != 0 || pipe(readyfd) != 0) {
		perror("pipe");
		return 1;
	}

	child = fork();
	if (child < 0) {
		perror("fork");
		close(pipefd[0]);
		close(pipefd[1]);
		close(readyfd[0]);
		close(readyfd[1]);
		return 1;
	}

	if (child == 0) {
		char go = 0;
		ssize_t r;
		const char *base = strrchr(cmd_argv[0], '/');

		close(pipefd[1]);
		close(readyfd[0]);
		/*
		 * The profiler records the task's comm when the PID is
		 * registered, which happens before execvp(). Take the
		 * command's name now so /proc/sched_profiler shows
		 * "cpu_workload" rather than "sched_profiler_ctl".
		 */
		(void)prctl(PR_SET_NAME, base ? base + 1 : cmd_argv[0]);
		if (write(readyfd[1], "r", 1) != 1)
			_exit(126);
		close(readyfd[1]);

		/* Wait until parent registers our PID in /proc/sched_profiler */
		do {
			r = read(pipefd[0], &go, 1);
		} while (r < 0 && errno == EINTR);
		close(pipefd[0]);

		if (r <= 0 || go != '1')
			_exit(126);

		execvp(cmd_argv[0], cmd_argv);
		fprintf(stderr, "sched_profiler_ctl: execvp(%s) failed: %s\n",
			cmd_argv[0], strerror(errno));
		_exit(127);
	}

	close(pipefd[0]);
	close(readyfd[1]);
	/* Wait for the child to rename itself before registering it */
	do {
		rr = read(readyfd[0], &ready, 1);
	} while (rr < 0 && errno == EINTR);
	close(readyfd[0]);

	snprintf(cmd, sizeof(cmd), "%s %d\n", tgid_mode ? "tgid" : "pid", (int)child);

	if (write_proc_cmd(cmd) != 0) {
		/* Tell child to abort cleanly instead of running untracked */
		ssize_t ignored;

		ack = '0';
		ignored = write(pipefd[1], &ack, 1);
		(void)ignored;
		close(pipefd[1]);
		while (waitpid(child, &status, 0) < 0 && errno == EINTR)
			;
		return 1;
	}

	if (write(pipefd[1], &ack, 1) != 1) {
		perror("write pipe");
		close(pipefd[1]);
		kill(child, SIGTERM);
		while (waitpid(child, &status, 0) < 0 && errno == EINTR)
			;
		return 1;
	}
	close(pipefd[1]);

	while (waitpid(child, &status, 0) < 0 && errno == EINTR)
		;

	printf("\n");
	if (read_proc_status() != 0)
		return 1;

	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 1;
}

int main(int argc, char *argv[])
{
	int target_pid = -1;
	int wait_sec = 0;
	long interval_ms = 1000;
	int tgid_mode = 0;
	int do_status = 0;
	int do_reset = 0;
	int do_stop = 0;
	int run_idx = -1;
	int i;

	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return 0;
		} else if (strcmp(argv[i], "--status") == 0) {
			do_status = 1;
		} else if (strcmp(argv[i], "--reset") == 0) {
			do_reset = 1;
		} else if (strcmp(argv[i], "--stop") == 0) {
			do_stop = 1;
		} else if (strcmp(argv[i], "--tgid") == 0) {
			tgid_mode = 1;
		} else if (strcmp(argv[i], "-p") == 0) {
			if (i + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			target_pid = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-w") == 0) {
			if (i + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			wait_sec = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-i") == 0) {
			if (i + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			interval_ms = atol(argv[++i]);
		} else if (strcmp(argv[i], "--run") == 0) {
			if (i + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			if (strcmp(argv[i + 1], "--tgid") == 0) {
				tgid_mode = 1;
				i++;
			}
			if (i + 1 >= argc) {
				print_usage(argv[0]);
				return 1;
			}
			run_idx = i + 1;
			break;
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
	}

	if (do_status)
		return read_proc_status() == 0 ? 0 : 1;

	if (do_reset) {
		if (write_proc_cmd("reset\n") != 0)
			return 1;
		return read_proc_status() == 0 ? 0 : 1;
	}

	if (do_stop) {
		if (write_proc_cmd("stop\n") != 0)
			return 1;
		return read_proc_status() == 0 ? 0 : 1;
	}

	if (run_idx >= 0)
		return run_wrapped(tgid_mode, &argv[run_idx]);

	if (target_pid <= 0) {
		fprintf(stderr, "Error: specify a valid positive PID with -p <PID>\n");
		return 1;
	}

	{
		char cmd[64];

		snprintf(cmd, sizeof(cmd), "%s %d\n",
			 tgid_mode ? "tgid" : "pid", target_pid);
		if (write_proc_cmd(cmd) != 0)
			return 1;
	}

	if (wait_sec > 0) {
		long total_ms = (long)wait_sec * 1000L;
		long elapsed = 0;

		if (interval_ms <= 0)
			interval_ms = total_ms;

		while (elapsed < total_ms) {
			long step = (total_ms - elapsed) < interval_ms ?
				    (total_ms - elapsed) : interval_ms;
			sleep_ms(step);
			elapsed += step;
			if (read_proc_status() != 0)
				return 1;
			if (elapsed < total_ms)
				printf("\n");
		}
		return 0;
	}

	return read_proc_status() == 0 ? 0 : 1;
}

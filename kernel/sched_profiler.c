// SPDX-License-Identifier: GPL-2.0
/*
 * sched_profiler.c - Linux Kernel Process Scheduler Profiler
 *
 * CPE333 Operating Systems - Mini-Project 2: CPU Virtualization
 *
 * Monitors a target PID (or TGID) in real time using static Linux Kernel
 * tracepoints (sched_switch, sched_wakeup, sched_migrate_task) and exposes
 * scheduling statistics through /proc/sched_profiler as well as module
 * load-time parameters.
 */

#include <linux/atomic.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/tracepoint.h>
#include <linux/uaccess.h>
#include <linux/version.h>

/*
 * Include sched tracepoint definitions WITHOUT defining CREATE_TRACE_POINTS:
 * we only attach probe callbacks to existing kernel tracepoints and obtain
 * compile-time check_trace_callback_type_*() helpers.
 */
#include <trace/events/sched.h>

#define PROC_ENTRY_NAME "sched_profiler"
#define CMD_BUF_LEN 64

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CPE333 Group - Mini-Project 2");
MODULE_DESCRIPTION("Process Scheduler Profiler using Kernel Tracepoints");
MODULE_VERSION("1.0");

static int pid = -1;
module_param(pid, int, 0444);
MODULE_PARM_DESC(pid, "Initial target PID to monitor (<= 0 starts idle)");

static int match_tgid;
module_param(match_tgid, int, 0644);
MODULE_PARM_DESC(match_tgid, "Match entire thread group (tgid) instead of single PID (0=PID, 1=TGID)");

static DEFINE_MUTEX(ctrl_mutex);
static struct proc_dir_entry *proc_entry;

static atomic_t target_pid = ATOMIC_INIT(-1);
static atomic_t target_match_tgid = ATOMIC_INIT(0);

static atomic64_t sched_in_count = ATOMIC64_INIT(0);
static atomic64_t sched_out_count = ATOMIC64_INIT(0);
static atomic64_t sched_out_invol_count = ATOMIC64_INIT(0);
static atomic64_t sched_out_vol_count = ATOMIC64_INIT(0);
static atomic64_t wakeup_count = ATOMIC64_INIT(0);
static atomic64_t migrate_count = ATOMIC64_INIT(0);

static atomic_t last_cpu = ATOMIC_INIT(-1);
static atomic_t last_orig_cpu = ATOMIC_INIT(-1);
static atomic_t last_dest_cpu = ATOMIC_INIT(-1);

static u64 start_time_ns;
static char cached_comm[TASK_COMM_LEN] = "<none>";

static __always_inline bool task_matches(const struct task_struct *p, int tpid)
{
	if (unlikely(!p || tpid <= 0))
		return false;

	if (atomic_read(&target_match_tgid))
		return p->tgid == tpid;

	return p->pid == tpid;
}

/*
 * sched_switch gained the `unsigned int prev_state` argument in Linux 5.19.
 * The voluntary/involuntary split below depends on it, so older kernels are
 * not supported (the target VM runs 7.0.0-999-generic).
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
#error "sched_profiler requires Linux 5.19 or newer (sched_switch with prev_state)"
#endif

/*
 * Tracepoint callback: sched_switch
 * TP_PROTO(bool preempt, struct task_struct *prev,
 *          struct task_struct *next, unsigned int prev_state)
 */
static void probe_sched_switch(void *data, bool preempt,
			       struct task_struct *prev,
			       struct task_struct *next,
			       unsigned int prev_state)
{
	int tpid = atomic_read(&target_pid);

	(void)data;
	if (tpid <= 0)
		return;

	if (task_matches(prev, tpid)) {
		atomic64_inc(&sched_out_count);
		/*
		 * Same rule __schedule() uses to pick nivcsw or nvcsw.
		 * `preempt` alone is not enough: a CPU-bound task whose time
		 * slice runs out is rescheduled on the way back to user space
		 * through schedule(), so preempt == false there, but it is
		 * still TASK_RUNNING. Only a task that is going to sleep
		 * (prev_state != TASK_RUNNING) gave up the CPU voluntarily.
		 */
		if (preempt || prev_state == TASK_RUNNING)
			atomic64_inc(&sched_out_invol_count);
		else
			atomic64_inc(&sched_out_vol_count);
	}

	if (task_matches(next, tpid)) {
		atomic64_inc(&sched_in_count);
		atomic_set(&last_cpu, raw_smp_processor_id());
	}
}

/*
 * Tracepoint callback: sched_wakeup
 * Triggered when a sleeping task transitions to TASK_RUNNING.
 */
static void probe_sched_wakeup(void *data, struct task_struct *p)
{
	int tpid = atomic_read(&target_pid);

	(void)data;
	if (tpid <= 0)
		return;

	if (task_matches(p, tpid))
		atomic64_inc(&wakeup_count);
}

/*
 * Tracepoint callback: sched_migrate_task
 * Note: TP_PROTO is (struct task_struct *p, int dest_cpu). orig_cpu is NOT
 * passed as a parameter and must be read from task_cpu(p) before the move
 * completes inside set_task_cpu().
 */
static void probe_sched_migrate_task(void *data, struct task_struct *p,
				     int dest_cpu)
{
	int tpid = atomic_read(&target_pid);
	int orig_cpu;

	(void)data;
	if (tpid <= 0 || !task_matches(p, tpid))
		return;

	orig_cpu = task_cpu(p);
	if (orig_cpu != dest_cpu) {
		atomic64_inc(&migrate_count);
		atomic_set(&last_orig_cpu, orig_cpu);
		atomic_set(&last_dest_cpu, dest_cpu);
		atomic_set(&last_cpu, dest_cpu);
	}
}

struct tp_binding {
	const char *name;
	void *probe;
	struct tracepoint *tp;
	bool registered;
};

static struct tp_binding tp_bindings[] = {
	{
		.name = "sched_switch",
		.probe = probe_sched_switch,
	},
	{
		.name = "sched_wakeup",
		.probe = probe_sched_wakeup,
	},
	{
		.name = "sched_migrate_task",
		.probe = probe_sched_migrate_task,
	},
};

static void lookup_tracepoint_cb(struct tracepoint *tp, void *priv)
{
	size_t i;

	(void)priv;
	for (i = 0; i < ARRAY_SIZE(tp_bindings); i++) {
		if (!tp_bindings[i].tp &&
		    strcmp(tp->name, tp_bindings[i].name) == 0) {
			tp_bindings[i].tp = tp;
		}
	}
}

static bool lookup_task_comm(int nr, char *buf, size_t len)
{
	struct task_struct *task;
	bool alive = false;

	if (nr <= 0) {
		strscpy(buf, "<none>", len);
		return false;
	}

	rcu_read_lock();
	task = pid_task(find_vpid(nr), PIDTYPE_PID);
	if (task) {
		/*
		 * get_task_comm() only accepts a fixed-size array on recent
		 * kernels (it checks sizeof(buf)), and buf here is a pointer,
		 * so copy task->comm directly. task->comm is always
		 * NUL-terminated and the task cannot be freed under RCU.
		 */
		strscpy(buf, task->comm, len);
		alive = true;
	}
	rcu_read_unlock();

	return alive;
}

static bool task_exists(int nr)
{
	char tmp[TASK_COMM_LEN];

	return lookup_task_comm(nr, tmp, sizeof(tmp));
}

static void configure_target(int new_pid, int new_tgid_mode, bool do_sync)
{
	mutex_lock(&ctrl_mutex);

	/*
	 * Disable probes by setting target_pid = -1 and waiting one RCU grace
	 * period before zeroing counters so in-flight callbacks on other CPUs
	 * cannot corrupt the newly reset counters.
	 */
	atomic_set(&target_pid, -1);
	if (do_sync)
		synchronize_rcu();

	atomic64_set(&sched_in_count, 0);
	atomic64_set(&sched_out_count, 0);
	atomic64_set(&sched_out_invol_count, 0);
	atomic64_set(&sched_out_vol_count, 0);
	atomic64_set(&wakeup_count, 0);
	atomic64_set(&migrate_count, 0);

	atomic_set(&last_cpu, -1);
	atomic_set(&last_orig_cpu, -1);
	atomic_set(&last_dest_cpu, -1);

	atomic_set(&target_match_tgid, new_tgid_mode ? 1 : 0);

	if (new_pid > 0) {
		if (!lookup_task_comm(new_pid, cached_comm, sizeof(cached_comm)))
			strscpy(cached_comm, "<not-found>", sizeof(cached_comm));
		start_time_ns = ktime_get_ns();
	} else {
		new_pid = -1;
		strscpy(cached_comm, "<none>", sizeof(cached_comm));
		start_time_ns = 0;
	}

	smp_wmb();
	atomic_set(&target_pid, new_pid);

	mutex_unlock(&ctrl_mutex);
}

static int sched_profiler_show(struct seq_file *m, void *v)
{
	int tpid;
	int tgid_mode;
	bool alive = false;
	char comm_buf[TASK_COMM_LEN];
	u64 elapsed_ns = 0;
	u64 secs = 0;
	u32 rem_ms = 0;
	int cur_last_cpu;
	int orig_cpu;
	int dest_cpu;

	(void)v;

	mutex_lock(&ctrl_mutex);
	tpid = atomic_read(&target_pid);
	tgid_mode = atomic_read(&target_match_tgid);
	strscpy(comm_buf, cached_comm, sizeof(comm_buf));

	if (tpid > 0) {
		if (lookup_task_comm(tpid, comm_buf, sizeof(comm_buf))) {
			strscpy(cached_comm, comm_buf, sizeof(cached_comm));
			alive = true;
		}
		if (start_time_ns > 0) {
			elapsed_ns = ktime_get_ns() - start_time_ns;
			secs = div_u64_rem(div_u64(elapsed_ns, 1000000ULL),
					   1000U, &rem_ms);
		}
	}
	mutex_unlock(&ctrl_mutex);

	cur_last_cpu = atomic_read(&last_cpu);
	orig_cpu = atomic_read(&last_orig_cpu);
	dest_cpu = atomic_read(&last_dest_cpu);

	seq_puts(m, "=== Process Scheduler Profiler ===\n");
	if (tpid > 0) {
		seq_printf(m, "Target PID        : %d\n", tpid);
		seq_printf(m, "Target Comm       : %s\n", comm_buf);
		seq_printf(m, "Match Mode        : %s\n",
			   tgid_mode ? "TGID (all threads)" : "PID (single task)");
		seq_printf(m, "Process Status    : %s\n",
			   alive ? "ALIVE" : "EXITED (statistics preserved)");
		seq_printf(m, "Monitoring Time   : %llu.%03u s\n", secs, rem_ms);
	} else {
		seq_puts(m, "Target PID        : None (idle)\n");
		seq_puts(m, "Target Comm       : <none>\n");
		seq_printf(m, "Match Mode        : %s\n",
			   tgid_mode ? "TGID (all threads)" : "PID (single task)");
		seq_puts(m, "Process Status    : IDLE\n");
		seq_puts(m, "Monitoring Time   : 0.000 s\n");
	}

	seq_puts(m, "--- Scheduling Metrics ---\n");
	seq_printf(m, "Scheduled In      : %lld\n",
		   (long long)atomic64_read(&sched_in_count));
	seq_printf(m, "Scheduled Out     : %lld (involuntary: %lld, voluntary: %lld)\n",
		   (long long)atomic64_read(&sched_out_count),
		   (long long)atomic64_read(&sched_out_invol_count),
		   (long long)atomic64_read(&sched_out_vol_count));
	seq_printf(m, "Woken Up          : %lld\n",
		   (long long)atomic64_read(&wakeup_count));
	seq_printf(m, "CPU Migrations    : %lld\n",
		   (long long)atomic64_read(&migrate_count));

	if (cur_last_cpu >= 0)
		seq_printf(m, "Last Observed CPU : CPU %d\n", cur_last_cpu);
	else
		seq_puts(m, "Last Observed CPU : N/A\n");

	if (orig_cpu >= 0 && dest_cpu >= 0)
		seq_printf(m, "Last Migration    : CPU %d -> CPU %d\n",
			   orig_cpu, dest_cpu);
	else
		seq_puts(m, "Last Migration    : N/A\n");

	seq_puts(m, "--- Usage ---\n");
	seq_puts(m, "Commands: <PID> | pid <PID> | tgid <PID> | reset | stop\n");

	return 0;
}

static int sched_profiler_open(struct inode *inode, struct file *file)
{
	(void)inode;
	return single_open(file, sched_profiler_show, NULL);
}

/*
 * Start monitoring `val` (or go idle when val <= 0). A PID that does not
 * exist is rejected with -ESRCH, so `echo 999999 > /proc/sched_profiler`
 * fails with "No such process" instead of silently watching nothing.
 */
static ssize_t apply_target(int val, int tgid_mode, size_t count)
{
	if (val <= 0) {
		configure_target(-1, 0, true);
		return count;
	}
	if (!task_exists(val))
		return -ESRCH;

	configure_target(val, tgid_mode, true);
	return count;
}

static ssize_t sched_profiler_write(struct file *file, const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	char kbuf[CMD_BUF_LEN];
	char *cmd;
	int val;
	int ret;

	(void)file;
	(void)ppos;

	if (count == 0)
		return 0;
	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, ubuf, count))
		return -EFAULT;

	kbuf[count] = '\0';
	cmd = strim(kbuf);
	if (*cmd == '\0')
		return -EINVAL;

	if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "off") == 0) {
		configure_target(-1, 0, true);
		return count;
	}

	if (strcmp(cmd, "reset") == 0) {
		int cur_pid = atomic_read(&target_pid);
		int cur_tgid = atomic_read(&target_match_tgid);

		configure_target(cur_pid, cur_tgid, true);
		return count;
	}

	if (strncmp(cmd, "pid ", 4) == 0) {
		ret = kstrtoint(strim(cmd + 4), 10, &val);
		if (ret)
			return ret;
		return apply_target(val, 0, count);
	}

	if (strncmp(cmd, "tgid ", 5) == 0) {
		ret = kstrtoint(strim(cmd + 5), 10, &val);
		if (ret)
			return ret;
		return apply_target(val, 1, count);
	}

	ret = kstrtoint(cmd, 10, &val);
	if (ret)
		return ret;

	return apply_target(val, match_tgid ? 1 : 0, count);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops sched_profiler_proc_ops = {
	.proc_open    = sched_profiler_open,
	.proc_read    = seq_read,
	.proc_write   = sched_profiler_write,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations sched_profiler_proc_ops = {
	.owner   = THIS_MODULE,
	.open    = sched_profiler_open,
	.read    = seq_read,
	.write   = sched_profiler_write,
	.llseek  = seq_lseek,
	.release = single_release,
};
#endif

static void unregister_all_tracepoints(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(tp_bindings); i++) {
		if (tp_bindings[i].registered && tp_bindings[i].tp) {
			tracepoint_probe_unregister(tp_bindings[i].tp,
						    tp_bindings[i].probe, NULL);
			tp_bindings[i].registered = false;
		}
	}
	tracepoint_synchronize_unregister();
}

static int __init sched_profiler_init(void)
{
	size_t i;
	int ret;

	/* Compile-time verification against current kernel's sched.h signatures */
	check_trace_callback_type_sched_switch(probe_sched_switch);
	check_trace_callback_type_sched_wakeup(probe_sched_wakeup);
	check_trace_callback_type_sched_migrate_task(probe_sched_migrate_task);

	/*
	 * -EINVAL, not -ESRCH: insmod (kmod) prints ESRCH as "Module has
	 * wrong symbol version", which would point at the wrong problem.
	 */
	if (pid > 0 && !task_exists(pid)) {
		pr_err("sched_profiler: pid=%d does not exist\n", pid);
		return -EINVAL;
	}
	configure_target(pid > 0 ? pid : -1, match_tgid, false);

	for_each_kernel_tracepoint(lookup_tracepoint_cb, NULL);

	for (i = 0; i < ARRAY_SIZE(tp_bindings); i++) {
		if (!tp_bindings[i].tp) {
			pr_err("sched_profiler: tracepoint '%s' not found\n",
			       tp_bindings[i].name);
			return -ENOENT;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tp_bindings); i++) {
		ret = tracepoint_probe_register(tp_bindings[i].tp,
						tp_bindings[i].probe, NULL);
		if (ret) {
			pr_err("sched_profiler: failed to register '%s' (%d)\n",
			       tp_bindings[i].name, ret);
			unregister_all_tracepoints();
			return ret;
		}
		tp_bindings[i].registered = true;
	}

	proc_entry = proc_create(PROC_ENTRY_NAME, 0666, NULL,
				 &sched_profiler_proc_ops);
	if (!proc_entry) {
		pr_err("sched_profiler: failed to create /proc/%s\n",
		       PROC_ENTRY_NAME);
		atomic_set(&target_pid, -1);
		unregister_all_tracepoints();
		return -ENOMEM;
	}

	pr_info("sched_profiler: loaded (initial pid=%d, match_tgid=%d)\n",
		atomic_read(&target_pid), atomic_read(&target_match_tgid));
	return 0;
}

static void __exit sched_profiler_exit(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;

	atomic_set(&target_pid, -1);
	unregister_all_tracepoints();

	pr_info("sched_profiler: unloaded cleanly\n");
}

module_init(sched_profiler_init);
module_exit(sched_profiler_exit);

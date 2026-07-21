// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/sort.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/syscore_ops.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/cpufreq.h>
#include <linux/sched/cpufreq.h>
#include <trace/hooks/sched.h>

#include "game_ctrl.h"

static int process_num = 0;
pid_t process_pids[PROCESS_PID_COUNT];

atomic_t enable_multi_task_util = ATOMIC_INIT(false);

static DEFINE_RAW_SPINLOCK(g_lock);

static void reset_task_util_info(void)
{
	int i;
	struct game_task_struct *game_task = NULL;
	struct task_struct *task = NULL;
	raw_spin_lock(&g_lock);
	for (i = 0; i < process_num; i++) {
		game_task = get_game_task_struct_by_pid(process_pids[i]);
		task = get_task_struct_by_pid(process_pids[i]);
		if (game_task != NULL) {
			atomic_set(&game_task->mtu_info.is_tracked, 0);
			atomic_set(&game_task->mtu_info.have_valid_process_pids, 0);
			if (game_task->mtu_info.child_threads != NULL) {
				kfree(game_task->mtu_info.child_threads);
				game_task->mtu_info.child_threads = NULL;
			}
			game_task->mtu_info.child_num = 0;
			if (game_task->mtu_info.ui_assist_threads != NULL) {
				kfree(game_task->mtu_info.ui_assist_threads);
				game_task->mtu_info.ui_assist_threads = NULL;
			}
			game_task->mtu_info.ui_assist_nums = 0;
		}
		if (task != NULL) {
			put_task_struct(task);
		}
		process_pids[i] = 0;
	}
	process_num = 0;
	raw_spin_unlock(&g_lock);
}

static void set_multi_task_util_enable(int value)
{
	bool enable;
	if (value != 0 && value != 1)
		return;

	enable = value == 1;

	if (atomic_read(&enable_multi_task_util) != enable) {
		atomic_set(&enable_multi_task_util, enable);
	}
}

static void set_multi_task_util_tgid(int pid)
{
	struct task_struct *leader = NULL;
	struct game_task_struct *tg_g_task = NULL;
	u64 now;
	raw_spin_lock(&g_lock);
	if (pid <= 0) {
		goto unlock;
	}
	rcu_read_lock();
	leader = find_task_by_vpid(pid);
	if (!leader || leader->pid != leader->tgid) {
		rcu_read_unlock();
		goto unlock;
	}
	get_task_struct(leader);
	if (!ts_to_gts(leader, &tg_g_task)) {
		put_task_struct(leader);
		rcu_read_unlock();
		goto unlock;
	}
	rcu_read_unlock();

	process_pids[process_num] = pid;
	now = ktime_get_raw_ns();
	tg_g_task->mtu_info.window_start = now;
	atomic_set(&tg_g_task->mtu_info.have_valid_process_pids, 1);
	tg_g_task->mtu_info.child_num = 0;
	atomic_set(&tg_g_task->mtu_info.is_tracked, 1);

	tg_g_task->mtu_info.child_threads = kmalloc(sizeof(struct task_runtime_info) * MAX_TID_COUNT, GFP_KERNEL);
	if (!tg_g_task->mtu_info.child_threads) {
		goto unlock;
	}
	tg_g_task->mtu_info.ui_assist_threads = kmalloc(sizeof(struct thread_wake_info) * MAX_UI_ASSIST_NUM, GFP_KERNEL);
	if (!tg_g_task->mtu_info.ui_assist_threads) {
		kfree(tg_g_task->mtu_info.child_threads);
		tg_g_task->mtu_info.child_threads = NULL;
		goto unlock;
	}
	++process_num;
	if (process_num >= PROCESS_PID_COUNT) {
		goto unlock;
	}
unlock:
	raw_spin_unlock(&g_lock);
}

static long multi_task_util_ctrl_proc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct multi_task_ctrl_info data;
	void __user *uarg = (void __user *)arg;
	long ret = 0;
	int i;
	if ((_IOC_TYPE(cmd) != MULTI_TASK_INFO_MAGIC) || (_IOC_NR(cmd) >= MULTI_TASK_MAX_ID)) {
		return -EINVAL;
	}
	if (copy_from_user(&data, uarg, sizeof(data))) {
		return -EFAULT;
	}
	switch (cmd) {
	case CMD_ID_MULTI_TASK_ENABLE:
		set_multi_task_util_enable(data.data[0]);
		break;
	case CMD_ID_MULTI_TASK_TGID:
		reset_task_util_info();
		for (i = 0; i < data.size; i++) {
			set_multi_task_util_tgid(data.data[i]);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int multi_task_util_ctrl_proc_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct proc_ops multi_task_util_ctrl_proc_ops = {
	.proc_ioctl		= multi_task_util_ctrl_proc_ioctl,
	.proc_open		= multi_task_util_ctrl_proc_open,
	.proc_lseek		= default_llseek,
};

static ssize_t multi_task_util_enable_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[16] = {0};
	int ret, value;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	ret = sscanf(page, "%d", &value);
	if (ret != 1)
		return -EINVAL;

	set_multi_task_util_enable(value);
	return count;
}

static ssize_t multi_task_util_enable_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char page[16] = {0};
	int len;
	int value;
	value = atomic_read(&enable_multi_task_util) ? 1 : 0;
	len = snprintf(page, sizeof(page), "%d\n", value);
	if (len > 0) {
		len = simple_read_from_buffer(buf, count, ppos, page, len);
	}
	return len;
}

static const struct proc_ops multi_task_util_enable_proc_ops = {
	.proc_write		= multi_task_util_enable_proc_write,
	.proc_read		= multi_task_util_enable_proc_read,
	.proc_lseek		= default_llseek,
};

static ssize_t task_pid_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[1024] = {0};
	char *iter = page;
	int ret, pid;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	reset_task_util_info();

	while (iter != NULL) {
		ret = sscanf(iter, "%d", &pid);
		if (ret != 1) {
			return -EINVAL;
		}
		iter = strchr(iter + 1, ' ');
		set_multi_task_util_tgid(pid);
	}
	ret = count;
	return ret;
}

static ssize_t task_pid_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char page[1024] = {0};
	int len, i, num;
	struct game_task_struct *game_task;

	raw_spin_lock(&g_lock);
	len = 0;
	num = 0;
	for (i = 0; i < process_num; i++) {
		game_task = get_game_task_struct_by_pid(process_pids[i]);
		if (!game_task) {
			continue;
		}
		len += snprintf(page + len, RESULT_PAGE_SIZE - len, "process_pid=%d child_num=%d\n",
			process_pids[i], game_task->mtu_info.child_num);
		++num;
		if (num >= PROCESS_PID_COUNT) {
			break;
		}
	}
	raw_spin_unlock(&g_lock);

	return simple_read_from_buffer(buf, count, ppos, page, len);
}

static const struct proc_ops task_pid_proc_ops = {
	.proc_write		= task_pid_proc_write,
	.proc_read		= task_pid_proc_read,
	.proc_lseek		= default_llseek,
};

/*
 * Ascending order by wake_count
 */
static int cmp_task_wake_count(const void *a, const void *b)
{
	struct thread_wake_info *prev, *next;

	prev = (struct thread_wake_info *)a;
	next = (struct thread_wake_info *)b;
	if (unlikely(!prev || !next))
		return 0;

	if (prev->wake_count > next->wake_count)
		return -1;
	else if (prev->wake_count < next->wake_count)
		return 1;
	else
		return 0;
}

static struct thread_wake_info ui_results[MAX_UI_ASSIST_NUM * PROCESS_PID_COUNT];
static char ui_page[512] = {0};
#define MAX_UA_RESULT_NUM 5
static int ui_assist_thread_show(struct seq_file *m, void *v)
{
	int i, j, num, result_num = 0;
	char task_name[TASK_COMM_LEN];
	ssize_t len = 0;
	struct game_task_struct *game_task;

	for (i = 0; i < process_num; i++) {
		game_task = get_game_task_struct_by_pid(process_pids[i]);
		if (!game_task)
			continue;
		if (atomic_read(&game_task->mtu_info.have_valid_process_pids) == 0)
			continue;
		raw_spin_lock(&g_lock);
		for (j = 0; j < game_task->mtu_info.ui_assist_nums; j++) {
			if (game_task->mtu_info.ui_assist_threads[j].wake_count > 0) {
				ui_results[result_num].pid = game_task->mtu_info.ui_assist_threads[j].pid;
				ui_results[result_num].wake_count = game_task->mtu_info.ui_assist_threads[j].wake_count;
				ui_results[result_num].task = game_task->mtu_info.ui_assist_threads[j].task;
				result_num++;
			}
		}
		raw_spin_unlock(&g_lock);
		if (result_num > 1) {
			sort(&ui_results[0], result_num,
				sizeof(struct thread_wake_info), &cmp_task_wake_count, NULL);
		}
		memset(ui_page, 0, sizeof(ui_page));

		num = 0;
		for (j = 0; j < result_num; j++) {
			if (get_task_name(ui_results[j].pid, ui_results[j].task, task_name)) {
				len += snprintf(ui_page + len, sizeof(ui_page) - len, "%d;%s;%u\n",
					ui_results[j].pid, task_name, ui_results[j].wake_count);
				++num;
				if (num >= MAX_UA_RESULT_NUM) {
					break;
				}
			}
		}
		if (i + 1 < process_num) {
			len += snprintf(ui_page + len, RESULT_PAGE_SIZE - len, "\n");
		}
	}

	if (len > 0)
		seq_puts(m, ui_page);

	return 0;
}

static int ui_assist_thread_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, ui_assist_thread_show, inode);
}

static const struct proc_ops ui_assist_thread_proc_ops = {
	.proc_open		= ui_assist_thread_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

/*
 * Ascending order by sum_exec_scale
 */
static int cmp_task_sum_exec_scale(const void *a, const void *b)
{
	struct task_runtime_info *prev, *next;

	prev = (struct task_runtime_info *)a;
	next = (struct task_runtime_info *)b;
	if (unlikely(!prev || !next))
		return 0;

	if (prev->sum_exec_scale > next->sum_exec_scale)
		return -1;
	else if (prev->sum_exec_scale < next->sum_exec_scale)
		return 1;
	else
		return 0;
}

static inline int cal_util(u64 sum_exec_scale, u64 window_size)
{
	int util;

	if (unlikely(window_size <= 0))
		return 0;

	util = sum_exec_scale / (window_size >> 10);
	if (util > 1024)
		util = 1024;

	return util;
}

static int heavy_task_info_show(struct seq_file *m, void *v)
{
	char *page;
	struct task_runtime_info *results;
	int i, j, num, util, result_num;
	char task_name[TASK_COMM_LEN];
	ssize_t len = 0;
	u64 now, window_size;
	struct game_task_struct *game_task;

	page = kzalloc(RESULT_PAGE_SIZE * PROCESS_PID_COUNT, GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	results = kmalloc(sizeof(struct task_runtime_info) * MAX_TID_COUNT, GFP_KERNEL);
	if (!results) {
		kfree(page);
		return -ENOMEM;
	}
	now = ktime_get_raw_ns();

	for (i = 0; i < process_num; i++) {
		game_task = get_game_task_struct_by_pid(process_pids[i]);
		if (!game_task)
			continue;
		if (atomic_read(&game_task->mtu_info.have_valid_process_pids) == 0)
			continue;

		raw_spin_lock(&g_lock);
		for (j = 0; j < game_task->mtu_info.child_num; j++) {
			results[j].pid = game_task->mtu_info.child_threads[j].pid;
			results[j].task = game_task->mtu_info.child_threads[j].task;
			results[j].sum_exec_scale = game_task->mtu_info.child_threads[j].sum_exec_scale;
		}
		result_num = game_task->mtu_info.child_num;
		game_task->mtu_info.child_num = 0;
		window_size = now - game_task->mtu_info.window_start;
		game_task->mtu_info.window_start = now;
		raw_spin_unlock(&g_lock);
		sort(results, result_num, sizeof(struct task_runtime_info),
			&cmp_task_sum_exec_scale, NULL);

		num = 0;
		for (j = 0; j < result_num; j++) {
			util = cal_util(results[j].sum_exec_scale, window_size);
			if (util <= 0) {
				break;
			}
			if (get_task_name(results[j].pid, results[j].task, task_name)) {
				len += snprintf(page + len, RESULT_PAGE_SIZE - len, "%d;%s;%d\n",
					results[j].pid, task_name, util);
				if (++num >= MAX_TASK_NR) {
					break;
				}
			}
		}
		if (i + 1 < process_num) {
			len += snprintf(page + len, RESULT_PAGE_SIZE - len, "\n");
		}
	}

	if (len > 0)
		seq_puts(m, page);

	kfree(results);
	kfree(page);

	return 0;
}

static int heavy_task_info_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, heavy_task_info_show, inode);
}

static const struct proc_ops heavy_task_info_proc_ops = {
	.proc_open		= heavy_task_info_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

#define DIV64_U64_ROUNDUP(X, Y) div64_u64((X) + (Y - 1), Y)
static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
	u64 task_exec_scale;
	unsigned int cur_freq, max_freq;
	int cpu = cpu_of(rq);

	cur_freq = get_cur_freq(cpu);
	max_freq = get_max_freq(cpu);

	if (unlikely(cur_freq <= 0) || unlikely(max_freq <= 0) || unlikely(cur_freq > max_freq))
		return delta;

	task_exec_scale = DIV64_U64_ROUNDUP(cur_freq *
				arch_scale_cpu_capacity(cpu),
				max_freq);

	return (delta * task_exec_scale) >> 10;
}

static struct task_runtime_info *find_child_thread(struct task_struct *task, struct game_task_struct *tg_g_task)
{
	int i;

	for (i = 0; i < tg_g_task->mtu_info.child_num; i++) {
		if ((tg_g_task->mtu_info.child_threads[i].task == task) &&
				(tg_g_task->mtu_info.child_threads[i].pid == task->pid))
			return &tg_g_task->mtu_info.child_threads[i];
	}

	return NULL;
}

static inline void update_task_runtime(struct task_struct *task, u64 runtime)
{
	u64 exec_scale;
	struct rq *rq = task_rq(task);
	struct task_runtime_info *child_thread;
	struct task_struct *tg_task = NULL;
	struct game_task_struct *tg_g_task = NULL;

	if (task == NULL || atomic_read(&enable_multi_task_util) == 0) {
		return;
	}

	rcu_read_lock();
	tg_task = rcu_dereference(task->group_leader);

	if (!ts_to_gts(tg_task, &tg_g_task)) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();

	if (atomic_read(&tg_g_task->mtu_info.is_tracked) == 0 ||
		atomic_read(&tg_g_task->mtu_info.have_valid_process_pids) == 0) {
		return;
	}

	/*
	 * only stat runtime when lock is available,
	 * if not available, skip.
	 */
	if (raw_spin_trylock(&g_lock)) {
		exec_scale = scale_exec_time(runtime, rq);

		child_thread = find_child_thread(task, tg_g_task);
		if (!child_thread) {
			if (tg_g_task->mtu_info.child_num >= MAX_TID_COUNT) {
				goto unlock;
			}
			child_thread = &tg_g_task->mtu_info.child_threads[tg_g_task->mtu_info.child_num];
			child_thread->pid = task->pid;
			child_thread->task = task;
			child_thread->sum_exec_scale = exec_scale;
			tg_g_task->mtu_info.child_num++;
		} else {
			child_thread->sum_exec_scale += exec_scale;
		}

unlock:
		raw_spin_unlock(&g_lock);
	}
}

static void sched_stat_runtime_hook(void *unused, struct task_struct *p, u64 runtime, u64 vruntime)
{
	update_task_runtime(p, runtime);
}

static void register_task_util_vendor_hooks(void)
{
	/* Register vender hook in kernel/sched/fair.c{rt.c|deadline.c} */
	register_trace_sched_stat_runtime(sched_stat_runtime_hook, NULL);
}

int multi_task_util_init(void)
{
	register_task_util_vendor_hooks();

	proc_create_data("multi_task_ctrl", 0664, multi_task_dir, &multi_task_util_ctrl_proc_ops, NULL);
	proc_create_data("multi_task_util_enable", 0664, multi_task_dir, &multi_task_util_enable_proc_ops, NULL);
	proc_create_data("multi_task_pid", 0664, multi_task_dir, &task_pid_proc_ops, NULL);
	proc_create_data("multi_heavy_task_info", 0444, multi_task_dir, &heavy_task_info_proc_ops, NULL);
	proc_create_data("multi_ui_assist_thread", 0444, multi_task_dir, &ui_assist_thread_proc_ops, NULL);

	return 0;
}

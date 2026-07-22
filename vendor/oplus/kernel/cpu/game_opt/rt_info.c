// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <trace/events/sched.h>
#include <trace/hooks/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sort.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/spinlock.h>

#include "game_ctrl.h"

#include "task_boost/heavy_task_boost.h"
#include "frame_detect/frame_detect.h"
#include "critical_task_boost.h"

static struct render_related_thread related_threads[MAX_TID_COUNT];

pid_t related_threads_sorted[MAX_TID_COUNT];

static int rt_num = 0;
static int total_num = 0;
static int rt_num_sorted = 0;
static int total_num_sorted = 0;
static pid_t game_tgid = -1;

static DEFINE_RAW_SPINLOCK(rt_info_lock);
static DEFINE_RWLOCK(rt_info_sorted_rwlock);
atomic_t have_valid_render_pid = ATOMIC_INIT(0);

static void update_critical_task_pids(void);
static pid_t critical_task_pids[CRITICAL_TASK_NUM] = {-1, -1};
extern void update_ctb_pids(pid_t game_tgid, pid_t unitymain_pid, pid_t unitygfxdevice_pid);
extern void get_critical_task_name(char *unityMain_name, char *unityGfxDevice_name);

static inline bool same_rt_thread_group(struct task_struct *waker,
	struct task_struct *wakee)
{
	return (waker->tgid == game_tgid) && (wakee->tgid == game_tgid);
}

/*
 * surfaceflinger app thread start game logic every frame
 */
static inline bool sf_app_wakeup_game_thread(struct task_struct *waker,
		struct task_struct *wakee)
{
	struct game_task_struct *waker_gts = NULL;

	if ((waker->tgid == game_tgid) || (wakee->tgid != game_tgid))
		return false;

	if (ts_to_gts(waker, &waker_gts)) {
		if (waker_gts->thread_type.is_sf_app == THREAD_TYPE_YES)
			return true;
		else if (waker_gts->thread_type.is_sf_app == THREAD_TYPE_NO)
			return false;
		else {
			bool is_sf_app = !strcmp(waker->comm, "app") &&
				(waker->group_leader != NULL) && !strcmp(waker->group_leader->comm, "surfaceflinger");

			if (is_sf_app)
				waker_gts->thread_type.is_sf_app = THREAD_TYPE_YES;
			else
				waker_gts->thread_type.is_sf_app = THREAD_TYPE_NO;

			return is_sf_app;
		}
	}

	return false;
}

static struct render_related_thread *find_related_thread(struct task_struct *task)
{
	int i;

	for (i = 0; i < total_num; i++) {
		if ((related_threads[i].task == task) && (related_threads[i].pid == task->pid))
			return &related_threads[i];
	}

	return NULL;
}

static bool is_render_thread(struct render_related_thread * thread)
{
	int i;

	for (i = 0; i < rt_num; i++) {
		if (related_threads[i].pid == thread->pid)
			return true;
	}

	return false;
}

static bool is_UnityMain_thread(struct task_struct *task)
{
	struct game_task_struct *gts = NULL;

	if (ts_to_gts(task, &gts)) {
		if (gts->thread_type.is_unitymain == THREAD_TYPE_YES)
			return true;
		else if (gts->thread_type.is_unitymain == THREAD_TYPE_NO)
			return false;
		else {
			bool is_unitymain = !strcmp(task->comm, "UnityMain");

			if (is_unitymain)
				gts->thread_type.is_unitymain = THREAD_TYPE_YES;
			else
				gts->thread_type.is_unitymain = THREAD_TYPE_NO;

			return is_unitymain;
		}
	}

	return false;
}

static void try_to_wake_up_success_hook(void *unused, struct task_struct *task)
{
	struct render_related_thread *wakee;
	struct render_related_thread *waker;
	unsigned long flags;

	ui_assist_threads_wake_stat(task);
	ttwu_multi_rt_info_hook(task);

	if (atomic_read(&have_valid_render_pid) == 0)
		return;

	ed_render_wakeup_times_stat(task);
	ttwu_frame_detect_hook(task);

	/*
	 * ignore wakeup event if waker or wakee
	 * not belong to a same game thread group.
	 */
	if (!(same_rt_thread_group(current, task) || sf_app_wakeup_game_thread(current, task)))
		return;

	/*
	 * only update wake stat when lock is available,
	 * if not available, skip.
	 */
	if (raw_spin_trylock_irqsave(&rt_info_lock, flags)) {
		if (sf_app_wakeup_game_thread(current, task)) {
			wakee = find_related_thread(task);
			if (!wakee) {
				if (total_num >= MAX_TID_COUNT)
					goto unlock;
				wakee = &related_threads[total_num];
				wakee->pid = task->pid;
				wakee->task = task;
				wakee->wake_count = 1;
				total_num++;
			} else {
				wakee->wake_count++;
			}

			goto unlock;
		}

		if (!same_rt_thread_group(current, task))
			goto unlock;

		/* wakee is a render related thread */
		wakee = find_related_thread(task);
		if (wakee) {
			waker = find_related_thread(current);
			if (!waker) {
				if (total_num >= MAX_TID_COUNT)
					goto unlock;
				waker = &related_threads[total_num];
				waker->pid = current->pid;
				waker->task = current;
				waker->wake_count = 1;
				total_num++;
			} else {
				waker->wake_count++;
			}

			if (is_render_thread(wakee) || is_UnityMain_thread(current) || is_UnityMain_thread(task))
				wakee->wake_count++;
		} else {
			/* waker is a sepcific render related thread */
			waker = find_related_thread(current);
			if (waker && (is_render_thread(waker) || is_UnityMain_thread(current))) {
				if (total_num >= MAX_TID_COUNT)
					goto unlock;
				wakee = &related_threads[total_num];
				wakee->pid = task->pid;
				wakee->task = task;
				wakee->wake_count = 1;
				total_num++;

				waker->wake_count++;
			}
		}

unlock:
		raw_spin_unlock_irqrestore(&rt_info_lock, flags);
	}
	heavy_task_boost(task, related_threads, total_num);
}

static bool need_tracked_task(char *name)
{
	bool skip = strstr(name, "binder:") || strstr(name, "HwBinder:") ||
				strstr(name, "AudioTrack") || strstr(name, "NativeThread");

	return !skip;
}

/*
 * Ascending order by wake_count
 */
static int cmp_task_wake_count(const void *a, const void *b)
{
	struct render_related_thread *prev, *next;

	prev = (struct render_related_thread *)a;
	next = (struct render_related_thread *)b;
	if (unlikely(!prev || !next))
		return 0;

	if (prev->wake_count > next->wake_count)
		return -1;
	else if (prev->wake_count < next->wake_count)
		return 1;
	else
		return 0;
}

static int rt_info_show(struct seq_file *m, void *v)
{
	reset_critical_task_time();
	int i, result_num, gl_num;
	struct render_related_thread *results;
	char *page;
	char task_name[TASK_COMM_LEN];
	pid_t tracked_pids[MAX_TRACKED_TASK_NUM];
	int tracked_pid_num = 0;
	ssize_t len = 0;
	unsigned long flags;
	pid_t logic_thread;

	if (atomic_read(&have_valid_render_pid) == 0)
		return -ESRCH;

	page = kzalloc(RESULT_PAGE_SIZE, GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	results = kmalloc(sizeof(struct render_related_thread) * MAX_TID_COUNT, GFP_KERNEL);
	if (!results) {
		kfree(page);
		return -ENOMEM;
	}

	raw_spin_lock_irqsave(&rt_info_lock, flags);
	for (i = 0; i < total_num; i++) {
		results[i].pid = related_threads[i].pid;
		results[i].task = related_threads[i].task;
		results[i].wake_count = related_threads[i].wake_count;
	}

	for (i = 0; i < rt_num; i++)
		related_threads[i].wake_count = 0;
	result_num = total_num;
	total_num_sorted = total_num;
	gl_num = rt_num;
	rt_num_sorted = rt_num;
	total_num = rt_num;
	raw_spin_unlock_irqrestore(&rt_info_lock, flags);

	if (unlikely(gl_num > 1)) {
		sort(&results[0], gl_num,
			sizeof(struct render_related_thread), &cmp_task_wake_count, NULL);
	}

	if (result_num > gl_num) {
		sort(&results[gl_num], result_num - gl_num,
			sizeof(struct render_related_thread), &cmp_task_wake_count, NULL);
	}

	read_lock(&rt_info_sorted_rwlock);
	for (i = 0; i < result_num && i < MAX_TASK_NR; i++) {
		if (get_task_name(results[i].pid, results[i].task, task_name)) {
			if ((tracked_pid_num < MAX_TRACKED_TASK_NUM) && need_tracked_task(task_name)) {
				tracked_pids[tracked_pid_num] = results[i].pid;
				tracked_pid_num++;
			}

			len += snprintf(page + len, RESULT_PAGE_SIZE - len, "%d;%s;%u\n",
				results[i].pid, task_name, results[i].wake_count);
		}
		related_threads_sorted[i] = results[i].pid;
	}

	if (rt_info_top_k_locked(1, &logic_thread)) {
		set_frame_detect_task(TASK_INFO_LOGIC_THREAD, logic_thread);
	}
	read_unlock(&rt_info_sorted_rwlock);

	if (tracked_pid_num > 0)
		add_tasks_to_frame_group(tracked_pids, tracked_pid_num);

	if (len > 0)
		seq_puts(m, page);

	kfree(results);
	kfree(page);

	return 0;
}

static int rt_info_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, rt_info_show, inode);
}

static inline bool is_repetitive_pid(pid_t pid)
{
	int i;

	for (i = 0; i < rt_num; i++) {
		if (pid == related_threads[i].pid)
			return true;
	}

	return false;
}

static ssize_t rt_info_proc_write(struct file *file, const char __user *buf,
	size_t count, loff_t *ppos)
{
	int i, ret;
	char page[128] = {0};
	char *iter = page;
	struct task_struct *task;
	pid_t pid;
	unsigned long flags;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	raw_spin_lock_irqsave(&rt_info_lock, flags);

	update_critical_task_pids();

	atomic_set(&have_valid_render_pid, 0);

	for (i = 0; i < rt_num; i++) {
		if (related_threads[i].task)
			put_task_struct(related_threads[i].task);
	}

	rt_num = 0;
	total_num = 0;
	rt_num_sorted = 0;
	total_num_sorted = 0;
	game_tgid = -1;
	ed_set_render_task(NULL);

	while (iter != NULL) {
		/* input should be "123 234" */
		ret = sscanf(iter, "%d", &pid);
		if (ret != 1)
			break;

		iter = strchr(iter + 1, ' ');

		/* skip repetitive pid */
		if (is_repetitive_pid(pid))
			continue;

		rcu_read_lock();
		task = find_task_by_vpid(pid);
		if (task)
			get_task_struct(task);
		rcu_read_unlock();

		if (task) {
			if (game_tgid == -1) {
				game_tgid = task->tgid;
			} else {
				/* all rt threads should belong to a group */
				if (game_tgid != task->tgid) {
					put_task_struct(task);
					continue;
				}
			}

			related_threads[rt_num].pid = pid;
			related_threads[rt_num].task = task;
			related_threads[rt_num].wake_count = 0;

			rt_num++;
		}
	}

	if (rt_num) {
		total_num = rt_num;
		atomic_set(&have_valid_render_pid, 1);
		if (rt_num == 1) {
			ed_set_render_task(related_threads[0].task);
		}
	}

	raw_spin_unlock_irqrestore(&rt_info_lock, flags);

	return count;
}

static const struct proc_ops rt_info_proc_ops = {
	.proc_open		= rt_info_proc_open,
	.proc_write		= rt_info_proc_write,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

static int rt_num_show(struct seq_file *m, void *v)
{
	char page[256] = {0};
	ssize_t len = 0;
	int i;
	unsigned long flags;

	raw_spin_lock_irqsave(&rt_info_lock, flags);
	len += snprintf(page + len, sizeof(page) - len, "rt_num=%d total_num=%d\n",
		rt_num, total_num);
	for (i = 0; i < rt_num; i++) {
		len += snprintf(page + len, sizeof(page) - len, "tgid:%d pid:%d comm:%s\n",
			related_threads[i].task->tgid, related_threads[i].task->pid,
			related_threads[i].task->comm);
	}
	raw_spin_unlock_irqrestore(&rt_info_lock, flags);

	seq_puts(m, page);

	return 0;
}

static int rt_num_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, rt_num_show, inode);
}

static const struct proc_ops rt_num_proc_ops = {
	.proc_open		= rt_num_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

static void register_rt_info_vendor_hooks(void)
{
	/* Register vender hook in kernel/sched/core.c */
	register_trace_android_rvh_try_to_wake_up_success(try_to_wake_up_success_hook, NULL);
}

bool rt_info_top_k_locked(int k, pid_t *pid)
{
	if (!rt_num_sorted || (unsigned int)k > total_num_sorted - rt_num_sorted) {
		return false;
	}
	*pid = related_threads_sorted[rt_num_sorted + k - 1];
	return true;
}

bool rt_info_top_k(int k, pid_t *pid)
{
	bool ret;

	if (!read_trylock(&rt_info_sorted_rwlock)) {
		return false;
	}
	ret = rt_info_top_k_locked(k, pid);
	read_unlock(&rt_info_sorted_rwlock);
	return ret;
}

int check_task_name(const char *name)
{
	int name_len;
	if (!name || strlen(name) >= TASK_COMM_LEN) {
		return -1;
	}
	if (total_num <= 0 || atomic_read(&have_valid_render_pid) == 0) {
		return -1;
	}
	name_len = strlen(name);
	return name_len;
}

int get_critical_task_by_name(const char *name, struct task_struct **task)
{
	int name_len, i;
	unsigned long flags;

	name_len = check_task_name(name);

	if (name_len < 0)
		return -1;

	raw_spin_lock_irqsave(&rt_info_lock, flags);
	for (i = 0; i < total_num; i++) {
		if (strncmp(name, related_threads[i].task->comm, name_len) == 0) {
			*task = get_pid_task(find_vpid(related_threads[i].pid), PIDTYPE_PID);
			break;
		}
	}
	raw_spin_unlock_irqrestore(&rt_info_lock, flags);
	return 0;
}

/*
 * Check if a thread matches the critical task name.
 */
static bool is_matching_thread(const char *task_name, int name_len,
								const char *thread_name, pid_t *last_pid,
								pid_t current_pid)
{
	if (strncmp(task_name, thread_name, name_len) != 0)
		return false;

	if (*last_pid != -1 && *last_pid == current_pid)
		return false;

	*last_pid = current_pid;
	return true;
}

static pid_t find_critical_task_pid(const char *task_name, int name_len,
	  									int local_total_num, pid_t *last_pid)
{
	int j;
	char thread_name[TASK_COMM_LEN];

	for (j = 0; j < local_total_num; j++) {
		if (!related_threads[j].task ||
				!get_task_name(related_threads[j].pid,
								related_threads[j].task,
								thread_name))
		continue;

	if (is_matching_thread(task_name, name_len, thread_name,
							last_pid, related_threads[j].pid))
		return *last_pid;
	}

	return -1;
}

/*
 * Update critical task PIDs by searching in related_threads.
 * Note: Caller must hold rt_info_lock.
 */
static void update_critical_task_pids(void)
{
	int name_len, i;
	char critical_task[CRITICAL_TASK_NUM][100] = {0};
	int local_total_num;
	pid_t last_pid = -1;

	if (!get_ctb_enable() && !get_htb_enable())
		return;

	get_critical_task_name(critical_task[0], critical_task[1]);
	local_total_num = total_num;

	for (i = CRITICAL_TASK_NUM - 1; i >= 0; i--) {
		critical_task_pids[i] = -1;

		name_len = check_task_name(critical_task[i]);
		if (name_len < 0)
			continue;

		critical_task_pids[i] = find_critical_task_pid(critical_task[i],
														name_len,
														local_total_num,
														&last_pid);
	}

	update_ctb_pids(game_tgid, critical_task_pids[0], critical_task_pids[1]);
}

int rt_info_init(void)
{
	register_rt_info_vendor_hooks();

	proc_create_data("rt_info", 0664, game_opt_dir, &rt_info_proc_ops, NULL);
	proc_create_data("rt_num", 0444, game_opt_dir, &rt_num_proc_ops, NULL);

	return 0;
}

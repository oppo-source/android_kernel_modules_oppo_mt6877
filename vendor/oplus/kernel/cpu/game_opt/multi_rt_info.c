#include <trace/events/sched.h>
#include <trace/hooks/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sort.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

#include "game_ctrl.h"

#include "frame_detect/frame_detect.h"

static int rt_group_num = 0;
pid_t rt_process_tgid[PROCESS_PID_COUNT];

static DEFINE_RAW_SPINLOCK(multi_rt_info_lock);
static DEFINE_RWLOCK(multi_rt_info_sorted_rwlock);

static inline bool same_rt_thread_group(struct task_struct *waker,
	struct task_struct *wakee, struct task_struct *tg_task)
{
	return (waker->tgid == tg_task->tgid) && (wakee->tgid == tg_task->tgid);
}

static inline bool sf_app_wakeup_game_thread(struct task_struct *waker,
		struct task_struct *wakee, struct task_struct *tg_task)
{
	return (wakee->tgid == tg_task->tgid) && !strcmp(waker->comm, "app") &&
		(waker->group_leader != NULL) && !strcmp(waker->group_leader->comm, "surfaceflinger");
}

static struct render_related_thread *find_related_thread(struct task_struct *task, struct game_task_struct *tg_g_task)
{
	int i;
	for (i = 0; i < tg_g_task->mrt_info.total_num; i++) {
		if ((tg_g_task->mrt_info.related_threads[i].task == task) && (tg_g_task->mrt_info.related_threads[i].pid == task->pid))
			return &tg_g_task->mrt_info.related_threads[i];
	}
	return NULL;
}

static bool is_render_thread(struct render_related_thread * thread, struct game_task_struct *tg_g_task)
{
	int i;
	for (i = 0; i < tg_g_task->mrt_info.rt_num; i++) {
		if (tg_g_task->mrt_info.related_threads[i].pid == thread->pid)
			return true;
	}
	return false;
}

static bool is_sepcific_thread(struct task_struct *task)
{
	return !strcmp(task->comm, "UnityMain");
}

static bool add_related_threads(struct render_related_thread **task,
								struct task_struct *target_task,
								struct game_task_struct *tg_g_task)
{
	if (tg_g_task->mrt_info.total_num >= MAX_TID_COUNT) {
		return false;
	}
	*task = &tg_g_task->mrt_info.related_threads[tg_g_task->mrt_info.total_num];
	(*task)->pid = target_task->pid;
	(*task)->task = target_task;
	(*task)->wake_count = 1;
	tg_g_task->mrt_info.total_num++;
	return true;
}

void ttwu_multi_rt_info_hook(struct task_struct *task)
{
	struct render_related_thread *wakee;
	struct render_related_thread *waker;
	struct task_struct *tg_task = NULL;
	struct game_task_struct *tg_g_task = NULL;
	unsigned long flags;

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
		atomic_read(&tg_g_task->mrt_info.have_valid_render_pids) == 0) {
		return;
	}

	if (!(same_rt_thread_group(current, task, tg_task) ||
			sf_app_wakeup_game_thread(current, task, tg_task))) {
		return;
	}

	if (raw_spin_trylock_irqsave(&multi_rt_info_lock, flags)) {
		if (sf_app_wakeup_game_thread(current, task, tg_task)) {
			wakee = find_related_thread(task, tg_g_task);
			if (!wakee) {
				if (!add_related_threads(&wakee, task, tg_g_task)) {
					goto unlock;
				}
			} else {
				wakee->wake_count++;
			}
			goto unlock;
		}

		if (!same_rt_thread_group(current, task, tg_task)) {
			goto unlock;
		}

		/* wakee is a render related thread */
		wakee = find_related_thread(task, tg_g_task);
		if (wakee) {
			waker = find_related_thread(current, tg_g_task);
			if (!waker) {
				if (!add_related_threads(&waker, current, tg_g_task)) {
					goto unlock;
				}
			} else {
				waker->wake_count++;
			}

			if (is_render_thread(wakee, tg_g_task) || is_sepcific_thread(current) || is_sepcific_thread(task))
				wakee->wake_count++;
		} else {
			/* waker is a sepcific render related thread */
			waker = find_related_thread(current, tg_g_task);
			if (waker && (is_render_thread(waker, tg_g_task) || is_sepcific_thread(current))) {
				if (!add_related_threads(&wakee, task, tg_g_task)) {
					goto unlock;
				}
				waker->wake_count++;
			}
		}
unlock:
		raw_spin_unlock_irqrestore(&multi_rt_info_lock, flags);
	}
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

static void copy_and_reset_related_threads(int *result_num, int *gl_num,
										   struct render_related_thread *results,
										   struct game_task_struct *game_task)
{
	int i;
	unsigned long flags;
	raw_spin_lock_irqsave(&multi_rt_info_lock, flags);
	for (i = 0; i < game_task->mrt_info.total_num; i++) {
		results[i].pid = game_task->mrt_info.related_threads[i].pid;
		results[i].task = game_task->mrt_info.related_threads[i].task;
		results[i].wake_count = game_task->mrt_info.related_threads[i].wake_count;
	}
	for (i = 0; i < game_task->mrt_info.rt_num; i++) {
		game_task->mrt_info.related_threads[i].wake_count = 0;
	}
	*result_num = game_task->mrt_info.total_num;
	*gl_num = game_task->mrt_info.rt_num;
	game_task->mrt_info.total_num = game_task->mrt_info.rt_num;
	game_task->mrt_info.total_num_sorted = game_task->mrt_info.total_num;
	game_task->mrt_info.rt_num_sorted = game_task->mrt_info.rt_num;
	raw_spin_unlock_irqrestore(&multi_rt_info_lock, flags);
}

static int multi_rt_info_show(struct seq_file *m, void *v)
{
	int i, j, result_num, gl_num;
	struct render_related_thread *results;
	struct game_task_struct *game_task = NULL;
	char *page;
	char task_name[TASK_COMM_LEN];
	ssize_t len = 0;

 	page = kzalloc(RESULT_PAGE_SIZE * RT_PROCESS_GROUP_COUNT, GFP_KERNEL);
	if (!page) {
		return -ENOMEM;
	}
	results = kmalloc(sizeof(struct render_related_thread) * MAX_TID_COUNT, GFP_KERNEL);
	if (!results) {
		kfree(page);
		return -ENOMEM;
	}

	for (i = 0; i < rt_group_num; i++) {
		game_task = get_game_task_struct_by_pid(rt_process_tgid[i]);
		if (game_task == NULL) {
			continue;
		}
		if (atomic_read(&game_task->mrt_info.have_valid_render_pids) == 0) {
			continue;
		}
		copy_and_reset_related_threads(&result_num, &gl_num, results, game_task);

		if (unlikely(gl_num > 1)) {
			sort(&results[0], gl_num, sizeof(struct render_related_thread),
				&cmp_task_wake_count, NULL);
		}
		if (result_num > gl_num) {
			sort(&results[gl_num], result_num - gl_num, sizeof(struct render_related_thread),
				&cmp_task_wake_count, NULL);
		}

		read_lock(&multi_rt_info_sorted_rwlock);
		for (j = 0; j < result_num && j < MAX_TASK_NR; j++) {
			if (get_task_name(results[j].pid, results[j].task, task_name)) {
				len += snprintf(page + len, RESULT_PAGE_SIZE - len, "%d;%s;%u\n",
					results[j].pid, task_name, results[j].wake_count);
			}
			game_task->mrt_info.related_threads_sorted[j] = results[j].pid;
		}
		if (i + 1 < rt_group_num) {
			len += snprintf(page + len, RESULT_PAGE_SIZE - len, "\n");
		}
		read_unlock(&multi_rt_info_sorted_rwlock);
	}

	if (len > 0)
		seq_puts(m, page);

	kfree(results);
	kfree(page);

	return 0;
}

static int multi_rt_info_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, multi_rt_info_show, inode);
}

static inline bool is_repetitive_pid(pid_t pid, struct game_task_struct *tg_g_task)
{
	int i;
	bool result = false;
	for (i = 0; i < tg_g_task->mrt_info.rt_num; i++) {
		if (pid == tg_g_task->mrt_info.related_threads[i].pid) {
			result = true;
			break;
		}
	}
	return result;
}

static void remove_rt_process_pid(pid_t pid)
{
	int i, target_idx;
	target_idx = -1;
	for (i = 0; i < rt_group_num; i++) {
		if (pid == rt_process_tgid[i]) {
			target_idx = i;
			break;
		}
	}
	if (target_idx == -1) {
		return;
	}
	for (i = target_idx; i < rt_group_num - 1; i++) {
		rt_process_tgid[i] = rt_process_tgid[i + 1];
	}
	rt_process_tgid[rt_group_num - 1] = -1;
	rt_group_num--;
}

static void add_rt_process_pid(pid_t pid)
{
	if (rt_group_num < 0 || rt_group_num >= RT_PROCESS_GROUP_COUNT) {
		return;
	}
	rt_process_tgid[rt_group_num++] = pid;
}

static bool check_rt_process_pid_exist(pid_t pid)
{
	int i;
	for (i = 0; i < rt_group_num; i++) {
		if (pid == rt_process_tgid[i]) {
			return true;
		}
	}
	return false;
}

static void reset_multi_rt_info_by_pid(pid_t pid)
{
	int i;
	struct game_task_struct *game_task = NULL;
	struct task_struct *task = NULL;

	if (!check_rt_process_pid_exist(pid)) {
		return;
	}

	game_task = get_game_task_struct_by_pid(pid);
	task = get_task_struct_by_pid(pid);

	if (game_task != NULL) {
		for (i = 0; i < game_task->mrt_info.rt_num; i++) {
			if (game_task->mrt_info.related_threads[i].task) {
				put_task_struct(game_task->mrt_info.related_threads[i].task);
			}
		}
		game_task->mrt_info.rt_num = 0;
		game_task->mrt_info.total_num = 0;
		game_task->mrt_info.rt_num_sorted = 0;
		game_task->mrt_info.total_num_sorted = 0;
		atomic_set(&game_task->mrt_info.have_valid_render_pids, 0);
		if (game_task->mrt_info.related_threads != NULL) {
			kfree(game_task->mrt_info.related_threads);
			game_task->mrt_info.related_threads = NULL;
		}
		if (game_task->mrt_info.related_threads_sorted != NULL) {
			kfree(game_task->mrt_info.related_threads_sorted);
			game_task->mrt_info.related_threads_sorted = NULL;
		}
	}
	if (task != NULL) {
		put_task_struct(task);
	}
	remove_rt_process_pid(pid);
}

static void reset_multi_rt_info(void)
{
	int i;
	for (i = 0; i < rt_group_num; i++) {
		reset_multi_rt_info_by_pid(rt_process_tgid[i]);
	}
}

static void set_multi_rt_info_pid(pid_t pid, struct game_task_struct *tg_g_task)
{
	struct task_struct *task = NULL;

	if (tg_g_task == NULL || pid <= 0)
		return;
	if (is_repetitive_pid(pid, tg_g_task)) {
		return;
	}

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();

	if (!task)
		return;

	if (rt_group_num < 0 || rt_group_num >= RT_PROCESS_GROUP_COUNT) {
		put_task_struct(task);
		return;
	}

	if (rt_group_num == 0 || !check_rt_process_pid_exist(task->tgid)) {
		add_rt_process_pid(task->tgid);
	} else if (tg_g_task->task->tgid != task->tgid) {
		put_task_struct(task);
		return;
	}

	if (tg_g_task->mrt_info.rt_num < 0 || tg_g_task->mrt_info.rt_num >= MAX_TID_COUNT) {
		put_task_struct(task);
		return;
	}

	tg_g_task->mrt_info.related_threads = kmalloc(sizeof(struct render_related_thread) * MAX_TID_COUNT, GFP_KERNEL);
	if (!tg_g_task->mrt_info.related_threads) {
		put_task_struct(task);
		return;
	}
	tg_g_task->mrt_info.related_threads_sorted = kmalloc(sizeof(pid_t) * MAX_TID_COUNT, GFP_KERNEL);
	if (!tg_g_task->mrt_info.related_threads_sorted) {
		kfree(tg_g_task->mrt_info.related_threads);
		put_task_struct(task);
		return;
	}

	tg_g_task->mrt_info.related_threads[tg_g_task->mrt_info.rt_num].pid = pid;
	tg_g_task->mrt_info.related_threads[tg_g_task->mrt_info.rt_num].task = task;
	tg_g_task->mrt_info.related_threads[tg_g_task->mrt_info.rt_num].wake_count = 0;
	tg_g_task->mrt_info.rt_num++;
}

static bool set_multi_rt_info_status(struct game_task_struct *tg_g_task)
{
	if (tg_g_task->mrt_info.rt_num > 0) {
		tg_g_task->mrt_info.total_num = tg_g_task->mrt_info.rt_num;
		atomic_set(&tg_g_task->mrt_info.have_valid_render_pids, 1);
		if (rt_group_num >= RT_PROCESS_GROUP_COUNT) {
			return false;
		}
	}
	return true;
}

/**
 * 1. use tgid to record the multi rt info, so use pid to find the related tgid
 * 2. for example: tgid1 pid pid pid#tgid2 pid pid pid
 */
static ssize_t multi_rt_info_proc_write(struct file *file, const char __user *buf,
	size_t count, loff_t *ppos)
{
	int ret;
	bool is_tgid;
	char page[1024] = {0};
	char *iter, *line, *line_copy;
	pid_t pid;
	struct game_task_struct *game_task = NULL;
	unsigned long flags;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	raw_spin_lock_irqsave(&multi_rt_info_lock, flags);
	reset_multi_rt_info();

	line = page;
	while ((line_copy = strsep(&line, "#")) && line_copy != NULL) {
		iter = line_copy;
		is_tgid = true;
		game_task = NULL;
		while (iter != NULL) {
			ret = sscanf(iter, "%d", &pid);
			if (ret != 1) {
				break;
			}
			iter = strchr(iter + 1, ' ');
			if (is_tgid) {
				is_tgid = false;
				game_task = get_game_task_struct_and_task_struct_by_pid(pid);
				if (!game_task)
					break;
			} else {
				set_multi_rt_info_pid(pid, game_task);
			}
		}
		if (!game_task || !set_multi_rt_info_status(game_task)) {
			break;
		}
	}
	raw_spin_unlock_irqrestore(&multi_rt_info_lock, flags);

	return count;
}

static const struct proc_ops multi_rt_info_proc_ops = {
	.proc_open		= multi_rt_info_proc_open,
	.proc_write		= multi_rt_info_proc_write,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

/**
 * data->data  ->   tgid1,pid1,pid2,pid3;
 * 					tgid2,pid4,pid5;
 */
static void set_multi_rt_info_pids(struct multi_rt_ctrl_info *data)
{
	int i;
	unsigned long flags;
	struct game_task_struct *game_task = NULL;
	if (data == NULL || data->size <= 1) {
		return;
	}

	raw_spin_lock_irqsave(&multi_rt_info_lock, flags);

	reset_multi_rt_info_by_pid(data->data[0]);
	game_task = get_game_task_struct_and_task_struct_by_pid(data->data[0]);
	if (game_task == NULL) {
		goto unlock;
	}

	for (i = 1; i < data->size; i++) {
		set_multi_rt_info_pid(data->data[i], game_task);
	}
	set_multi_rt_info_status(game_task);

unlock:
	raw_spin_unlock_irqrestore(&multi_rt_info_lock, flags);
}

static long multi_rt_info_ctrl_proc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct multi_rt_ctrl_info data;
	void __user *uarg = (void __user *)arg;
	long ret = 0;
	if ((_IOC_TYPE(cmd) != MULTI_RT_INFO_MAGIC) || (_IOC_NR(cmd) >= MULTI_RT_MAX_ID)) {
		return -EINVAL;
	}
	if (copy_from_user(&data, uarg, sizeof(data))) {
		return -EFAULT;
	}
	switch (cmd) {
	case CMD_ID_MULTI_RT_PIDS:
		set_multi_rt_info_pids(&data);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int multi_rt_info_ctrl_proc_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct proc_ops multi_rt_info_ctrl_proc_ops = {
	.proc_ioctl		= multi_rt_info_ctrl_proc_ioctl,
	.proc_open		= multi_rt_info_ctrl_proc_open,
	.proc_lseek		= default_llseek,
};

static int multi_rt_num_show(struct seq_file *m, void *v)
{
	char *page;
	ssize_t len = 0;
	int i, j;
	unsigned long flags;
	size_t max_len = RESULT_PAGE_SIZE * RT_PROCESS_GROUP_COUNT;
	struct game_task_struct *game_task = NULL;

	page = kmalloc(max_len, GFP_KERNEL);
	if (!page) {
		return -ENOMEM;
	}

	raw_spin_lock_irqsave(&multi_rt_info_lock, flags);
	for (i = 0; i < rt_group_num; i++) {
		game_task = get_game_task_struct_by_pid(rt_process_tgid[i]);
		if (!game_task)
			continue;
		len += snprintf(page + len, max_len - len, "rt_num=%d total_num=%d\n",
			game_task->mrt_info.rt_num, game_task->mrt_info.total_num);
		for (j = 0; j < game_task->mrt_info.rt_num; j++) {
			if (game_task->mrt_info.related_threads[j].task) {
				len += snprintf(page + len, max_len - len, "tgid:%d pid:%d comm:%s\n",
					game_task->mrt_info.related_threads[j].task->tgid,
					game_task->mrt_info.related_threads[j].task->pid,
					game_task->mrt_info.related_threads[j].task->comm);
			}
		}
		if (i + 1 < rt_group_num) {
			len += snprintf(page + len, max_len - len, "\n");
		}
	}
	raw_spin_unlock_irqrestore(&multi_rt_info_lock, flags);

	if (len > 0) {
		seq_puts(m, page);
	}

	kfree(page);

	return 0;
}

static int multi_rt_num_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, multi_rt_num_show, inode);
}

static const struct proc_ops multi_rt_num_proc_ops = {
	.proc_open		= multi_rt_num_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

int multi_rt_info_init(void)
{
	proc_create_data("multi_rt_ctrl", 0664, multi_task_dir, &multi_rt_info_ctrl_proc_ops, NULL);
	proc_create_data("multi_rt_info", 0664, multi_task_dir, &multi_rt_info_proc_ops, NULL);
	proc_create_data("multi_rt_num", 0444, multi_task_dir, &multi_rt_num_proc_ops, NULL);

	return 0;
}

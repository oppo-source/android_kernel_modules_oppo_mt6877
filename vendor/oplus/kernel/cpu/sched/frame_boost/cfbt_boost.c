/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#include "cfbt_boost_struct.h"
#include "cfbt_boost.h"
#include "cfbt_rescue.h"
#include "frame_boost.h"
#include "frame_debug.h"
#include <linux/sched.h>
#include <kernel/sched/sched.h>
#include <linux/sched/cpufreq.h>
#include <linux/cpufreq.h>
#include <linux/sched/task.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <../kernel/oplus_cpu/sched/sched_assist/sa_common.h>
#include "cfbt_trace.h"
#include "cfbt_config.h"
#define DEFAULT_FRAME_RATE (60)
#define DEFAULT_MAX_FRAME_INTERVAL (CAMERA_FRAME_INTERVAL + (CAMERA_FRAME_INTERVAL > 1))
extern struct list_head cluster_head;

#define for_each_sched_cluster(cluster) \
	list_for_each_entry_rcu(cluster, &cluster_head, list)

#define DIV64_U64_ROUNDUP(X, Y) div64_u64((X) + (Y - 1), Y)

struct cfbt_frame_group *pipe_frame_groups[MAX_NUM_FBG_ID];
static int cfbt_currnet_idx = 0;
static int cfbt_version = 20241219;
static int cfbt_current_scene = 0;
static int cfbt_grp_util[CFBT_MAX_GROUP_NUM];
struct key_thread_common_pool *pipe_thread_pool;
static DEFINE_RAW_SPINLOCK(cfbt_freq_protect_lock);
static int max_cluster_id = 1;

enum task_event {
	PUT_PREV_TASK	= 0,
	PICK_NEXT_TASK	= 1,
};

static inline unsigned long cfbt_get_frame_putil(int grp_id, u64 delta)
{
	unsigned long util = 0;

	util = div_u64((delta << SCHED_CAPACITY_SHIFT), get_target_time_for_scene(get_cfbt_current_scene()));
	return util;
}

bool in_common_pool(struct task_struct *task)
{
	int i;
	struct task_struct *tmp = NULL;

	raw_spin_lock(&pipe_thread_pool->common_pool_lock);
	for (i = 0; i < pipe_thread_pool->tail + 1; i++) {
		tmp = pipe_thread_pool->key_thread_pool[i];
		if (tmp && task == tmp) {
			raw_spin_unlock(&pipe_thread_pool->common_pool_lock);
			return true;
		}
	}
	raw_spin_unlock(&pipe_thread_pool->common_pool_lock);
	return false;
}

inline int get_cfbt_current_scene(void)
{
	return cfbt_current_scene;
}
EXPORT_SYMBOL(get_cfbt_current_scene);


static inline void cpufreq_update_util_wrap(struct rq *rq, unsigned int flags)
{
	unsigned long lock_flags;

	raw_spin_lock_irqsave(&cfbt_freq_protect_lock, lock_flags);
	cpufreq_update_util(rq, flags);
	raw_spin_unlock_irqrestore(&cfbt_freq_protect_lock, lock_flags);
}

static inline unsigned int get_max_freq(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);

	return (policy == NULL) ? 0 : policy->cpuinfo.max_freq;
}

static inline unsigned int get_cur_freq(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);

	return (policy == NULL) ? 0 : policy->cur;
}

int __cfbt_add_common_tids(struct cfbt_struct *data)
{
	struct task_struct *task = NULL;
	struct oplus_task_struct *ots = NULL;
	int i, tail_idx, thread_num, j;
	pid_t pid;
	unsigned long flags;

	raw_spin_lock_irqsave(&pipe_thread_pool->common_pool_lock, flags);

	for (i = 0; i < data->tid_count; i++) {
		tail_idx = pipe_thread_pool->tail;
		thread_num = pipe_thread_pool->thread_num;
		pid = data->tids[i];

		rcu_read_lock();
		task = find_task_by_vpid(pid);
		if ((task) && (thread_num < CFBT_MAX_THREAD_NUM)) {
			get_task_struct(task);
			rcu_read_unlock();
			ots = get_oplus_task_struct(task);
			if (IS_ERR_OR_NULL(ots))
				continue;
			ots->cfbt_cur_group = 1;
			ots->cfbt_running = true;
			pr_err("[CFBT]PID %d, comm %s insert into comm pool", task->pid, task->comm);
			//fath path,add task to tail
			if (pipe_thread_pool->key_thread_pool[tail_idx] == NULL) {
				pipe_thread_pool->key_thread_pool[tail_idx] = task;
				pipe_thread_pool->thread_num++;
				pipe_thread_pool->tail++;
				continue;
			}
			//slow path,insert the task
			for (j = 0; j < thread_num; j++) {
				if (unlikely(pipe_thread_pool->key_thread_pool[j] == NULL)) {
					pipe_thread_pool->key_thread_pool[j] = task;
					pipe_thread_pool->thread_num++;
					break;
				}
			}
		}
		else
			rcu_read_unlock();
	}
	raw_spin_unlock_irqrestore(&pipe_thread_pool->common_pool_lock, flags);
	data->header.ret = 0;
	pr_err("[CFBT KERNEL]%s %s %d is called success!", __FILE__, __FUNCTION__, __LINE__);
	return 0;
}
EXPORT_SYMBOL(__cfbt_add_common_tids);

int __cfbt_remove_common_tid(struct cfbt_struct *data)
{
	int i, thread_num;
	struct oplus_task_struct *ots = NULL;
	unsigned long flags;

	raw_spin_lock_irqsave(&pipe_thread_pool->common_pool_lock, flags);
	thread_num = pipe_thread_pool->thread_num;
	for (i = 0; i < CFBT_MAX_THREAD_NUM; i++) {
		if (pipe_thread_pool->key_thread_pool[i]) {
			ots = get_oplus_task_struct(pipe_thread_pool->key_thread_pool[i]);
			if (IS_ERR_OR_NULL(ots))
				continue;
			ots->cfbt_cur_group = -1;
			ots->cfbt_running = false;
			put_task_struct(pipe_thread_pool->key_thread_pool[i]);
			pipe_thread_pool->key_thread_pool[i] = NULL;
		}
	}
	pipe_thread_pool->thread_num = 0;
	pipe_thread_pool->tail = 0;
	raw_spin_unlock_irqrestore(&pipe_thread_pool->common_pool_lock, flags);
	data->header.ret = 0;
	return 0;
}
EXPORT_SYMBOL(__cfbt_remove_common_tid);

void cfbt_clear_frame_info(struct cfbt_frame_group *grp)
{
	int i;
	struct task_struct *p = NULL;
	struct oplus_task_struct *ots = NULL;
	unsigned long flags;

	raw_spin_lock_irqsave(&grp->lock, flags);
	grp->window_start = 0;
	grp->nr_running = 0;
	atomic_set(&grp->using, 0);
	atomic_set(&grp->need_clean, 0);
	grp->mark_start = 0;
	grp->stage_start_time = 0;
	grp->cfbt_key_thread_num = 0;
	grp->cfbt_key_thread_tail = 0;
	grp->frame_start_time = 0;
	grp->curr_window_scale = 0;
	grp->prev_window_scale = 0;
	grp->curr_window_exec = 0;
	grp->prev_window_exec = 0;
	grp->preferred_cluster = NULL;
	grp->available_cluster = NULL;
	grp->stage = 0;
	grp->util_stage_start = 0;
	grp->cfbt_grp_util_arr = cfbt_grp_util;
	grp->isRescuring = RESCUE_OF_NONE;
	grp->cur_enhance_util = 0;
	atomic64_set(&grp->policy_util, 0);
	cfbt_grp_util[grp->id] = 0;
	memset(&grp->stages_time, 0, sizeof(grp->stages_time));
	for (i = 0; i < CFBT_MAX_THREAD_NUM; i++) {
		p = grp->key_thread[i];
		if (p) {
			ots = get_oplus_task_struct(p);
			if (!IS_ERR_OR_NULL(ots)) {
				ots->cfbt_cur_group = -1;
				ots->cfbt_running = false;
			}
			grp->key_thread[i] = NULL;
		}
	}
	raw_spin_unlock_irqrestore(&grp->lock, flags);

}

struct cfbt_frame_group *cfbt_available_grp(void)
{
	int idx = cfbt_currnet_idx;
	int unsing;
	struct cfbt_frame_group *grp = NULL;

	do {
		unsing = 0;
		grp = pipe_frame_groups[idx];
		if (unlikely(grp == NULL)) {
			pr_err("[CFBT KERNEL]%s %s %d has a NULL group %d", __FILE__, __FUNCTION__, __LINE__, idx);
			idx = (idx + 1) % CFBT_MAX_GROUP_NUM;
			continue;
		}
		if (atomic_try_cmpxchg(&grp->using, &unsing, 1)) {
			cfbt_currnet_idx = (cfbt_currnet_idx + 1) % CFBT_MAX_GROUP_NUM;
			return grp;
		}
		idx = (idx + 1) % CFBT_MAX_GROUP_NUM;
	} while (idx % CFBT_MAX_GROUP_NUM != cfbt_currnet_idx);

	return NULL;
}

int cfbt_clear_all_frame_info(void)
{
	int i;
	struct cfbt_frame_group *grp = NULL;

	for (i = 0; i < CFBT_MAX_GROUP_NUM; i++) {
		grp = pipe_frame_groups[i];
		cfbt_clear_frame_info(grp);
	}
	pipe_thread_pool->mark_start = 0;
	return 0;
}

int cfbt_clear_frame_info_by_id(int frameid)
{
	int i;
	struct cfbt_frame_group *grp = NULL;

	for (i = 0; i < CFBT_MAX_GROUP_NUM; i++) {
		grp = pipe_frame_groups[i];
		if (grp->id == frameid) {
			cfbt_clear_frame_info(grp);
			return 0;
		}
	}
	return 0;
}

static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
	u64 task_exec_scale;
	unsigned int cur_freq, max_freq;
	int cpu = cpu_of(rq);

	/* TODO:
	 * Use freq_avg instead of freq_cur, because freq may trans when task running.
	 * Can we use this hook trace_android_rvh_cpufreq_transition?
	 */
	cur_freq = get_cur_freq(cpu);
	max_freq = get_max_freq(cpu);

	if (unlikely(cur_freq <= 0) || unlikely(max_freq <= 0) || unlikely(cur_freq > max_freq)) {
		ofb_err("cpu=%d cur_freq=%u max_freq=%u\n", cpu, cur_freq, max_freq);
		return delta;
	}

	task_exec_scale = DIV64_U64_ROUNDUP(cur_freq *
				arch_scale_cpu_capacity(cpu),
				max_freq);

	return (delta * task_exec_scale) >> 10;
}


int cfbt_get_frame_id(struct cfbt_struct *data, struct cfbt_frame_group *ret)
{
	unsigned long flags;
	struct cfbt_frame_group *grp = cfbt_available_grp();

	if (grp == NULL) {
		pr_err("[CFBT KERNEL]%s %s %d grp is a NULL", __FILE__, __FUNCTION__, __LINE__);
		return -1;
	}
	raw_spin_lock_irqsave(&grp->lock, flags);
	grp->frame_start_time = fbg_ktime_get_ns();
	grp->window_start = grp->frame_start_time;
	data->frame_id = grp->id;
	raw_spin_unlock_irqrestore(&grp->lock, flags);
	return 0;

}

int cfbt_set_frame_end(int frame_id)
{
	int i, ret = -1;
	struct cfbt_frame_group *grp;

	for (i = 0; i < CFBT_MAX_GROUP_NUM; i++) {
		grp = pipe_frame_groups[i];
		if (grp && ((grp->id == frame_id) || atomic_read(&grp->need_clean))) {
			update_frame_running_time(grp, fbg_ktime_get_ns() - grp->frame_start_time);
			cfbt_clear_frame_info(grp);
			trace_frame_end(i);
			trace_cfbt_frame_state(i, 0);
			ret = 0;
		}
	}

	return ret;
}

int cfbt_get_version(int version)
{
	return cfbt_version == get_configuration_version();
}

int __cfbt_set_scene_start(struct cfbt_struct *data)
{
	if (!cfbt_get_version(data->header.version)) {
		pr_err("[CFBT KERNEL]%s, %s, %d, version match fail!", __FILE__, __FUNCTION__, __LINE__);
		data->header.ret = -1;
		return 0;
	}
	if (data->scene < 0) {
		pr_err("[CFBT KERNEL]%s, %s, %d, scene %d set fail!", __FILE__, __FUNCTION__, __LINE__, data->scene);
		data->header.ret = -1;
		return 0;
	}
	init_cfbt_rescue();
	cfbt_trace_init();
	cfbt_clear_all_frame_info();
	cfbt_current_scene = data->scene;
	data->header.ret = 0;
	return 0;
}
EXPORT_SYMBOL(__cfbt_set_scene_start);

int __cfbt_set_scene_end(struct cfbt_struct *data)
{
	int ret;

	ret = cfbt_clear_all_frame_info();
	if (ret) {
		pr_err("[CFBT KERNEL]%s, %s, %d, scene 0 set fail!", __FILE__, __FUNCTION__, __LINE__);
		data->header.ret = -1;
		return 0;
	}
	cfbt_current_scene = CFBT_NONE;
	data->header.ret = 0;

	return 0;
}
EXPORT_SYMBOL(__cfbt_set_scene_end);

int cfb_get_rescue_rtime(char *buf, int len)
{
	return retrieve_rescue_time(buf, len);
}
EXPORT_SYMBOL(cfb_get_rescue_rtime);

int __cfbt_request_frame_id(struct cfbt_struct *data)
{
	struct cfbt_frame_group *grp = NULL;
	int ret;

	ret = cfbt_get_frame_id(data, grp);
	if (ret) {
		pr_err("[CFBT KERNEL]%s, %s, %d, request frame id fail!", __FILE__, __FUNCTION__, __LINE__);
		data->header.ret = -1;
		return 0;
	}
	data->header.ret = 0;
	return 0;
}
EXPORT_SYMBOL(__cfbt_request_frame_id);

int __cfbt_release_frame_id(struct cfbt_struct *data)
{
	int ret;

	ret = cfbt_set_frame_end(data->frame_id);
	if (ret) {
		pr_err("[CFBT KERNEL]%s, %s, %d, release frame id fail!", __FILE__, __FUNCTION__, __LINE__);
		data->header.ret = -1;
		return 0;
	}
	data->header.ret = 0;
	return 0;
}
EXPORT_SYMBOL(__cfbt_release_frame_id);

int __cfbt_set_stage(struct cfbt_struct *data)
{
	struct cfbt_frame_group *grp = NULL;
	int idx = -1;

	idx = data->frame_id;
	grp = pipe_frame_groups[idx];

	if (grp == NULL) {
		pr_err("[CFBT KERNEL]%s %s %dpipe_frame_groups has a NULL group %d", __FILE__, __FUNCTION__, __LINE__, idx);
		data->header.ret = -1;
		return 0;
	}

	if (!atomic_read(&grp->using)) {
		pr_err("[CFBT KERNEL]%s %s %dpipe_frame_groups is unusing", __FILE__, __FUNCTION__, __LINE__);
		data->header.ret = -1;
		return 0;
	}

	grp->stage = data->stage;
	grp->stage_timeout= data->timeout;
	grp->stage_tag = data->tag;
	if (data->tag == CFBT_STAGE_BEGIN) {
		grp->stage_start_time = fbg_ktime_get_ns();
		grp->util_stage_start = atomic64_read(&grp->policy_util);
		update_configuration_stage(get_cfbt_current_scene(), data->stage);
		start_cfbt_timer(grp);
		trace_cfbt_stage(idx, data->stage + 1);
	}

	if (data->tag == CFBT_STAGE_END) {
		if (grp->stage < (get_max_stage_count(get_cfbt_current_scene()) - 1))
			clear_stage_rescue(grp);
		update_stage_running_time(grp, fbg_ktime_get_ns() - grp->stage_start_time, grp->stage);
	}

	data->header.ret = 0;
	return 0;
}
EXPORT_SYMBOL(__cfbt_set_stage);

int __cfbt_add_stage_tid(struct cfbt_struct *data)
{
	struct task_struct *task = NULL;
	int idx = data->frame_id;
	int i, tail_idx, thread_num, j;
	pid_t pid;
	unsigned long flags;
	struct cfbt_frame_group *grp = NULL;

	grp = pipe_frame_groups[idx];
	if (grp == NULL) {
		pr_err("[CFBT KERNEL]%s %s %dpipe_frame_groups has a NULL group %d", __FILE__, __FUNCTION__, __LINE__, idx);
		data->header.ret = -1;
		return 0;
	}
	raw_spin_lock_irqsave(&grp->lock, flags);
	if (!atomic_read(&grp->using)) {
		pr_err("[CFBT KERNEL]%s %s %dpipe_frame_groups is unusing", __FILE__, __FUNCTION__, __LINE__);
		data->header.ret = -1;
		raw_spin_unlock_irqrestore(&grp->lock, flags);
		return 0;
	}

	for (i = 0; i < data->tid_count; i++) {
		tail_idx = grp->cfbt_key_thread_tail;
		thread_num = grp->cfbt_key_thread_num;
		pid = data->tids[i];

		rcu_read_lock();
		task = find_task_by_vpid(pid);
		if ((task) && (thread_num < CFBT_MAX_THREAD_NUM)) {
			get_task_struct(task);
			rcu_read_unlock();
			//fath path,add task to tail
			if (likely(grp->key_thread[tail_idx] == NULL)) {
				grp->key_thread[tail_idx] = task;
				grp->cfbt_key_thread_num++;
				grp->cfbt_key_thread_tail++;
				continue;
			}
			//slow path,insert the task
			for (j = 0; j < grp->cfbt_key_thread_num; j++) {
				if (unlikely(grp->key_thread[j] == NULL)) {
					grp->key_thread[j] = task;
					grp->cfbt_key_thread_num++;
					break;
				}
			}
		} else
			rcu_read_unlock();
	}
	raw_spin_unlock_irqrestore(&grp->lock, flags);
	data->header.ret = 0;
	return 0;
}
EXPORT_SYMBOL(__cfbt_add_stage_tid);

int __cfbt_remove_stage_tid(struct cfbt_struct *data)
{
	struct task_struct *task = NULL;
	int idx = data->frame_id;
	int i, thread_num, j;
	pid_t pid;
	unsigned long flags;
	struct cfbt_frame_group *grp = NULL;
	grp = pipe_frame_groups[idx];

	if (grp == NULL) {
		pr_err("[CFBT KERNEL]%s %s %dpipe_frame_groups has a NULL group %d", __FILE__, __FUNCTION__, __LINE__, idx);
		data->header.ret = -1;
		return 0;
	}

	raw_spin_lock_irqsave(&grp->lock, flags);
	if (!atomic_read(&grp->using)) {
		pr_err("[CFBT KERNEL]%s %s %dpipe_frame_groups is unusing", __FILE__, __FUNCTION__, __LINE__);
		data->header.ret = -1;
		raw_spin_unlock_irqrestore(&grp->lock, flags);
		return 0;
	}
	for (i = 0; i < data->tid_count; i++) {
		pid = data->tids[i];
		thread_num = grp->cfbt_key_thread_num;
		for (j = 0; j < thread_num; j++) {
			if (grp->key_thread[thread_num]->pid == pid) {
				task = grp->key_thread[thread_num];
				grp->key_thread[thread_num] = NULL;
				grp->cfbt_key_thread_num--;
				if (j == thread_num - 1)
					grp->cfbt_key_thread_tail--;
			}
		}
	}
	raw_spin_unlock_irqrestore(&grp->lock, flags);
	data->header.ret = 0;
	return 0;
}
EXPORT_SYMBOL(__cfbt_remove_stage_tid);

void reset_cfbt_frame_time(struct cfbt_frame_group *grp)
{
	grp->frame_start_time = 0;
	grp->curr_window_scale = 0;
	grp->prev_window_scale = 0;
	grp->curr_window_exec = 0;
	grp->prev_window_exec = 0;
}

int __cfbt_set_frame_start(struct cfbt_struct *data)
{
	struct cfbt_frame_group *grp = NULL;
	unsigned long flags;
	int idx = data->frame_id;
	int max_stage_cnt = 0;
	int target_time = 0;

	grp = pipe_frame_groups[idx];
	if (grp == NULL) {
		pr_err("[CFBT KERNEL]%s %s %dpipe_frame_groups has a NULL group %d", __FILE__, __FUNCTION__, __LINE__, idx);
		data->header.ret = -1;
		return 0;
	}
	raw_spin_lock_irqsave(&grp->lock, flags);
	reset_cfbt_frame_time(grp);
	if (!atomic_read(&grp->using)) {
		pr_err("[CFBT KERNEL]%s %s %dpipe_frame_groups is unusing", __FILE__, __FUNCTION__, __LINE__);
		data->header.ret = -1;
		raw_spin_unlock_irqrestore(&grp->lock, flags);
		return 0;
	}
	grp->frame_start_time = fbg_ktime_get_ns();
	grp->window_start = grp->frame_start_time;

	max_stage_cnt = get_max_stage_count(get_cfbt_current_scene());
	target_time = get_target_time_for_scene(get_cfbt_current_scene());

	cfbt_grp_util[grp->id] = 0;

	for (int i = 0; i < MAX_FRAME_STAGE_NUM; i++) {
		grp->stages_time.cnt_set = max_stage_cnt;
		grp->stages_time.atime = target_time;
	}
	// cfbt_insert_curr_to_grp(grp);
	raw_spin_unlock_irqrestore(&grp->lock, flags);
	trace_cfbt_uframeid(idx, data->uframeid);
	data->header.ret = 0;
	return 0;
}
EXPORT_SYMBOL(__cfbt_set_frame_start);

int __cfbt_notify_rescue_of_user(struct cfbt_struct *data)
{
	return 0;
}
EXPORT_SYMBOL(__cfbt_notify_rescue_of_user);

int __cfbt_notify_stop_rescue_of_user(struct cfbt_struct *data)
{
	return 0;
}
EXPORT_SYMBOL(__cfbt_notify_stop_rescue_of_user);

int __cfbt_notify_error_of_user(struct cfbt_struct *data)
{
	int frameid = data->frame_id;

	cfbt_clear_frame_info_by_id(frameid);
	trace_frame_end(frameid);
	cfbt_trace_notify_err(frameid);
	cfbt_trace_notify_err(0);
	data->header.ret = 0;
	return 0;
}
EXPORT_SYMBOL(__cfbt_notify_error_of_user);

inline bool is_curr_cfbt_task(struct task_struct *p)
{
	struct oplus_task_struct *ots = NULL;

	ots = get_oplus_task_struct(p);
	if (!IS_ERR_OR_NULL(ots))
		return ots->cfbt_running;
	return false;
}

int cfbt_should_skip(int first_cpu)
{
	struct oplus_sched_cluster *cluster;
	int cpu, cpu_tmp;
	struct rq *rq = NULL;
	int ret = 1;
	int skip_cpu = get_skip_cpu_by_user_config();

	if (first_cpu < skip_cpu) {
		ret = 0;
		goto out;
	}
	for_each_sched_cluster(cluster) {
		cpu = cpumask_first(&cluster->cpus);
		if(cpu == skip_cpu) {
			for_each_cpu(cpu_tmp, &cluster->cpus) {
				rq = cpu_rq(cpu_tmp);
				if(is_curr_cfbt_task(rq->curr)) {
					ret = 0;
					goto out;
				}
			}
		}
	}
out:
	return ret;
}

inline void notify_update_freq(void)
{
	struct oplus_sched_cluster *cluster;
	int cpu;
	struct rq *rq = NULL;

	rcu_read_lock();
	for_each_sched_cluster(cluster) {
		cpu = cpumask_first(&cluster->cpus);
		rq = cpu_rq(cpu);
		if (fbg_hook.update_freq)
			fbg_hook.update_freq(rq, SCHED_CPUFREQ_DEF_FRAMEBOOST);
		else
			cpufreq_update_util_wrap(rq, SCHED_CPUFREQ_DEF_FRAMEBOOST);
	}
	rcu_read_unlock();
}

unsigned long cfbt_update_freq_policy_util(struct cfbt_frame_group *grp, u64 wallclock)
{
	unsigned long curr_putil = 0;

	lockdep_assert_held(&grp->lock);
	curr_putil = cfbt_get_frame_putil(grp->id, grp->curr_window_scale);
	atomic64_set(&grp->curr_util, curr_putil);
	return curr_putil;

}
EXPORT_SYMBOL(cfbt_update_freq_policy_util);

bool cfbt_update_cpufreq(int grp_id, struct task_struct *p)
{
	struct cfbt_frame_group *grp = pipe_frame_groups[grp_id];
	u64 wallclock = fbg_ktime_get_ns();
	u64 frame_delta, stage_delta;
	unsigned long fbg_util = 0;
	unsigned long real_util = 0;

	if (grp == NULL) {
		pr_err("[CFBT KERNEL]%s %s %dpipe_frame_groups has a NULL group %d", __FILE__, __FUNCTION__, __LINE__, grp_id);
		return false;
	}

	if (!atomic_read(&grp->using))
		return false;

	if (atomic_read(&grp->need_clean))
		return false;

	if ((grp->frame_start_time > 0) && (grp->stage_start_time > 0)){
		frame_delta = wallclock - grp->frame_start_time;
		stage_delta = wallclock - grp->stage_start_time;
		if (unlikely((frame_delta > DEFAULT_MAX_FRAME_INTERVAL) && (stage_delta > CAMERA_FRAME_INTERVAL))){
			atomic_set(&grp->need_clean, 1);
			return true;
		}
	}

	fbg_util = cfbt_update_freq_policy_util(grp, wallclock);
	atomic64_set(&grp->policy_util, fbg_util);
	/*Boost for enhance*/
	real_util = get_rescue_utilization(grp, fbg_util);
	// raw_spin_unlock_irqrestore(&grp->lock, flags);
	/*Boost for enhance*/
	cfbt_grp_util[grp->id] = real_util;
	trace_cfbt_util(grp_id, fbg_util);
	trace_cfbt_rutil(grp_id, real_util);
	return true;
}

void calculate_task_util(struct task_struct *p, u64 running,
	u64 wallclock, u64 *curr_window_exec, u64 *curr_window_scale)
{
		u64 adjusted_running;
		u64 exec_scale;
		struct rq *rq = task_rq(p);

		*curr_window_exec = 0;
		*curr_window_scale = 0;
		/*
		* adjust the running time, for serial load track.
		* only adjust STATIC_FRAME_TASK tasks, not BINDER_FRAME_TASK tasks,
		* matched with the logic of update_group_nr_running().
		*/
		if (pipe_thread_pool->mark_start < 0)
			return;

		adjusted_running = wallclock - pipe_thread_pool->mark_start;
		if (unlikely(adjusted_running <= 0))
			return;

		pipe_thread_pool->mark_start = wallclock;
		running = adjusted_running;


		if (running <= 0)
			return;

		*curr_window_exec += running;
		exec_scale = scale_exec_time(running, rq);
		*curr_window_scale += exec_scale;
}

inline void cfbt_update_frame_group_util(u64 *curr_window_exec, u64 *curr_window_scale, struct cfbt_frame_group *grp)
{
	grp->curr_window_exec += *curr_window_exec;
	grp->curr_window_scale += *curr_window_scale ;
}

bool cfbt_update_task_util(struct task_struct *task, int idx, u64 runtime, bool need_freq_update)
{
	struct cfbt_frame_group *grp = NULL;
	unsigned long flags;
	u64 wallclock;
	int i;
	u64 curr_window_exec, curr_window_scale;

	if (!is_cfbt_enabled())
		return true;

	if (get_cfbt_current_scene() == CFBT_NONE)
		return true;

	curr_window_exec = 0;
	curr_window_scale = 0;
	wallclock = fbg_ktime_get_ns();

	raw_spin_lock_irqsave(&pipe_thread_pool->common_pool_lock, flags);
	calculate_task_util(task, runtime, wallclock, &curr_window_exec, &curr_window_scale);
	raw_spin_unlock_irqrestore(&pipe_thread_pool->common_pool_lock, flags);
	for(i = 0; i < CFBT_MAX_GROUP_NUM; i++){
		grp = pipe_frame_groups[i];
		if (grp == NULL) {
			pr_err("[CFBT KERNEL]%s %s %dpipe_frame_groups has a NULL group %d", __FILE__, __FUNCTION__, __LINE__, idx);
			return false;
		}
		if (!atomic_read(&grp->using))
			continue;
		cfbt_update_frame_group_util(&curr_window_exec, &curr_window_scale, grp);
	}

	if (need_freq_update) {
		for (i = 0; i < CFBT_MAX_GROUP_NUM; i++) {
			cfbt_update_cpufreq(i,task);
		}
		notify_update_freq();
	}
	return true;
}
EXPORT_SYMBOL(cfbt_update_task_util);

void cfbt_update_group_nr_running(int idx, int event, struct oplus_task_struct *ots)
{
	unsigned long flags;

	if (!is_cfbt_enabled())
		return;

	raw_spin_lock_irqsave(&pipe_thread_pool->common_pool_lock, flags);
	if (event == PICK_NEXT_TASK) {
		pipe_thread_pool->nr_running++;
		if (pipe_thread_pool->nr_running == 1) {
			pipe_thread_pool->mark_start = max(pipe_thread_pool->mark_start, fbg_ktime_get_ns());
		}
	} else if (event == PUT_PREV_TASK && ots->cfbt_running) {
		pipe_thread_pool->nr_running--;
		if (unlikely(pipe_thread_pool->nr_running < 0))
			pipe_thread_pool->nr_running = 0;
	}
	raw_spin_unlock_irqrestore(&pipe_thread_pool->common_pool_lock, flags);
}
EXPORT_SYMBOL(cfbt_update_group_nr_running);

bool cfbt_select_task_rq(struct task_struct *p, int *target_cpu)
{
	struct oplus_task_struct *ots;
	int orig_cls_id = -1;
	int cpu;
	struct oplus_sched_cluster *cluster;
	struct rq *rq;

	if (!is_cfbt_enabled())
		return false;

	if (is_cfbt_suspend())
		return false;

	if (!is_selection_option_enabled())
		return false;

	ots = get_oplus_task_struct(p);
	if (IS_ERR_OR_NULL(ots))
		return false;

	if (!ots->cfbt_running)
		return false;

	orig_cls_id = topology_cluster_id(*target_cpu);
	if (orig_cls_id == max_cluster_id) {
		for_each_sched_cluster(cluster) {
			if (unlikely(cluster->id == max_cluster_id))
				continue;
			for_each_cpu(cpu, &cluster->cpus) {
				rq = cpu_rq(cpu);
				if (!is_curr_cfbt_task(rq->curr)) {
					*target_cpu = cpu;
					return true;
				}
			}
		}
	}
	return false;
}
EXPORT_SYMBOL(cfbt_select_task_rq);

bool cfbt_freq_policy_util(unsigned int policy_flags, const struct cpumask *query_cpus,
	unsigned long *util)
{
	int i;
	unsigned long max_util = 0;
	int first_cpu = cpumask_first(query_cpus);

	if (!is_cfbt_enabled())
		return true;

	if (get_cfbt_current_scene() == CFBT_NONE)
		return true;

	if (is_cfbt_suspend())
		return true;

	if (cfbt_should_skip(first_cpu))
		return true;

	for (i = 0; i < CFBT_MAX_GROUP_NUM; i++) {
		if (max_util < cfbt_grp_util[i])
			max_util = cfbt_grp_util[i];
	}
	trace_cfbt_systrace_c("cfbt_max_util", max_util);
	trace_cfbt_systrace_c("raw_util", *util);

	if (!!max_util && max_util < *util) {
		*util = max_util;
		trace_cfbt_systrace_c("using_cfbt_util", 1);
	} else
		trace_cfbt_systrace_c("using_cfbt_util", 0);

	return (!!max_util);
}
EXPORT_SYMBOL_GPL(cfbt_freq_policy_util);

int cfbt_frame_group_init(void)
{
	struct cfbt_frame_group *grp = NULL;
	int i, j, ret = 0;

	cfbt_current_scene = CFBT_NONE;

	pipe_thread_pool = kzalloc(sizeof(*pipe_thread_pool), GFP_KERNEL);
	if (!pipe_thread_pool)
		return -ENOMEM;
	raw_spin_lock_init(&pipe_thread_pool->common_pool_lock);
	pipe_thread_pool->mark_start = 0;
	pipe_thread_pool->nr_running = 0;
	for (i = 0; i < CFBT_MAX_GROUP_NUM; i++) {
		grp = kzalloc(sizeof(*grp), GFP_NOWAIT);
		if (!grp) {
			ret = -ENOMEM;
			goto out;
		}
		memset(grp, 0, sizeof(*grp));
		INIT_LIST_HEAD(&grp->tasks);
		grp->window_size = NSEC_PER_SEC / DEFAULT_FRAME_RATE;
		grp->window_start = 0;
		grp->nr_running = 0;
		atomic_set(&grp->using, 0);
		atomic_set(&grp->need_clean, 0);
		grp->mark_start = 0;
		grp->cfbt_key_thread_num = 0;
		grp->cfbt_key_thread_tail = 0;
		grp->frame_start_time = 0;
		grp->preferred_cluster = NULL;
		grp->available_cluster = NULL;
		grp->curr_window_scale = 0;
		grp->prev_window_scale = 0;
		grp->curr_window_exec = 0;
		grp->prev_window_exec = 0;
		grp->isRescuring = RESCUE_OF_NONE;
		grp->stage = 0;
		grp->util_stage_start = 0;
		atomic64_set(&grp->policy_util, 0);
		grp->id = i;
		for (j = 0; j < CFBT_MAX_THREAD_NUM; j++)
			grp->key_thread[j] = NULL;
		raw_spin_lock_init(&grp->lock);
		pipe_frame_groups[i] = grp;
		cfbt_grp_util[i] = 0;
	}
out:
	pr_err("[CFBT KERNEL]cfbt_boost init okay");
	return ret;
}

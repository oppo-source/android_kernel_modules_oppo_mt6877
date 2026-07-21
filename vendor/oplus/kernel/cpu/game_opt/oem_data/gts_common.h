/** Copyright (C), 2025-2029, OPLUS Mobile Comm Corp., Ltd.
* Description: game task struct
* Author: zhoutianyao
* Create: 2025-1-15
* Notes: NA
*/

#ifndef __GTS_COMMON_H__
#define __GTS_COMMON_H__

#include <linux/types.h>

#define GTS_IDX			3

enum GAME_WAKEUP_FLAG
{
	GWF_DISPLAY_PIPELINE,
};

struct runtime_avg
{
	u64 runtime_avg;
	u64 prev_runtime_sum;
	u64 curr_runtime_sum;
	u64 last_update_time;
};

struct task_demand
{
	int tracking;
	int handoff;
	u64 prev_runtime_sum;
	u64 curr_runtime_sum;
	u64 prev_demand;
	u64 curr_demand;
	u64 last_update_time;
	u64 last_rollover_time;
};

struct task_runtime_info {
	pid_t pid;
	struct task_struct *task;
	u64 sum_exec_scale;
};

struct thread_wake_info {
	pid_t pid;
	struct task_struct *task;
	u32 wake_count;
	bool ui_wakeup_assist;
};

struct multi_task_util_info {
	atomic_t is_tracked;
	struct task_runtime_info *child_threads;
	struct thread_wake_info *ui_assist_threads;
	int ui_assist_nums;
	struct task_struct* process_leaders;
	int child_num;
	u64 window_start;
	atomic_t have_valid_process_pids;
};

struct render_related_thread {
	pid_t pid;
	struct task_struct *task;
	u32 wake_count;
};

struct multi_rt_info {
	pid_t *related_threads_sorted;
	struct render_related_thread *related_threads;
	int rt_num;
	int total_num;
	int rt_num_sorted;
	int total_num_sorted;
	atomic_t have_valid_render_pids;
};

enum THREAD_TYPE
{
	THREAD_TYPE_UNKNOWN,
	THREAD_TYPE_YES,
	THREAD_TYPE_NO,
};

struct thread_type {
	u8 is_sf_app;
	u8 is_thread;
	u8 is_unitymain;
};

struct game_task_struct
{
	struct task_struct *task;

	/* define for sched assist */
	u64 sched_prop;

	/* define for game */
	u64 sp_ext;

	/* define for wakeup flag */
	u64 wakeup_flag;

	/* define for display pipeline */
	u64 display_pipeline;

	/* sched avg */
	struct runtime_avg runtime_avg;

	/* load tracking */
	struct task_demand demand;

	/* multi task util info*/
	struct multi_task_util_info mtu_info;

	/* multi task util info*/
	struct multi_rt_info mrt_info;

	struct thread_type thread_type;
} ____cacheline_aligned;

static inline struct game_task_struct *get_game_task_struct(struct task_struct *p)
{
	struct game_task_struct *gts = NULL;

	/* not Skip idle thread */
	if (!p) {
		return NULL;
	}

	gts = (struct game_task_struct *) READ_ONCE(p->android_oem_data1[GTS_IDX]);
	if (IS_ERR_OR_NULL(gts)) {
		return NULL;
	}

	return gts;
}

static inline bool ts_to_gts(struct task_struct *p, struct game_task_struct **gts_ptr)
{
	/* not Skip idle thread */
	if (!p) {
		return false;
	}

	*gts_ptr = (struct game_task_struct *) READ_ONCE(p->android_oem_data1[GTS_IDX]);
	return !IS_ERR_OR_NULL(*gts_ptr);
}

static inline void init_game_task_struct(void *ptr)
{
	struct game_task_struct* gts = ptr;

	memset(gts, 0, sizeof(struct game_task_struct));
	gts->task = NULL;
	gts->sched_prop = 0;
	gts->sp_ext = 0;
	gts->wakeup_flag = 0;
	gts->display_pipeline = 0;
	gts->runtime_avg.runtime_avg = 0;
	gts->runtime_avg.prev_runtime_sum = 0;
	gts->runtime_avg.curr_runtime_sum = 0;
	gts->runtime_avg.last_update_time = 0;
	gts->demand.tracking = 0;
	gts->demand.handoff = 0;
	gts->demand.prev_demand = 0;
	gts->demand.curr_demand = 0;
	gts->demand.last_update_time = 0;
	gts->demand.last_rollover_time = 0;
	gts->mtu_info.child_threads = NULL;
	gts->mtu_info.child_num = 0;
	gts->mtu_info.ui_assist_threads = NULL;
	gts->mtu_info.ui_assist_nums = 0;
	atomic_set(&gts->mtu_info.is_tracked, 0);
	atomic_set(&gts->mtu_info.have_valid_process_pids, 0);
	gts->mrt_info.related_threads_sorted = NULL;
	gts->mrt_info.related_threads = NULL;
	gts->mrt_info.rt_num = 0;
	gts->mrt_info.total_num = 0;
	gts->mrt_info.rt_num_sorted = 0;
	gts->mrt_info.total_num_sorted = 0;
	atomic_set(&gts->mrt_info.have_valid_render_pids, 0);
	gts->thread_type.is_sf_app = THREAD_TYPE_UNKNOWN;
	gts->thread_type.is_thread = THREAD_TYPE_UNKNOWN;
	gts->thread_type.is_unitymain = THREAD_TYPE_UNKNOWN;
}

#endif /* __GTS_COMMON_H__ */

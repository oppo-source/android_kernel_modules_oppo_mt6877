/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include "cfbt_rescue.h"
#include "frame_debug.h"
#include "frame_group.h"
#include "cfbt_trace.h"
#include "cfbt_config.h"

#define MAX_TIMER_COUNT (16)
#define MAX_AVERAGE_TIMES (8)
#define CFBT_DIV64_U64_ROUNDUP(X, Y) div64_u64((X) + (Y - 1), Y)

enum {
	CFBT_TIMER_TYPE_NONE,
	CFBT_TIMER_TYPE_STAGE,
	CFBT_TIMER_TYPE_FRAME,
};

struct timer_data {
	struct timer_list timer;
	struct cfbt_frame_group *grp;
	int stage;
	int type;
	u64 timeout;
	int initialized;
};

struct cfbt_rescue_request {
	struct cfb_stage_rtime stage_times[MAX_AVERAGE_TIMES];
	int index;
	int ready;
	int initialized;
};

atomic_t timer_index;
static int cfbt_timer_initialized = 0;
struct timer_data cfbt_timers[MAX_TIMER_COUNT];
struct cfbt_rescue_request rescue_request;
static DEFINE_SPINLOCK(timer_alloc_lock);

/* Check if running time is good */
int is_running_time_good(struct cfbt_frame_group *group, u64 running_time)
{
	int i;
	struct cfb_stage_rtime *stage_time = &group->stages_time;
	u64 upper_target_time = stage_time->atime + ((stage_time->atime * 205) >> 10);
	u64 lower_target_time = stage_time->atime - ((stage_time->atime * 205) >> 10);

	if (running_time >= upper_target_time || running_time <= lower_target_time)
		return 0;

	for (i = 0; i < stage_time->cnt_set; i++) {
		if (stage_time->rtime[i] == 0)
			return 0;

		if (stage_time->rtime[i] > stage_time->atime)
			return 0;
	}
	return 1;
}

u64 calculate_average_stage_timeout(int stage)
{
	u64 sum = 0;
	int i;
	u64 avg = 0;

	if (!rescue_request.ready)
		return get_default_stage_timeout(stage);

	for (i = 0; i < MAX_AVERAGE_TIMES; i++)
		sum += rescue_request.stage_times[i].rtime[stage];

	avg = CFBT_DIV64_U64_ROUNDUP(((sum * 128) >> 10), 1000000);

	return avg;
}

void update_stage_running_time(struct cfbt_frame_group *group, u64 running_time, int stage)
{
	if (group == NULL)
		return;

	if (stage < 0 || stage >= group->stages_time.cnt_set)
		return;

	group->stages_time.rtime[stage] = running_time;
}

void commit_frame_time(struct cfbt_frame_group *group, u64 running_time)
{
	memcpy(&rescue_request.stage_times[rescue_request.index], &group->stages_time, sizeof(struct cfb_stage_rtime));
	rescue_request.index++;
	if (rescue_request.index >= MAX_AVERAGE_TIMES) {
		rescue_request.index = 0;
		rescue_request.ready = 1;
	}
}

void update_frame_running_time(struct cfbt_frame_group *group, u64 running_time)
{
	if (!is_running_time_good(group, running_time))
		return;

	commit_frame_time(group, running_time);
}

/* for debug */
int retrieve_rescue_time(char *buf, int len)
{
	snprintf(buf, len - 1, "stage avg time:%llu, %llu, %llu, %llu, %llu, rescue request idx:%d",
			 calculate_average_stage_timeout(0),
			 calculate_average_stage_timeout(1),
			 calculate_average_stage_timeout(2),
			 calculate_average_stage_timeout(3),
			 calculate_average_stage_timeout(4), rescue_request.index);
	return 0;
}

/* rescue manipulation functions */
void clear_stage_rescue(struct cfbt_frame_group *group)
{
	group->isRescuring &= (~RESCUE_OF_STAGE);
	trace_cfbt_rescue(group->id, group->isRescuring);
	if (group->isRescuring & RESCUE_OF_FRAME)
		return;

	group->cur_enhance_util = 0;
}

void clear_frame_rescue(struct cfbt_frame_group *group)
{
	group->isRescuring &= (~RESCUE_OF_FRAME);
	group->cur_enhance_util = 0;
	trace_cfbt_rescue(group->id, group->isRescuring);
}

void activate_stage_rescue(struct cfbt_frame_group *group)
{
	if (group->isRescuring & RESCUE_OF_FRAME)
		return;
	group->isRescuring |= RESCUE_OF_STAGE;
	trace_cfbt_rescue(group->id, group->isRescuring);
}

void activate_frame_rescue(struct cfbt_frame_group *group)
{
	group->isRescuring |= RESCUE_OF_FRAME;
	group->cur_enhance_util = 0;
	clear_stage_rescue(group);
	trace_cfbt_rescue(group->id, group->isRescuring);
}

/* utility calculations */
unsigned long calculate_utilization(u64 utilization)
{
	return (utilization * get_stage_enhancement_value()) >> 10;
}

unsigned long get_rescue_utilization_inner(struct cfbt_frame_group *group, unsigned long fbg_util)
{
	unsigned long real_util = 0;
	unsigned long enhance = 0;

	if (group->isRescuring & RESCUE_OF_FRAME) {
		if (group->cur_enhance_util == 0) {
			real_util = fbg_util + ((fbg_util * get_frame_enhancement_value()) >> 10);
			group->cur_enhance_util = real_util;
		} else {
			real_util = group->cur_enhance_util;
		}
		goto out;
	}

	if (group->isRescuring && (group->cur_enhance_util == 0)) {
		if (likely(fbg_util > group->util_stage_start)) {
			enhance = calculate_utilization((fbg_util - group->util_stage_start));
			trace_cfbt_enhance(group->id, enhance);
		}
		group->cur_enhance_util = fbg_util + enhance;
		real_util = group->cur_enhance_util;
		goto out;
	}

	if (group->isRescuring && (group->cur_enhance_util != 0)) {
		real_util = group->cur_enhance_util;
		return real_util;
	}

	group->cur_enhance_util = 0;
	real_util = fbg_util;

out:
	return real_util;
}

unsigned long get_rescue_utilization(struct cfbt_frame_group *group, unsigned long fbg_util)
{
	return (get_rescue_utilization_inner(group, fbg_util) * get_cfbt_util_down()) >> 10;
}

/* timer management */
bool should_process_frame_rescue(struct cfbt_frame_group *group, struct timer_data *timer_data)
{
	return (timer_data->type == CFBT_TIMER_TYPE_FRAME) &&
		   (timer_data->stage == 0) &&
		   (group->stage < get_max_stage_count(get_cfbt_current_scene()) - 1);
}

void __process_cfbt_timeout(struct timer_list *t)
{
	struct timer_data *timer_data = from_timer(timer_data, t, timer);
	struct cfbt_frame_group *group = timer_data->grp;
	const int last_stage = get_max_stage_count(get_cfbt_current_scene());
	u64 frame_running_time = 0;

	if (!atomic_read(&group->using))
		return;

	if (should_process_frame_rescue(group, timer_data)) {
		activate_frame_rescue(group);
		return;
	}

	if (timer_data->type != CFBT_TIMER_TYPE_STAGE)
		return;

	if (timer_data->stage == (last_stage - 1)) {
		frame_running_time = fbg_ktime_get_ns() - group->frame_start_time;
	 	if ((group->stages_time.atime - frame_running_time) > ((timer_data->timeout * 512) >> 10))
				activate_stage_rescue(group);
		return;
	}

	if (timer_data->stage == group->stage) {
		activate_stage_rescue(group);
		return;
	}

	if (timer_data->stage < group->stage)
		clear_stage_rescue(group);

	if (unlikely(timer_data->stage > group->stage))
		clear_stage_rescue(group);
}

void __cfbt_rescue_init(void)
{
	memset(&rescue_request, 0, sizeof(rescue_request));
}

bool initialize_timer(void)
{
	int index;
	atomic_set(&timer_index, 0);
	memset(cfbt_timers, 0, sizeof(cfbt_timers));
	for (index = 0; index < MAX_TIMER_COUNT; index++) {
		cfbt_timers[index].initialized = 1;
		cfbt_timers[index].stage	  = -1;
		timer_setup(&cfbt_timers[index].timer, __process_cfbt_timeout, 0);
	}
	return true;
}

struct timer_data *allocate_timer(void)
{
	struct timer_data *found = NULL;
	int start_index, i;
	unsigned long flags;
	spin_lock_irqsave(&timer_alloc_lock, flags);
	start_index = atomic_read(&timer_index);
	if (start_index >= MAX_TIMER_COUNT)
		start_index = 0;
	for (i = 0; i < MAX_TIMER_COUNT; i++) {
		int current_index = (start_index + i) % MAX_TIMER_COUNT;
		struct timer_data *t = &cfbt_timers[current_index];
		if (!timer_pending(&t->timer) && t->initialized) {
			t->type = CFBT_TIMER_TYPE_NONE;
			t->stage = -1;
			atomic_set(&timer_index, (current_index + 1) % MAX_TIMER_COUNT);
			found = t;
			break;
		}
	}
	spin_unlock_irqrestore(&timer_alloc_lock, flags);

	if (!found)
		pr_warn("CFBT: No available timer slots!\n");
	return found;
}

void start_frame_timer(struct cfbt_frame_group *group, struct timer_data *timer_data) {
	u64 target_time = group->stages_time.atime;

	target_time = ((CFBT_DIV64_U64_ROUNDUP(target_time, 1000000)) * 819) >> 10;
	timer_data->timeout = target_time;
	timer_data->type = CFBT_TIMER_TYPE_FRAME;
	mod_timer(&timer_data->timer, jiffies + msecs_to_jiffies(target_time));
}

void start_stage_timer(struct timer_data *timer_data)
{
	const int last_stage = get_max_stage_count(get_cfbt_current_scene());
	u64 stage_timeout = calculate_average_stage_timeout(timer_data->stage);

	timer_data->type = CFBT_TIMER_TYPE_STAGE;
	timer_data->timeout = stage_timeout;
	if (timer_data->stage == (last_stage - 1)) {
		timer_data->timeout = (stage_timeout * 512) >> 10;
		mod_timer(&timer_data->timer, jiffies + msecs_to_jiffies(timer_data->timeout));
		return;
	}
	mod_timer(&timer_data->timer, jiffies + msecs_to_jiffies(stage_timeout));
}

void __start_cfbt_timer(struct cfbt_frame_group *group)
{
	struct timer_data *stage_timer;
	struct timer_data *frame_timer;

	if (group == NULL)
		return;

	stage_timer = allocate_timer();
	if (!stage_timer)
		return;

	if (group->stage < 0 || group->stage >= get_max_stage_count(get_cfbt_current_scene()))
		return;

	stage_timer->grp = group;
	stage_timer->stage = group->stage;
	start_stage_timer(stage_timer);

	if (group->stage == 0) {
		frame_timer = allocate_timer();
		if (!frame_timer)
			return;

		frame_timer->grp = group;
		frame_timer->stage = group->stage;
		start_frame_timer(group, frame_timer);
	}
}

void start_cfbt_timer(struct cfbt_frame_group *group)
{
	if (is_rescue_enabled())
		__start_cfbt_timer(group);
}

void init_cfbt_rescue(void)
{
	if (initialize_timer())
		cfbt_timer_initialized = 1;
}

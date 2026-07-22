/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#ifndef _CFBT_BOOST_H
#define _CFBT_BOOST_H
#include "cfbt_boost_struct.h"
#include "cfbt_config.h"
#include <../kernel/oplus_cpu/sched/sched_assist/sa_common.h>

int __cfbt_set_scene_start(struct cfbt_struct *data);
int __cfbt_set_scene_end(struct cfbt_struct *data);
int __cfbt_request_frame_id(struct cfbt_struct *data);
int __cfbt_set_stage(struct cfbt_struct *data);
int __cfbt_add_stage_tid(struct cfbt_struct *data);
int __cfbt_remove_stage_tid(struct cfbt_struct *data);
int __cfbt_release_frame_id(struct cfbt_struct *data);
int __cfbt_set_frame_start(struct cfbt_struct *data);
int __cfbt_add_common_tids(struct cfbt_struct *data);
int __cfbt_remove_common_tid(struct cfbt_struct *data);

int __cfbt_notify_rescue_of_user(struct cfbt_struct *data);
int __cfbt_notify_stop_rescue_of_user(struct cfbt_struct *data);
int __cfbt_notify_error_of_user(struct cfbt_struct *data);

int cfbt_frame_group_init(void);
bool cfbt_update_task_util(struct task_struct *task, int idx, u64 runtime, bool need_freq_update);
void cfbt_update_group_nr_running(int idx, int event, struct oplus_task_struct *ots);
bool cfbt_freq_policy_util(unsigned int policy_flags, const struct cpumask *query_cpus,
	unsigned long *util);
inline int get_cfbt_current_scene(void);
bool cfbt_select_task_rq(struct task_struct *p, int *target_cpu);
int cfb_get_rescue_rtime(char *buf, int len);

#endif /*_CFBT_BOOST_H*/

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

#include "game_ctrl.h"
#include "task_load_track.h"
#include "oem_data/game_oem_data.h"
#include "frame_detect/frame_detect.h"
#include "dsu_freq.h"

struct proc_dir_entry *game_opt_dir = NULL;
struct proc_dir_entry *early_detect_dir = NULL;
struct proc_dir_entry *critical_heavy_boost_dir = NULL;
struct proc_dir_entry *multi_task_dir = NULL;

static int __init game_ctrl_init(void)
{
	game_opt_dir = proc_mkdir("game_opt", NULL);
	if (!game_opt_dir) {
		pr_err("fail to mkdir /proc/game_opt\n");
		return -ENOMEM;
	}
	early_detect_dir = proc_mkdir("early_detect", game_opt_dir);
	if (!early_detect_dir) {
		pr_err("fail to mkdir /proc/game_opt/early_detect\n");
		return -ENOMEM;
	}
	critical_heavy_boost_dir = proc_mkdir("task_boost", game_opt_dir);
	if (!critical_heavy_boost_dir) {
		pr_err("fail to mkdir /proc/game_opt/task_boost\n");
		return -ENOMEM;
	}
	multi_task_dir = proc_mkdir("multi_task", game_opt_dir);
	if (!multi_task_dir) {
		pr_err("fail to mkdir /proc/game_opt/multi_task\n");
		return -ENOMEM;
	}

	game_oem_data_init();
	cpu_load_init();
	frame_load_init();
	cpufreq_limits_init();
	early_detect_init();
	task_load_track_init();
	task_util_init();
	multi_task_util_init();
	rt_info_init();
	multi_rt_info_init();
	frame_detect_init();
	fake_cpufreq_init();
	debug_init();

	return 0;
}

static void __exit game_ctrl_exit(void)
{
}

struct task_struct* get_task_struct_by_pid(pid_t pid)
{
	struct task_struct *task = NULL;
	rcu_read_lock();
	task = find_task_by_vpid(pid);
	rcu_read_unlock();
	return task;
}

struct game_task_struct* get_game_task_struct_by_pid(pid_t pid)
{
	struct task_struct *task = NULL;
	struct game_task_struct *game_task = NULL;
	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (!ts_to_gts(task, &game_task)) {
		rcu_read_unlock();
		return NULL;
	}
	rcu_read_unlock();
	return game_task;
}

struct game_task_struct* get_game_task_struct_and_task_struct_by_pid(pid_t pid)
{
	struct task_struct *leader = NULL;
	struct game_task_struct *tg_g_task = NULL;
	if (pid <= 0) {
		return NULL;
	}
	rcu_read_lock();
	leader = find_task_by_vpid(pid);
	if (!leader || leader->pid != leader->tgid) {
		rcu_read_unlock();
		return NULL;
	}
	get_task_struct(leader);
	if (!ts_to_gts(leader, &tg_g_task)) {
		put_task_struct(leader);
		rcu_read_unlock();
		return NULL;
	}
	rcu_read_unlock();
	return tg_g_task;
}

module_init(game_ctrl_init);
module_exit(game_ctrl_exit);
MODULE_LICENSE("GPL v2");

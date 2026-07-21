// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#ifndef __GAME_CTRL_H__
#define __GAME_CTRL_H__

#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>

#include "oem_data/gts_common.h"

#define MAX_TID_COUNT 256
#define MAX_TASK_NR 18
#define RESULT_PAGE_SIZE 1024
#define MAX_TRACKED_TASK_NUM 10
#define MAX_SCHED_CLUSTER_NUM 5

#define PROCESS_PID_COUNT 10
#define RT_PROCESS_GROUP_COUNT 10

/* a small value */
#define MAX_UI_ASSIST_NUM 20

extern struct proc_dir_entry *game_opt_dir;
extern struct proc_dir_entry *early_detect_dir;
extern struct proc_dir_entry *critical_heavy_boost_dir;
extern struct proc_dir_entry *multi_task_dir;

extern pid_t process_pids[PROCESS_PID_COUNT];
extern pid_t game_pid;

extern atomic_t have_valid_game_pid;
extern atomic_t have_valid_render_pid;

extern int g_debug_enable;
extern inline void systrace_c_printk(const char *msg, unsigned long val);
extern inline void systrace_c_signed_printk(const char *msg, long val);
extern inline void htb_systrace_c_printk(const char *prefix, int digit, const char *comm, int val);
extern inline void systrace_c_printk_common(const char *msg, unsigned long val, int id);

int cpu_load_init(void);
void frame_load_init(void);
int cpufreq_limits_init(void);
int task_util_init(void);
int multi_task_util_init(void);
int rt_info_init(void);
int multi_rt_info_init(void);
int fake_cpufreq_init(void);
int early_detect_init(void);
int debug_init(void);

bool get_task_name(pid_t pid, struct task_struct *in_task, char *name);
void ui_assist_threads_wake_stat(struct task_struct *task);
bool task_is_fair(struct task_struct *task);
void add_tasks_to_frame_group(pid_t *tracked_pids, int tracked_pid_num);
void cl_notify_frame_produce(void);
void fl_notify_frame_produce(void);

void ttwu_multi_rt_info_hook(struct task_struct *task);

/*----------------------------- rt info start -----------------------------*/

bool rt_info_top_k_locked(int k, pid_t *pid);
bool rt_info_top_k(int k, pid_t *pid);

/*----------------------------- rt info end -----------------------------*/

/*----------------------------- task util start -----------------------------*/

unsigned int get_cur_freq(unsigned int cpu);
unsigned int get_max_freq(unsigned int cpu);

/*----------------------------- task util end -----------------------------*/

/*----------------------------- early detect start -----------------------------*/
enum ED_BOOST_TYPE {
	ED_BOOST_NONE = 0,
	ED_BOOST_EDB = (1 << 0),
	ED_BOOST_RML = (1 << 1), /* frame drop Release Max frequency Limits */
	ED_BOOST_FST = (1 << 2),
	ED_BOOST_FLT = (1 << 3)
};

void ed_freq_boost_request(unsigned int boost_type);
void ed_render_wakeup_times_stat(struct task_struct *task);
void ed_set_render_task(struct task_struct *render_task);
/*----------------------------- early detect end -----------------------------*/

/*----------------------------- frame detect start -----------------------------*/

void ttwu_frame_detect_hook(struct task_struct *task);

/*----------------------------- frame detect end -----------------------------*/

/*----------------------------- ch boost req start -----------------------------*/
#define CRITICAL_TASK_NUM 2
void get_critical_task_name(char *unityMain_name, char *unityGfxDevice_name);
void update_ctb_pids(pid_t game_tgid, pid_t unitymain_pid, pid_t unitygfxdevice_pid);
bool get_ctb_enable(void);
bool get_htb_enable(void);

enum CH_BOOST_ACTION {
	CT_REQUSET_BOOST,
	CT_RELEASE_BOOST,
	HT_REQUSET_BOOST,
	HT_RELEASE_BOOST,
};

void ch_freq_boost_request(cpumask_var_t control_cpumask, enum CH_BOOST_ACTION action);

/*----------------------------- ch boost req end -----------------------------*/

/*----------------------------- multi task util start -----------------------------*/

#define MULTI_TASK_INFO_SIZE (1 << 5)
struct multi_task_ctrl_info
{
	s64 data[MULTI_TASK_INFO_SIZE];
	size_t size;
};

enum multi_task_ctrl_cmd_id {
	MULTI_TASK_ENABLE,
	MULTI_TASK_TGID,
	MULTI_TASK_MAX_ID,
};

#define MULTI_TASK_INFO_MAGIC 0xE1
#define CMD_ID_MULTI_TASK_ENABLE \
	_IOWR(MULTI_TASK_INFO_MAGIC, MULTI_TASK_ENABLE, struct multi_task_ctrl_info)
#define CMD_ID_MULTI_TASK_TGID \
	_IOWR(MULTI_TASK_INFO_MAGIC, MULTI_TASK_TGID, struct multi_task_ctrl_info)

/*----------------------------- multi task util end -----------------------------*/

/*----------------------------- multi rt info start -----------------------------*/

extern atomic_t enable_multi_task_util;

#define MULTI_RT_INFO_PID_SIZE (1 << 5)

struct multi_rt_ctrl_info
{
	s64 data[MULTI_RT_INFO_PID_SIZE];
	size_t size;
};

enum multi_rt_ctrl_cmd_id {
	MULTI_RT_PIDS,
	MULTI_RT_MAX_ID,
};

#define MULTI_RT_INFO_MAGIC 0xE2
#define CMD_ID_MULTI_RT_PIDS \
	_IOWR(MULTI_RT_INFO_MAGIC, MULTI_RT_PIDS, struct multi_rt_ctrl_info)

/*----------------------------- multi rt info end -----------------------------*/

/*----------------------------- game task tool start ----------------------------*/
struct task_struct* get_task_struct_by_pid(pid_t pid);
struct game_task_struct* get_game_task_struct_by_pid(pid_t pid);
struct game_task_struct* get_game_task_struct_and_task_struct_by_pid(pid_t pid);
/*----------------------------- game task tool end ----------------------------*/

#endif /*__GAME_CTRL_H__*/

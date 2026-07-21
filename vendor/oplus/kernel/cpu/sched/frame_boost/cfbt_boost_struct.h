/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#ifndef _CFBT_BOOST_STRUCT_H_
#define _CFBT_BOOST_STRUCT_H_
#include <linux/cpufreq.h>

#define CFBT_MAX_THREAD_NUM 128
#define CFBT_MAX_GROUP_NUM 8
#define CFBT_KEY_THREAD  (1 << 8)

enum cfbt_time_tag {
	CFBT_STAGE_BEGIN,
	CFBT_STAGE_END,
};

enum cfbt_scene {
	CFBT_NONE = -1,
	CFBT_CAMERA_4K_60FPS = 0,
};

enum cfbt_cmd_id {
	CFBT_START = 0,
	CFBT_END   = 1,
	CFBT_REQUEST_FRAME_ID = 2,
	CFBT_SET_STAGE  = 3,
	CFBT_ADD_STAGE_TID = 4,
	CFBT_REMOVE_STAGE_TID = 5,
	CFBT_FRAME_START = 6,
	CFBT_FRAME_END = 7,
	CFBT_ADD_TIDS = 8,
	CFBT_REMOVE_TIDS = 9,
	CFBT_NOTIFY_USER_RESCUE = 10,
	CFBT_NOTIFY_STOP_USER_RESCUE = 11,
	CFBT_NOTIFY_ERR = 12,
};

struct cfbt_header {
	int version;
	int ts;
	int ret;
	int stat_bits;
};

#define MAX_FRAME_STAGE_NUM (12)
struct cfb_stage_rtime {
	u64 rtime[MAX_FRAME_STAGE_NUM];
	u64 atime;
	int cnt_set;
};

enum rescue_type {
	RESCUE_OF_NONE = 0,
	RESCUE_OF_STAGE = 1 << 1,
	RESCUE_OF_FRAME = 1 << 2,
};

struct scene_stage_map {
	int scene;
	int cnt_set;
	u64 atime;
};
struct cfbt_frame_group {
	int id;
	raw_spinlock_t lock;
	struct list_head tasks;

	u64 window_start;
	u64 prev_window_size;
	u64 window_size;

	u64 curr_window_scale;
	u64 curr_window_exec;
	u64 prev_window_scale;
	u64 prev_window_exec;

	u64 util_stage_start;
	u64 curr_end_time;
	u64 curr_end_exec;


	/* nr_running:
	 *	 The number of running threads in the group
	 * mark_start:
	 *	 Mark the start time of next load track
	 */
	int nr_running;
	u64 mark_start;

	atomic64_t last_freq_update_time;
	atomic64_t last_util_update_time;

	/* For Surfaceflinger Process:
	 *	 ui is "surfaceflinger", render is "RenderEngine"
	 * For Top Application:
	 *	 ui is "UI Thread", render is "RenderThread"
	 */
	struct task_struct *key_thread[CFBT_MAX_THREAD_NUM];
	int cfbt_key_thread_num;
	int cfbt_key_thread_tail;
	/* Frame group task should be placed on these clusters */
	struct oplus_sched_cluster *preferred_cluster;
	struct oplus_sched_cluster *available_cluster;
	/* Util used to adjust cpu frequency */
	atomic64_t policy_util;
	atomic64_t curr_util;
	u64 frame_start_time;
	atomic_t using;
	int stage;
	int stage_timeout;
	u64 stage_start_time;
	u64 isRescuring;
	int stage_tag;
	unsigned long cur_enhance_util;
	struct cfb_stage_rtime stages_time;
	int *cfbt_grp_util_arr;
	atomic_t need_clean;
};

struct cfbt_struct{
	struct cfbt_header header;
	int tag;
	int scene;
	int frame_id;
	int uframeid;
	int stage;
	int timeout;
	int enhance;
	int fps;
	int tids[CFBT_MAX_THREAD_NUM];
	int tid_count;
};

struct key_thread_common_pool {
	struct task_struct *key_thread_pool[CFBT_MAX_THREAD_NUM];
	int thread_num;
	int tail;
	u64 mark_start;
	int nr_running;
	// u64 curr_window_scale;
	// u64 curr_window_exec;
	raw_spinlock_t common_pool_lock;
};
#define CFBT_MAGIC 0XF0
#define CMD_ID_CFBT_START \
	_IOWR(CFBT_MAGIC, CFBT_START, struct cfbt_struct)
#define CMD_ID_CFBT_END \
	_IOWR(CFBT_MAGIC, CFBT_END, struct cfbt_struct)
#define CMD_ID_CFBT_REQUEST_FRAME_ID \
	_IOWR(CFBT_MAGIC, CFBT_REQUEST_FRAME_ID, struct cfbt_struct)
#define CMD_ID_CFBT_SET_STAGE \
	_IOWR(CFBT_MAGIC, CFBT_SET_STAGE, struct cfbt_struct)
#define CMD_ID_CFBT_ADD_STAGE_TID \
	_IOWR(CFBT_MAGIC, CFBT_ADD_STAGE_TID, struct cfbt_struct)
#define CMD_ID_CFBT_REMOVE_STAGE_TID \
	_IOWR(CFBT_MAGIC, CFBT_REMOVE_STAGE_TID, struct cfbt_struct)
#define CMD_ID_CFBT_ADD_TIDS \
	_IOWR(CFBT_MAGIC, CFBT_ADD_TIDS, struct cfbt_struct)
#define CMD_ID_CFBT_REMOVE_TIDS \
	_IOWR(CFBT_MAGIC, CFBT_REMOVE_TIDS, struct cfbt_struct)
#define CMD_ID_CFBT_FRAME_START \
	_IOWR(CFBT_MAGIC, CFBT_FRAME_START, struct cfbt_struct)
#define CMD_ID_CFBT_FRAME_END \
	_IOWR(CFBT_MAGIC, CFBT_FRAME_END, struct cfbt_struct)

#define CFBT_NOTIFY_RESCUE_OF_USER \
	_IOWR(CFBT_MAGIC, CFBT_NOTIFY_USER_RESCUE, struct cfbt_struct)
#define CFBT_NOTIFY_STOP_RESCUE_OF_USER \
	_IOWR(CFBT_MAGIC, CFBT_NOTIFY_STOP_USER_RESCUE, struct cfbt_struct)
#define CFBT_NOTIFY_ERR_OF_USER \
	_IOWR(CFBT_MAGIC, CFBT_NOTIFY_ERR, struct cfbt_struct)

#endif // _CFBT_BOOST_STRUCT_H_

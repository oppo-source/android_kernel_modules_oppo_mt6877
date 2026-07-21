/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 MediaTek Inc.
 */

#ifndef USER_H
#define USER_H

#define TEEPERF_DEVNODE	"teeperf"

#define TEEPERF_IOC_MAGIC	'T'
#define TEEPERF_IO_HIGH_FREQ	_IO(TEEPERF_IOC_MAGIC, 0)

/* The CPU enter TEE */
#define TEE_CPU	0x6

#define PFX	"[TEEPERF]: "

extern int cpu_type;
extern int cpu_map;
extern int cpu_hint_mode;
extern int cpu_uclamp_min;
extern int cpu_index;

enum teeperf_cpu_type {
	CPU_V9_TYPE = 1,
	CPU_V8_TYPE = 2
};

enum teeperf_cpu_map {
	CPU_4_3_1_MAP = 0,
	CPU_6_2_MAP = 1,
	CPU_4_4_MAP = 2,
	CPU_MAP_MAX
};

enum teeperf_cpu_group {
	CPU_SUPER_GROUP = 0,
	CPU_BIG_GROUP = 1,
	CPU_LITTLE_GROUP = 2,
	CPU_GROUP_MAX
};

enum teeperf_cpu_hint_mode {
	CPU_UNSUPPORT = 0,
	CPU_UCLAMP_MODE = 1,
	CPU_GRP_AWARE_MODE = 2,
};

int teeperf_user_init(struct cdev *cdev);
static inline void teeperf_user_exit(void)
{
}

#endif /* USER_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */
#ifndef __INCLUDE_OSML__
#define __INCLUDE_OSML__

#include <linux/types.h>
#define MAX_REPORT_SIZE 300
#define SYS_PEVENT_SIZE 4
#define CUSTOM_PEVENT_SIZE 2
#define MAX_PEVENT_SIZE (SYS_PEVENT_SIZE + CUSTOM_PEVENT_SIZE)
#define MAX_LIST_SIZE 2
#define OSML_MAX_CLUSTER 4
#define OSML_CLUSTER 2
#define NSEC_PER_USEC 1000L
#define NSEC_PER_MSEC 1000000L
#define NSEC_TO_USEC(val) ((val) / NSEC_PER_USEC)
#define NSEC_TO_MSEC(val) ((val) / NSEC_PER_MSEC)
#define DEADLINE_MS 1000

#define OSML_CTL_NODE "osml_ctl"
#define OSML_IOC_MAGIC 'k'
#define OSML_IOC_COLLECT _IOWR(OSML_IOC_MAGIC, 0, struct osml_parcel)
#define MAX_LENGTH 30
#define MAX_CAPACITY 102400

enum {
	OSML_EVENT,
	IOC_EVENT
};

enum {
	TS,
	FRAME_DURATION,
	FRAME_ALLOW_DURATION,
	MAINTHREAD_CPU,
	RENDERTHREAD_CPU,
	TASK_LOAD,
	CPU_LOAD_CLUSTER_0,
	CPU_LOAD_CLUSTER_1,
#if (OSML_CLUSTER > 2)
	CPU_LOAD_CLUSTER_2,
#if (OSML_CLUSTER > 3)
	CPU_LOAD_CLUSTER_3,
#endif
#endif
	CPU_FREQ_CLUSTER_0,
	CPU_FREQ_CLUSTER_1,
#if (OSML_CLUSTER > 2)
	CPU_FREQ_CLUSTER_2,
#if (OSML_CLUSTER > 3)
	CPU_FREQ_CLUSTER_3,
#endif
#endif
	CPU_INST,
	CPU_CYCLE,
	CPU_LLC_RD,
	CPU_LLC_MISS_RD,
	DDR_FREQ,
	GPU_FREQ,
	THERMAL,
	TOUCHSCREEN,
	POWER_VOLTAGE,
	POWER_CURRENT,
	CPU_SCALING_MAX_FREQ_0,
	CPU_SCALING_MIN_FREQ_0,
	CPU_SCALING_MAX_FREQ_1,
	CPU_SCALING_MIN_FREQ_1,
#if (OSML_CLUSTER > 2)
	CPU_SCALING_MAX_FREQ_2,
	CPU_SCALING_MIN_FREQ_2,
#if (OSML_CLUSTER > 3)
	CPU_SCALING_MAX_FREQ_3,
	CPU_SCALING_MIN_FREQ_3,
#endif
#endif
	GPU_MAX_CLOCK,
	GPU_MIN_CLOCK,
	CUSTOM_PEVENT,
	MONITOR_SIZE,
};

enum {
	SHELL_FRONT,
	SHELL_FRAME,
	SHELL_BACK,
	SHELL_MAX,
};

struct perf_data {
	struct perf_event *pevent;
	unsigned long prev_count;
	unsigned long last_delta;
};

struct event_data {
	int uid;
	int buf_idx;
	int event_idx;
	int pdata_cnt;
	struct perf_data *pdata;
	char *title;
	struct list_head osml_event_node;
};

struct osml_monitor {
	int event_size;
	long long *buf;
};

struct cpu_load_stat {
	u64 t_user;
	u64 t_system;
	u64 t_idle;
	u64 t_iowait;
	u64 t_irq;
	u64 t_softirq;
};

struct osml_cpuinfo {
	u64 clus_id;
	u64 num_cpu;
	u64 *pwr_tbl;
	u64 max_state;
};

struct osml_parcel {
	u64 pid;
	u64 pevent_val[CUSTOM_PEVENT_SIZE];
};

#if defined(CONFIG_MTK_PLATFORM)
extern unsigned int mt_gpufreq_get_cur_freq(void);
extern unsigned int mtk_dramc_get_data_rate(void);
#else
extern void osml_register_kgsl_pwrctrl(void *pwr);
extern void clk_get_hw_freq(u64 *val, int idx);
#endif

#endif /*__INCLUDE_OSML__*/

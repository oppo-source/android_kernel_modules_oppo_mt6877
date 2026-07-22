/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

#ifndef __FPSGO_CPU_POLICY_H__
#define __FPSGO_CPU_POLICY_H__

struct fbt_cpu_dvfs_info {
	unsigned int *power;
	unsigned int *capacity_ratio;
	int num_cpu;
	int first_cpu;
	int num_opp;
};

void fpsgo_cpu_policy_init(void);
int fpsgo_get_cpu_policy_num(void);
void fpsgo_get_cpu_opp_info_by_idx(void);
int fpsgo_get_cpu_opp_info(int **opp_cnt, unsigned int ***opp_tbl);
int fpsgo_get_cpu_opp_info_for_leagcy(int **opp_cnt, unsigned int ***opp_tbl);
void get_cpu_cluster_core(struct fbt_cpu_dvfs_info *cpu_dvfs);

extern unsigned int mt_cpufreq_get_freq_by_idx(unsigned int cluster_id, int idx);

#endif /* __FPSGO_CPU_POLICY_H__ */


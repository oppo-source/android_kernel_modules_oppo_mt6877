// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/sort.h>
#include <linux/cpufreq.h>

#include "fpsgo_cpu_policy.h"

#if IS_ENABLED(CONFIG_MEDIATEK_CPU_DVFS)
#include "mtk_cpufreq_api.h"
#endif

#define NR_MT_CPU_CLUSTER_OPP 16
#define NR_MT_CPU_CLUSTER 2
#define MAX_CLUSTERS 8
#define DEBUG_LOG	0

static int cluster_count = 0;
static int cores_per_cluster[MAX_CLUSTERS];

static int policy_num;
static int *opp_count;
static unsigned int **opp_table;

static int parse_dt_topology_arm(void);

// --------------------------------------------------
static int cmp_uint(const void *a, const void *b)
{
	return *(unsigned int *)b - *(unsigned int *)a;
}

void fpsgo_cpu_policy_init(void)
{
	int cpu;
	int num = 0, count;
	struct cpufreq_policy *policy;
	struct cpufreq_frequency_table *pos;

	/* query policy number */
	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);

		if (policy) {
			pr_info("%s, policy[%d]: first:%d, min:%d, max:%d",
				__func__, num, cpu, policy->min, policy->max);

			num++;
			cpu = cpumask_last(policy->related_cpus);
			cpufreq_cpu_put(policy);
		}
	}

	policy_num = num;

	if (policy_num == 0) {
		pr_info("%s, no policy", __func__);
		return;
	}

	opp_count = kcalloc(policy_num, sizeof(int), GFP_KERNEL);
	opp_table = kcalloc(policy_num, sizeof(unsigned int *), GFP_KERNEL);

	num = 0;
	for_each_possible_cpu(cpu) {
		if (num >= policy_num)
			break;

		policy = cpufreq_cpu_get(cpu);

		if (!policy)
			continue;

		/* calc opp count */
		count = 0;
		cpufreq_for_each_entry(pos, policy->freq_table) {
			count++;
		}
		opp_count[num] = count;
		opp_table[num] = kcalloc(count, sizeof(unsigned int), GFP_KERNEL);
		count = 0;
		cpufreq_for_each_entry(pos, policy->freq_table) {
			opp_table[num][count] = pos->frequency;
			count++;
		}

		sort(opp_table[num], opp_count[num], sizeof(unsigned int), cmp_uint, NULL);

		num++;
		cpu = cpumask_last(policy->related_cpus);
		cpufreq_cpu_put(policy);
	}
	parse_dt_topology_arm();
}

int fpsgo_get_cpu_policy_num(void)
{
	return policy_num;
}

int fpsgo_get_cpu_opp_info(int **opp_cnt, unsigned int ***opp_tbl)
{
	int i, j;

	if (policy_num <= 0)
		return -EFAULT;

	*opp_cnt = kcalloc(policy_num, sizeof(int), GFP_KERNEL);
	*opp_tbl = kcalloc(policy_num, sizeof(unsigned int *), GFP_KERNEL);

	if (*opp_cnt == NULL || *opp_tbl == NULL)
		return -1;

	for (i = 0; i < policy_num; i++) {

		(*opp_cnt)[i] = opp_count[i];
		(*opp_tbl)[i] = kcalloc(opp_count[i], sizeof(unsigned int), GFP_KERNEL);

		for (j = 0; j < opp_count[i]; j++)
			(*opp_tbl)[i][j] = opp_table[i][j];

	}

	return 0;
}

int fpsgo_get_cpu_opp_info_for_leagcy(int **opp_cnt, unsigned int ***opp_tbl)
{
	int i, j;

	if (policy_num <= 0)
		return -EFAULT;

	*opp_cnt = kcalloc(policy_num, sizeof(int), GFP_KERNEL);
	*opp_tbl = kcalloc(policy_num, sizeof(unsigned int *), GFP_KERNEL);

	if (*opp_cnt == NULL || *opp_tbl == NULL)
		return -1;

	for (i = 0; i < policy_num; i++) {

		(*opp_cnt)[i] = opp_count[i];
#if DEBUG_LOG
        pr_info("%s, cluster:%d, opp: %d\n",
				__func__, i, opp_count[i]);
#endif
		(*opp_tbl)[i] = kcalloc(NR_MT_CPU_CLUSTER_OPP, sizeof(unsigned int), GFP_KERNEL);

		for (j = 0; j < opp_count[i]; j++)
			(*opp_tbl)[i][j] = opp_table[i][j];
	}
	return 0;
}

void fpsgo_get_cpu_opp_info_by_idx(void)
{
	int cluster, idx, frequency;

	policy_num = NR_MT_CPU_CLUSTER;
	opp_count = kcalloc(NR_MT_CPU_CLUSTER, sizeof(int), GFP_KERNEL);
	opp_table = kcalloc(NR_MT_CPU_CLUSTER, sizeof(unsigned int *), GFP_KERNEL);

        for (cluster = 0; cluster < NR_MT_CPU_CLUSTER; cluster++) {
		opp_count[cluster] = NR_MT_CPU_CLUSTER_OPP;
		opp_table[cluster] = kcalloc(NR_MT_CPU_CLUSTER_OPP, sizeof(unsigned int), GFP_KERNEL);

        for (idx = 0; idx < NR_MT_CPU_CLUSTER_OPP; idx++) {

#if IS_ENABLED(CONFIG_MEDIATEK_CPU_DVFS)
            frequency = mt_cpufreq_get_freq_by_idx(cluster, idx);
#else
            frequency = 0;
#endif /* CONFIG_MEDIATEK_CPU_DVFS */
#if DEBUG_LOG
            pr_info("%s, cluster:%d, opp: %d frequency %d\n",
				__func__, cluster, idx, frequency);
#endif

			opp_table[cluster][idx] = frequency;
        }
    }
#if IS_ENABLED(CONFIG_MEDIATEK_CPU_DVFS)
	parse_dt_topology_arm();
#endif
}

static int parse_cluster(struct device_node *cluster, int depth)
{
    char name[10];
    bool leaf = true;
    bool has_cores = false;
    struct device_node *c;
    static int cluster_id;
    int core_id = 0;
    int i;

    // 遍历子集群
    i = 0;
    do {
        snprintf(name, sizeof(name), "cluster%d", i);
        c = of_get_child_by_name(cluster, name);
        if (c) {
            leaf = false;
            int ret = parse_cluster(c, depth + 1);
            of_node_put(c);
            if (ret != 0)
                return ret;
        }
        i++;
    } while (c);

    // 遍历核心
    i = 0;
    do {
        snprintf(name, sizeof(name), "core%d", i);
        c = of_get_child_by_name(cluster, name);
        if (c) {
            has_cores = true;
            core_id++;  // 增加核心计数

            if (depth == 0) {
                pr_info("%pOF: cpu-map children should be clusters\n", c);
                of_node_put(c);
                return -EINVAL;
            }

            of_node_put(c);
        }
        i++;
    } while (c);

    if (leaf && !has_cores)
        pr_info("%pOF: empty cluster\n", cluster);

    if (leaf) {
        cores_per_cluster[cluster_id] = core_id;
        pr_info("%s: cluster_id:%d, core_num= %d\n", __func__, cluster_id, core_id);
        cluster_id++;
        cluster_count = cluster_id;
    }
    return 0;
}

static int parse_dt_topology_arm(void)
{
	struct device_node *cn_cpus = NULL;
	struct device_node *map;
	int ret;

	cn_cpus = of_find_node_by_path("/cpus");
	if (!cn_cpus) {
#if DEBUG_LOG
		pr_info("No CPU information found in DT\n");
#endif
		return -EINVAL;
	}

	map = of_get_child_by_name(cn_cpus, "cpu-map");
	if (!map) {
#if DEBUG_LOG
		pr_info("No cpu-map information found in DT\n");
#endif
		return -EINVAL;
	}

	ret = parse_cluster(map, 0);
	of_node_put(map);

	if (ret == 0) {
        for (int i = 0; i < cluster_count; i++) {
#if DEBUG_LOG
            pr_info("Cluster %d has %d cores\n", i, cores_per_cluster[i]);
#endif
        }
    }

	return ret;
}

void get_cpu_cluster_core(struct fbt_cpu_dvfs_info *cpu_dvfs)
{
	int i;
    int current_first_cpu = 0;

    if (!cpu_dvfs) {
#if DEBUG_LOG
        pr_err("Invalid input pointers\n");
#endif
		return;
    }

    for (i = 0; i < cluster_count; i++) {
        cpu_dvfs[i].num_cpu = cores_per_cluster[i];
        cpu_dvfs[i].first_cpu = current_first_cpu;

        current_first_cpu += cores_per_cluster[i];
    }
}

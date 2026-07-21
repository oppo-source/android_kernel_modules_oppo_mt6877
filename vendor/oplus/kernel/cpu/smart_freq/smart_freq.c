// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/percpu-defs.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/pm_qos.h>
#include <linux/sched/cputime.h>
#include <linux/jump_label.h>
#include <linux/cgroup.h>
#include <uapi/linux/sched/types.h>
#include <linux/cpuidle.h>
#include <linux/sched/clock.h>
#include <trace/hooks/cgroup.h>
#include <linux/arch_topology.h>
#include <trace/hooks/sched.h>
#include <kernel/sched/sched.h>
#include <linux/kmemleak.h>
#include <linux/sysctl.h>
#include <linux/bitops.h>
#include "smart_freq.h"
#include <linux/sysfs.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>


#define CREATE_TRACE_POINTS
#include "smart_freq_trace.h"

#define MAX_CLUSTERS 3
#define MAX_NAMELEN 16
#define MAX_FREQ  2147483647
#define CPUFREQ_SMART_FREQ_BIT BIT(26)
#define MAX_BUF_SIZE 256

DEFINE_PER_CPU(u64, intr_cnt);
DEFINE_PER_CPU(u64, cycle_cnt);
DEFINE_PER_CPU(u64, ipc_level);
DEFINE_PER_CPU(u64, ipc_cnt);
DEFINE_PER_CPU(u64, last_ipc_update);
DEFINE_PER_CPU(u64, ipc_deactivate_ns);
DEFINE_PER_CPU(bool, tickless_mode);


static bool smart_freq_init_done;
static unsigned int smart_freq_enable = 0;
static unsigned int smart_freq_debug_enable = 0;
static bool is_sbe_rescue;
__read_mostly int num_smart_freq_clusters;
static DEFINE_MUTEX(freq_reason_mutex);


/* Common config for 3 cluster system */
static struct smart_freq_cluster_info smart_freq_info[MAX_CLUSTERS];
static char sysctl_ipc_freq_levels_cluster1[MAX_BUF_SIZE] = "200:2700000,300:2147483647,500:2147483647";
static char sysctl_ipc_freq_levels_cluster2[MAX_BUF_SIZE] = "200:2750000,300:2147483647,500:2147483647";

static int arch_get_nr_clusters(void)
{
	int max_id = 0;
	unsigned int cpu = 0;
	int __arch_nr_clusters = -1;

	/* assume socket id is monotonic increasing without gap. */
	for_each_possible_cpu(cpu) {
		struct cpu_topology *cpu_topo = &cpu_topology[cpu];

		if (cpu_topo->cluster_id > max_id)
			max_id = cpu_topo->cluster_id;
	}
	__arch_nr_clusters = max_id + 1;
	return __arch_nr_clusters;
}

void smart_freq_ceiling_free_enable(bool rescue_enable)
{
	if (!smart_freq_init_done || !smart_freq_enable)
		return;

	if (is_sbe_rescue == rescue_enable)
		return;

	is_sbe_rescue = rescue_enable;
}
EXPORT_SYMBOL(smart_freq_ceiling_free_enable);

noinline int tracing_mark_write(const char *buf)
{
	trace_printk(buf);
	return 0;
}

static void arch_get_cluster_cpus(struct cpumask *cpus, int cluster_id)
{
	unsigned int cpu;

	cpumask_clear(cpus);
	for_each_possible_cpu(cpu) {
		struct cpu_topology *cpu_topo = &cpu_topology[cpu];

		if (cpu_topo->cluster_id == cluster_id)
			cpumask_set_cpu(cpu, cpus);
	}
}

/* return highest ipc of the cluster */
static unsigned int get_cluster_ipc_level_freq(int curr_cpu, u64 time)
{
	int cpu = 0, winning_cpu = 0, cpu_ipc_level = 0, index = 0;
	int cluster_id = topology_cluster_id(curr_cpu);

	if (!smart_freq_init_done || !smart_freq_enable)
		return MAX_FREQ;

	if (!smart_freq_info[cluster_id].smart_freq_ipc_participation_mask)
		return MAX_FREQ;

	for_each_cpu(cpu, &smart_freq_info[cluster_id].cpu_mask) {
		cpu_ipc_level = per_cpu(ipc_level, cpu);
		if ((time - per_cpu(last_ipc_update, cpu)) > 7999999ULL) {
			cpu_ipc_level = 0;
			per_cpu(tickless_mode, cpu) = true;
		} else {
			per_cpu(tickless_mode, cpu) = false;
		}

		if (cpu_ipc_level >= index) {
			winning_cpu = cpu;
			index = cpu_ipc_level;
		}
	}

	smart_freq_info[cluster_id].cluster_ipc_level = index;
	trace_ipc_freq(cluster_id, winning_cpu, index,
		smart_freq_info[cluster_id].ipc_reason_config[index].freq_allowed,
		time, per_cpu(ipc_deactivate_ns, winning_cpu), curr_cpu,
		per_cpu(ipc_cnt, curr_cpu));

	return smart_freq_info[cluster_id].ipc_reason_config[index].freq_allowed;
}

void smart_freq_update(unsigned int cpu, u64 time, unsigned int flags)
{
	int cluster_id = 0;

	if (!smart_freq_init_done || !smart_freq_enable)
		return;

	cluster_id = topology_cluster_id(cpu);
	if (flags & CPUFREQ_SMART_FREQ_BIT)
		smart_freq_info[cluster_id].cluster_freq =
				get_cluster_ipc_level_freq(cpu, time);

	if (is_sbe_rescue)
		smart_freq_info[cluster_id].cluster_freq = MAX_FREQ;
}
EXPORT_SYMBOL(smart_freq_update);

unsigned int smart_freq_update_final_freq(struct cpumask *cpumask, unsigned int freq)
{
	unsigned int cluster_id = 0;
	unsigned int max_freq = 0;

	if (!cpumask || !smart_freq_init_done || !smart_freq_enable)
		return freq;

	cluster_id = topology_cluster_id(cpumask_first(cpumask));
	if (cluster_id == 0)
		return freq;

	if (is_sbe_rescue)
		max_freq = MAX_FREQ;
	else
		max_freq = smart_freq_info[cluster_id].cluster_freq;

	return freq > max_freq ? max_freq : freq;
}
EXPORT_SYMBOL(smart_freq_update_final_freq);

static unsigned long smart_freq_calculate_ipc(int cpu, int cluster_id)
{
	unsigned long ipc = 0;
	u64 amu_cnt, delta_cycl = 0, delta_intr = 0;
	u64 prev_cycl_cnt = per_cpu(cycle_cnt, cpu);
	u64 prev_intr_cnt = per_cpu(intr_cnt, cpu);


	amu_cnt = read_sysreg_s(SYS_AMEVCNTR0_CORE_EL0);
	delta_cycl = amu_cnt - prev_cycl_cnt;
	per_cpu(cycle_cnt, cpu) = amu_cnt;

	amu_cnt = read_sysreg_s(SYS_AMEVCNTR0_INST_RET_EL0);
	per_cpu(intr_cnt, cpu) = amu_cnt;
	delta_intr = amu_cnt - prev_intr_cnt;

	if (prev_cycl_cnt > 0 && delta_cycl > 0)
		ipc = (delta_intr * 100) / delta_cycl;

	per_cpu(ipc_cnt, cpu) = ipc;
	per_cpu(last_ipc_update, cpu) = cpu_rq(cpu)->clock;

	if (prev_cycl_cnt)
		trace_ipc_update(cpu, per_cpu(cycle_cnt, cpu), per_cpu(intr_cnt, cpu),
			per_cpu(ipc_cnt, cpu), per_cpu(last_ipc_update, cpu),
			per_cpu(ipc_deactivate_ns, cpu), cpu_rq(cpu)->clock);

	return ipc;
}

static void smart_freq_sched_tick(void *data, struct rq *rq)
{
	int i = 0;
	int cluster_id = -1;
	u64 last_ipc_level = 0;
	u64 curr_ipc_level = 0;
	u64 last_deactivate_ns = 0;
	unsigned long ipc = 0;
	int  cpu = cpu_of(rq);
	bool inform_governor = false;
	int max_ipc_count = 0;

	if (!smart_freq_init_done || !smart_freq_enable)
		return;

	/* IPC based smart FMAX */
	cluster_id = topology_cluster_id(cpu);
	max_ipc_count = hweight32(smart_freq_info[cluster_id].smart_freq_ipc_participation_mask);
	if (smart_freq_info[cluster_id].smart_freq_ipc_participation_mask & IPC_PARTICIPATION) {
		last_ipc_level = per_cpu(ipc_level, cpu);
		last_deactivate_ns = per_cpu(ipc_deactivate_ns, cpu);
		ipc = smart_freq_calculate_ipc(cpu, cluster_id);
		for (i = 0; i < max_ipc_count; i++)
			if (ipc < smart_freq_info[cluster_id].ipc_reason_config[i].ipc)
				break;

		if (i >= max_ipc_count)
			i = max_ipc_count - 1;

		curr_ipc_level = i;
		if ((curr_ipc_level != last_ipc_level)  || per_cpu(tickless_mode, cpu))
			inform_governor = true;

		if ((curr_ipc_level < last_ipc_level) &&
			(smart_freq_info[cluster_id].ipc_reason_config[last_ipc_level].hyst_ns > 0)) {
			if (!last_deactivate_ns) {
				per_cpu(ipc_deactivate_ns, cpu) = rq->clock;
				inform_governor = false;
			} else {
				u64 delta =  rq->clock - last_deactivate_ns;

				if (smart_freq_info->ipc_reason_config[last_ipc_level].hyst_ns >
					delta)
					inform_governor = false;
			}
		}

		if (inform_governor) {
			if (smart_freq_debug_enable && (per_cpu(ipc_level, cpu) != curr_ipc_level)) {
				char buf[256];

				snprintf(buf, sizeof(buf), "C|9999|Cpu%d_ipc_level|%lu\n", cpu, ipc);
				tracing_mark_write(buf);
			}
			per_cpu(ipc_level, cpu) = curr_ipc_level;
			per_cpu(ipc_deactivate_ns, cpu) = 0;
			cpufreq_update_util(cpu_rq(cpu), CPUFREQ_SMART_FREQ_BIT);
		}
	}
}

static int sched_smart_freq_to_string(int cluster_id, char *buf, size_t size)
{
	int len = 0;
	int i = 0;
	for (i = 0; i < SMART_FMAX_IPC_MAX; i++) {
		int ipc  = smart_freq_info[cluster_id].ipc_reason_config[i].ipc;
		int freq = smart_freq_info[cluster_id].ipc_reason_config[i].freq_allowed;
		int n = snprintf(buf + len, size - len, "%d:%d", ipc, freq);
		if (n >= size - len)
			return -ENOSPC;

		len += n;
		if (i < SMART_FMAX_IPC_MAX - 1) {
			if (len + 1 >= size)
				return -ENOSPC;
			buf[len++] = ',';
		}
	}
	buf[len] = '\0';

	return 0;
}

static int sched_smart_freq_parse_store(int cluster_id, char *str)
{
	char *token, *tmp, *buf;
	unsigned int ipc, freq;
	int count = 0;

	buf = kstrdup(str, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	tmp = buf;
	mutex_lock(&freq_reason_mutex);
	while ((token = strsep(&tmp, ",")) != NULL) {
		if (sscanf(token, "%u:%u", &ipc, &freq) == 2) {
			smart_freq_info[cluster_id].ipc_reason_config[count].ipc = ipc;
			smart_freq_info[cluster_id].ipc_reason_config[count].freq_allowed = freq;

			count++;
			if (count >= SMART_FMAX_IPC_MAX)
				goto done;
		}

	}
done:
	mutex_unlock(&freq_reason_mutex);
	kfree(buf);
	return 0;
}


static int sched_smart_freq_ipc_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp,
			long long *ppos)
{
	int ret = 0;
	int cluster_id = -1;
	char *temp;
	char *data = (char *)table->data;
	struct ctl_table local_table = *table;

	if (!smart_freq_init_done)
		return -EINVAL;

	temp = kzalloc(MAX_BUF_SIZE, GFP_KERNEL);
	if (!temp)
		return -ENOMEM;

	if (data == &sysctl_ipc_freq_levels_cluster1[0])
		cluster_id = 1;
	if (data == &sysctl_ipc_freq_levels_cluster2[0])
		cluster_id = 2;

	if (cluster_id == -1)
		goto unlock;

	if (!write) {
		ret = sched_smart_freq_to_string(cluster_id, temp, MAX_BUF_SIZE);
		if (ret)
			goto unlock;

		local_table.data   = temp;
		local_table.maxlen = strlen(temp);
		ret = proc_dostring(&local_table, 0, buffer, lenp, ppos);
		goto unlock;
	}

	local_table.data = temp;
	local_table.maxlen = MAX_BUF_SIZE - 1;
	ret = proc_dostring(&local_table, write, buffer, lenp, ppos);
	if (ret)
		goto unlock;
	temp[MAX_BUF_SIZE - 1] = '\0';
	ret = sched_smart_freq_parse_store(cluster_id, temp);
	if (ret)
		goto unlock;

unlock:
	kfree(temp);
	return ret;
}

struct ctl_table smart_freq_table[] = {
	{
		.procname   = "enable",
		.data       = &smart_freq_enable,
		.maxlen     = sizeof(unsigned int),
		.mode       = 0644,
		.proc_handler   = proc_dointvec,
		.extra1     = SYSCTL_ZERO,
		.extra2     = SYSCTL_ONE,
	},
	{
		.procname   = "debug_enable",
		.data       = &smart_freq_debug_enable,
		.maxlen     = sizeof(unsigned int),
		.mode       = 0644,
		.proc_handler   = proc_dointvec,
		.extra1     = SYSCTL_ZERO,
		.extra2     = SYSCTL_ONE,
	},
	{
		.procname   = "ipc_freq_levels_cluster1",
		.data       = &sysctl_ipc_freq_levels_cluster1,
		.maxlen     = MAX_BUF_SIZE,
		.mode       = 0644,
		.proc_handler   = sched_smart_freq_ipc_handler,
	},
	{
		.procname   = "ipc_freq_levels_cluster2",
		.data       = &sysctl_ipc_freq_levels_cluster2,
		.maxlen     = MAX_BUF_SIZE,
		.mode       = 0644,
		.proc_handler   = sched_smart_freq_ipc_handler,
	}
};

static void smart_ipc_init_mt6899(void)
{
	unsigned int cluster_id = 0;
	struct cpumask cluster_cpus;

	for (cluster_id = 0; cluster_id < num_smart_freq_clusters; cluster_id++) {
		arch_get_cluster_cpus(&cluster_cpus, cluster_id);
		smart_freq_info[cluster_id].cluster_id = cluster_id;
		smart_freq_info[cluster_id].cluster_ipc_level = 0;
		cpumask_copy(&smart_freq_info[cluster_id].cpu_mask, &cluster_cpus);

		if (cluster_id == 0) {
			/* IPC */
			smart_freq_info[cluster_id].smart_freq_ipc_participation_mask = 0;
			smart_freq_info[cluster_id].ipc_reason_config[0].ipc = 300;
			smart_freq_info[cluster_id].ipc_reason_config[0].freq_allowed = MAX_FREQ;
			smart_freq_info[cluster_id].min_cycles = 5806080;
			smart_freq_info[cluster_id].cluster_freq = MAX_FREQ;
		} else if (cluster_id == 1) {
			/* IPC */
			smart_freq_info[cluster_id].smart_freq_ipc_participation_mask = BIT(IPC_A) | BIT(IPC_B) | BIT(IPC_C);
			smart_freq_info[cluster_id].ipc_reason_config[0].ipc = 300;
			smart_freq_info[cluster_id].ipc_reason_config[0].freq_allowed = 2600000;
			smart_freq_info[cluster_id].ipc_reason_config[1].ipc = 500;
			smart_freq_info[cluster_id].ipc_reason_config[1].freq_allowed = MAX_FREQ;
			smart_freq_info[cluster_id].ipc_reason_config[2].ipc = 800;
			smart_freq_info[cluster_id].ipc_reason_config[2].freq_allowed = MAX_FREQ;
			smart_freq_info[cluster_id].min_cycles = 5806080;
			smart_freq_info[cluster_id].cluster_freq = MAX_FREQ;
		} else if (cluster_id == 2) {
			/* IPC */
			smart_freq_info[cluster_id].smart_freq_ipc_participation_mask = BIT(IPC_A) | BIT(IPC_B) | BIT(IPC_C);
			smart_freq_info[cluster_id].ipc_reason_config[0].ipc = 300;
			smart_freq_info[cluster_id].ipc_reason_config[0].freq_allowed = 2500000;
			smart_freq_info[cluster_id].ipc_reason_config[1].ipc = 500;
			smart_freq_info[cluster_id].ipc_reason_config[1].freq_allowed = MAX_FREQ;
			smart_freq_info[cluster_id].ipc_reason_config[2].ipc = 800;
			smart_freq_info[cluster_id].ipc_reason_config[2].freq_allowed = MAX_FREQ;
			smart_freq_info[cluster_id].min_cycles = 5806080;
			smart_freq_info[cluster_id].cluster_freq = MAX_FREQ;
		}
	}
}

struct ctl_table_header *hdr;
static int __init smart_freq_init(void)
{
	int ret = -1;
	struct device_node *np;
	const char *soc_id = NULL;

	num_smart_freq_clusters = arch_get_nr_clusters();
	if (num_smart_freq_clusters <= 0)
		return ret;

	np = of_find_node_by_path("/");
	of_property_read_string(np, "model", &soc_id);
	if (!soc_id)
		return ret;

	if (!strcmp(soc_id, "MT6899")) {
		hdr = register_sysctl("smart_freq", smart_freq_table);
		if (!hdr)
			return -ENOMEM;
		smart_ipc_init_mt6899();
	} else {
		return 0;
	}

	/* register tick_entry */
	ret = register_trace_android_vh_scheduler_tick(smart_freq_sched_tick, NULL);
	if (ret) {
		pr_err("register android_rvh_tick_entry failed\n");
		return ret;
	}

	smart_freq_init_done = true;

	return 0;
}

static void __exit smart_freq_exit(void)
{
	if (!smart_freq_init_done)
		return;

	unregister_trace_android_vh_scheduler_tick(smart_freq_sched_tick, NULL);
	unregister_sysctl_table(hdr);
}

module_init(smart_freq_init);
module_exit(smart_freq_exit);
MODULE_DESCRIPTION("Oplus Smart Freq Modular");
MODULE_LICENSE("GPL v2");

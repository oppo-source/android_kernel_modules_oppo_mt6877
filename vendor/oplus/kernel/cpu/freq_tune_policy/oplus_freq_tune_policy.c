// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/cpumask.h>
#include <linux/string.h>

#define MAX_ENTRIES 16
#define MAX_CLUSTERS 5

static struct proc_dir_entry *dir_entry;
static struct proc_dir_entry *inefficient_proc_entry;
static struct proc_dir_entry *disable_proc_entry;

#define CPUFREQ_OPLUS_INEFFICIENT_FREQ	(1 << 5)

struct freq_entry {
	unsigned int cpu;
	unsigned long freq;
};
static struct freq_entry entries[MAX_ENTRIES];
static unsigned int num_entries;

static DEFINE_MUTEX(g_mutex);
static bool have_set_inefficient = false;
static bool disable_inefficient = false;
static bool inefficient_backup[MAX_CLUSTERS];

static int set_inefficient_freq(unsigned int cpu, unsigned long freq)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	struct cpufreq_frequency_table *pos;

	/* Not supported */
	if (!policy)
		return -EINVAL;

	if (policy->freq_table_sorted == CPUFREQ_TABLE_UNSORTED)
		goto err;

	cpufreq_for_each_valid_entry(pos, policy->freq_table) {
		if (pos->frequency == freq) {
			pos->flags |= CPUFREQ_OPLUS_INEFFICIENT_FREQ | CPUFREQ_INEFFICIENT_FREQ;
			policy->efficiencies_available = true;
			have_set_inefficient = true;
			cpufreq_cpu_put(policy);
			return 0;
		}
	}

err:
	cpufreq_cpu_put(policy);
	return -EINVAL;
}

/**
 * Parse user input (format: cpu:freq,cpu:freq,...)
 * Example: 0:2000000,1:1500000
 */
static ssize_t inefficient_write(struct file *file, const char __user *buf,
								 size_t count, loff_t *ppos)
{
	char *kbuf, *next, *token;
	unsigned int cpu;
	unsigned long freq;
	int ret = 0;

	kbuf = kzalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	mutex_lock(&g_mutex);

	num_entries = 0;
	next = kbuf;
	token = strsep(&next, ",");
	while (token) {
		char *colon, *cpu_str;

		if (num_entries >= MAX_ENTRIES) {
			ret = -ENOSPC;
			break;
		}

		if (*token == '\0')
			continue;

		cpu_str = token;
		colon = strsep(&cpu_str, ":");
		if (!colon || !cpu_str) {
			ret = -EINVAL;
			break;
		}

		if (kstrtouint(colon, 10, &cpu)) {
			ret = -EINVAL;
			break;
		}

		if (cpu >= nr_cpu_ids || !cpu_possible(cpu)) {
			ret = -EINVAL;
			break;
		}

		if (kstrtoul(cpu_str, 10, &freq)) {
			ret = -EINVAL;
			break;
		}

		entries[num_entries].cpu = cpu;
		entries[num_entries].freq = freq;
		if (set_inefficient_freq(cpu, freq))
			pr_warn("set_inefficient_freq CPU:%u freq:%lu\n", cpu, freq);
		num_entries++;
		token = strsep(&next, ",");
	}

	mutex_unlock(&g_mutex);

out:
	kfree(kbuf);
	return ret ? ret : count;
}

static int inefficient_show(struct seq_file *m, void *v)
{
	for (int i = 0; i < num_entries; ++i) {
		seq_printf(m, "%u:%lu%s",
				  entries[i].cpu,
				  entries[i].freq,
				  (i == num_entries - 1) ? "\n" : ",");
	}
	return 0;
}

static int inefficient_open(struct inode *inode, struct file *file)
{
	return single_open(file, inefficient_show, NULL);
}

static const struct proc_ops inefficient_fops = {
	.proc_open = inefficient_open,
	.proc_read = seq_read,
	.proc_write = inefficient_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static ssize_t disable_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[32] = {0};
	int ret, value;
	bool disable;
	int i, j, cluster_id;
	struct cpufreq_policy *policy;
	struct cpumask temp_mask;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	ret = sscanf(page, "%d", &value);
	if (ret != 1)
		return -EINVAL;

	disable = !!value;

	mutex_lock(&g_mutex);

	if (!have_set_inefficient)
		goto out;

	if (disable_inefficient == disable)
		goto out;

	disable_inefficient = disable;

	cpumask_copy(&temp_mask, cpu_possible_mask);

	if (disable_inefficient) {
		for (i = 0; i < MAX_CLUSTERS; i++)
			inefficient_backup[i] = false;

		for_each_cpu(i, &temp_mask) {
			policy = cpufreq_cpu_get(i);
			if (unlikely(!policy))
				continue;

			for_each_cpu(j, policy->related_cpus)
				cpumask_clear_cpu(j, &temp_mask);

			cluster_id = topology_cluster_id(policy->cpu);

			if (cluster_id < MAX_CLUSTERS) {
				if (policy->efficiencies_available) {
					inefficient_backup[cluster_id] = true;
					/* set efficiencies_available false */
					policy->efficiencies_available = false;
				}
			}

			cpufreq_cpu_put(policy);
		}
	} else {
		for_each_cpu(i, &temp_mask) {
			policy = cpufreq_cpu_get(i);
			if (unlikely(!policy))
				continue;

			for_each_cpu(j, policy->related_cpus)
				cpumask_clear_cpu(j, &temp_mask);

			cluster_id = topology_cluster_id(policy->cpu);

			if (cluster_id < MAX_CLUSTERS) {
				/* restore efficiencies_available */
				if (inefficient_backup[cluster_id])
					policy->efficiencies_available = true;
			}

			cpufreq_cpu_put(policy);
		}
	}

out:
	mutex_unlock(&g_mutex);
	return count;
}

static int disable_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", disable_inefficient ? 1 : 0);
	return 0;
}

static int disable_open(struct inode *inode, struct file *file)
{
	return single_open(file, disable_show, NULL);
}

static const struct proc_ops disable_fops = {
	.proc_open = disable_open,
	.proc_read = seq_read,
	.proc_write = disable_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init inefficient_init(void)
{
	dir_entry = proc_mkdir("oplus_cpu", NULL);
	if (!dir_entry)
		return -ENOMEM;

	inefficient_proc_entry = proc_create("inefficient", 0644, dir_entry, &inefficient_fops);
	if (!inefficient_proc_entry) {
		proc_remove(dir_entry);
		return -ENOMEM;
	}

	disable_proc_entry = proc_create("disable_inefficient", 0644, dir_entry, &disable_fops);
	if (!disable_proc_entry) {
		proc_remove(inefficient_proc_entry);
		proc_remove(dir_entry);
		return -ENOMEM;
	}

	return 0;
}

static void __exit inefficient_exit(void)
{
	if (disable_proc_entry)
		proc_remove(disable_proc_entry);
	if (inefficient_proc_entry)
		proc_remove(inefficient_proc_entry);
	if (dir_entry)
		proc_remove(dir_entry);
}

module_init(inefficient_init);
module_exit(inefficient_exit);
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 MediaTek Inc.
 */

#include <linux/cdev.h>
#include <linux/cpufreq.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include "user.h"

static const int super_group_431[] = {7};
static const int big_group_431[] = {4, 5, 6};
static const int little_group_431[] = {0, 1, 2, 3};
static const int big_group_62[] = {6, 7};
static const int little_group_62[] = {0, 1, 2, 3, 4, 5};
static const int big_group_44[] = {4, 5, 6, 7};
static const int little_group_44[] = {0, 1, 2, 3};


static const int all_group[] = {0, 1, 2, 3, 4, 5, 6, 7};

struct cpu_group_map {
	const int *cpu;
	int count;
};

static const struct cpu_group_map cpu_group_maps[CPU_MAP_MAX][CPU_GROUP_MAX] = {
	[CPU_4_3_1_MAP] = {
		[CPU_SUPER_GROUP] = {super_group_431, ARRAY_SIZE(super_group_431)},
		[CPU_BIG_GROUP] = {big_group_431, ARRAY_SIZE(big_group_431)},
		[CPU_LITTLE_GROUP] = {little_group_431, ARRAY_SIZE(little_group_431)},
	},
	[CPU_6_2_MAP] = {
		[CPU_SUPER_GROUP] = {all_group, 1},
		[CPU_BIG_GROUP] = {big_group_62, ARRAY_SIZE(big_group_62)},
		[CPU_LITTLE_GROUP] = {little_group_62, ARRAY_SIZE(little_group_62)},
	},
	[CPU_4_4_MAP] = {
		[CPU_SUPER_GROUP] = {all_group, 1},
		[CPU_BIG_GROUP] = {big_group_44, ARRAY_SIZE(big_group_44)},
		[CPU_LITTLE_GROUP] = {little_group_44, ARRAY_SIZE(little_group_44)},
	},
};

static struct freq_qos_request teeperf_min_freq_req[NR_CPUS];

static void teeperf_set_cpu_to_high_freq(int target_cpu, u32 high_freq, u32 freq_index_level)
{
	struct cpufreq_policy *policy;
	int index = -1, min_index = -1;

	if (target_cpu >= num_possible_cpus()) {
		pr_info(PFX "invalid target CPU %d\n", target_cpu);
		return;
	}

	policy = cpufreq_cpu_get(target_cpu);
	if (policy == NULL) {
		pr_info(PFX "invalid policy, target cpu (%d)\n", target_cpu);
		return;
	}

	down_write(&policy->rwsem);
	min_index = cpufreq_table_find_index_dl(policy, 0, false);
	if (high_freq) {
		if (freq_index_level > min_index)
			index = min_index;
		else
			index = freq_index_level;
	} else
		index = min_index;
	up_write(&policy->rwsem);

	if (high_freq) {
		if (!freq_qos_request_active(&teeperf_min_freq_req[target_cpu])) {
			int ret = freq_qos_add_request(&policy->constraints,
				&teeperf_min_freq_req[target_cpu], FREQ_QOS_MIN,
				policy->freq_table[index].frequency);
			if (ret < 0) {
				pr_info(PFX"freq_qos_add_request failed for CPU %d, ret %d\n", target_cpu, ret);
				cpufreq_cpu_put(policy);
				return;
			}
		}
		freq_qos_update_request(&teeperf_min_freq_req[target_cpu],
					policy->freq_table[index].frequency);
		pr_debug(PFX "CPU%d set min_freq=%u (index=%d)\n", target_cpu,
			 policy->freq_table[index].frequency, index);
	} else {
		if (freq_qos_request_active(&teeperf_min_freq_req[target_cpu]))
			freq_qos_remove_request(&teeperf_min_freq_req[target_cpu]);
	}
	cpufreq_cpu_put(policy);
}

static void teeperf_set_cpu_group_to_high_freq(enum teeperf_cpu_group group, u32 high_freq)
{
	unsigned int freq_index_level = 19;
	int i;
	int group_idx = -1;
	const struct cpu_group_map *group_map;

	if (cpu_index >= 0)
		freq_index_level = cpu_index;

	switch(group) {
	case CPU_SUPER_GROUP:
		group_idx = CPU_SUPER_GROUP;
		break;
	case CPU_BIG_GROUP:
		group_idx = CPU_BIG_GROUP;
		break;
	case CPU_LITTLE_GROUP:
		group_idx = CPU_LITTLE_GROUP;
		break;
	default:
		group_idx = -1;
		break;
	}

	if(group_idx < 0) {
		for (i = 0; i < num_possible_cpus(); i++)
			teeperf_set_cpu_to_high_freq(i, high_freq, 0);
		return;
	}

	group_map = &cpu_group_maps[cpu_map][group_idx];

	for (i = 0; i < group_map->count; i++)
		teeperf_set_cpu_to_high_freq(group_map->cpu[i], high_freq, freq_index_level);
}

static void teeperf_high_freq(enum teeperf_cpu_type type, u32 high_freq)
{
	if (type == CPU_V9_TYPE)
		teeperf_set_cpu_group_to_high_freq(CPU_BIG_GROUP, high_freq);
	else
		teeperf_set_cpu_group_to_high_freq(CPU_LITTLE_GROUP, high_freq);
}

static int teeperf_user_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int teeperf_user_release(struct inode *inode, struct file *file)
{
	return 0;
}

static inline int teeperf_ioctl_check_pointer(unsigned int cmd, int __user *uarg)
{
	size_t size = _IOC_SIZE(cmd);

	if (size == 0)
		return 0;
	if (!access_ok(uarg, size))
		return -EFAULT;

	return 0;
}

static long teeperf_user_ioctl(struct file *file, unsigned int id, unsigned long arg)
{
	int __user *uarg = (int __user *)arg;
	int ret = -EINVAL;

	pr_info(PFX "%u from %s\n", _IOC_NR(id), current->comm);

	if (teeperf_ioctl_check_pointer(id, uarg))
		return -EFAULT;

	switch (id) {
	case TEEPERF_IO_HIGH_FREQ: {
		enum teeperf_cpu_type type = cpu_type;
		u32 high_freq;

		if (copy_from_user(&high_freq, uarg, sizeof(high_freq))) {
			ret = -EFAULT;
			break;
		}
		teeperf_high_freq(type, high_freq);

		ret = 0;
		break;
	}
	default:
		ret = -ENOIOCTLCMD;
		pr_info(PFX "unsupported command, id %d\n", id);
	}

	return ret;
}

ssize_t teeperf_dbg_write(struct file *file, const char __user *buffer,
	size_t count, loff_t *data)
{
	enum teeperf_cpu_type type = cpu_type;
	char *pinput, *cmd_str, *parm_str;
	char input[64] = {0};
	long param;
	size_t len;
	int err;
	u32 high_freq;

	len = (count < (sizeof(input) - 1)) ? count : (sizeof(input) - 1);
	if (copy_from_user(input, buffer, len)) {
		pr_debug(PFX "copy from user failed\n");
		return -EFAULT;
	}

	input[len] = '\0';
	pinput = input;

	cmd_str = strsep(&pinput, " ");
	if (!cmd_str)
		return -EINVAL;

	parm_str = strsep(&pinput, " ");
	if (!parm_str)
		return -EINVAL;

	err = kstrtol(parm_str, 10, &param);
	if (err)
		return err;
	if (param < 0 || param > INT_MAX){
		pr_debug(PFX "Invalid param value; %ld\n", param);
		return -EINVAL;
	}
	if (!strncmp(cmd_str, "teeperf_ut", strlen("teeperf_ut"))) {
		if (param != 0)
			high_freq = 1;
		else
			high_freq = 0;

		teeperf_high_freq(type, high_freq);
	} else {
		return -EINVAL;
	}

	return count;
}

static const struct file_operations teeperf_user_fops = {
	.owner = THIS_MODULE,
	.open = teeperf_user_open,
	.release = teeperf_user_release,
	.unlocked_ioctl = teeperf_user_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = teeperf_user_ioctl,
#endif
};

static const struct proc_ops teeperf_dbg_fops = {
	.proc_write = teeperf_dbg_write,
};

int teeperf_user_init(struct cdev *cdev)
{
	cdev_init(cdev, &teeperf_user_fops);
	proc_create("teeperf_dbg", 0660, NULL, &teeperf_dbg_fops);
	return 0;
}

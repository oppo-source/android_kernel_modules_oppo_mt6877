// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/perf_event.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <net/genetlink.h>
#include <linux/sched/mm.h>
#include <linux/freezer.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/swap.h>
#include <linux/cpufreq.h>
#include <linux/workqueue.h>
#include <linux/tick.h>
#include <linux/power_supply.h>
#include <linux/version.h>
#include <linux/kernel_stat.h>
#include <linux/topology.h>
#if defined(CONFIG_MTK_UNIFY_POWER)
#include "../drivers/misc/mediatek/base/power/include/mtk_upower.h"
#else
#include <linux/energy_model.h>
#endif
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/thermal.h>
#include <linux/input.h>
#include <linux/sched/cputime.h>
#if defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST) || defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST_MODULE)
#include <../kernel/oplus_cpu/sched/sched_assist/sa_common.h>
#endif
#if !defined(CONFIG_MTK_PLATFORM)
#include <linux/sched/sysctl.h>
#endif

#include "osml.h"

struct task_struct *osml_polling_tsk;
static unsigned int osml_sample_rate = 1000;
static int osml_enable;
static int osml_pid;
static unsigned int record_cnt;
static char *monitor_case[MONITOR_SIZE] = {
	"timestamp",
	"frame_duration", "allow_frame_duration",
	"mainthread_cpu_id", "renderthread_cpu_id", "task_load",
	"cpu_load_cluster_0", "cpu_load_cluster_1",
#if (OSML_CLUSTER > 2)
	"cpu_load_cluster_2",
#if (OSML_CLUSTER > 3)
	"cpu_load_cluster_3",
#endif
#endif
	"cpu_freq_cluster_0", "cpu_freq_cluster_1",
#if (OSML_CLUSTER > 2)
	"cpu_freq_cluster_2",
#if (OSML_CLUSTER > 3)
	"cpu_freq_cluster_3",
#endif
#endif
	"raw-inst-retired", "raw-cpu-cycles", "raw-ll-cache-rd", "raw-ll-cache-miss-rd",
	"ddr_freq", "gpu_freq", "thermal", "swiping", "voltage", "current",
	"scaling_max_freq_0", "scaling_min_freq_0", "scaling_max_freq_1", "scaling_min_freq_1",
#if (OSML_CLUSTER > 2)
	"scaling_max_freq_2", "scaling_min_freq_2",
#if (OSML_CLUSTER > 3)
	"scaling_max_freq_3", "scaling_min_freq_3",
#endif
#endif
	"gpu_max_clock", "gpu_min_clock"
};
struct osml_monitor pmonitor = {
	.buf = NULL,
};
static struct osml_cpuinfo cpu_clus[OSML_MAX_CLUSTER] = {
	{-1, 0, NULL, -1},
	{-1, 0, NULL, -1},
	{-1, 0, NULL, -1},
	{-1, 0, NULL, -1},
};
static int nr_cores = 8;
static int fg_pid;
static struct workqueue_struct *osml_workq;
static atomic_t frame_number = ATOMIC_INIT(-1);
static atomic_t frame_owner = ATOMIC_INIT(0);
static atomic_t frame_duration = ATOMIC_INIT(0);
static atomic_t vsync_period = ATOMIC_INIT(0);

static char *pevent_name[CUSTOM_PEVENT_SIZE] = {
	"raw-ll-cache-rd", "raw-ll-cache-miss-rd",
};
static unsigned int pevent_id[CUSTOM_PEVENT_SIZE] = {
	54, 55,
};
static unsigned int pevent_type[CUSTOM_PEVENT_SIZE] = {
	PERF_TYPE_RAW, PERF_TYPE_RAW,
};
static int pevent_cnt = CUSTOM_PEVENT_SIZE;
module_param_array(pevent_name, charp, NULL, 0664);
module_param_array(pevent_id, uint, &pevent_cnt, 0664);
module_param_array(pevent_type, uint, NULL, 0664);

static int pevent_sample_rate = 20;
module_param_named(pevent_sample_rate, pevent_sample_rate, uint, 0664);

static bool osml_debug;
module_param_named(osml_debug, osml_debug, bool, 0664);

struct power_supply *psy;
struct thermal_zone_device *thermal[SHELL_MAX];
static char *thermal_name[SHELL_MAX] = {
	"shell_front", "shell_frame", "shell_back",
};

static struct list_head list_event_head[MAX_LIST_SIZE] = {
	LIST_HEAD_INIT(list_event_head[OSML_EVENT]),
	LIST_HEAD_INIT(list_event_head[IOC_EVENT])
};
/*0:frame trigger, 1:polling*/
static bool sample_type = 1;
module_param_named(sample_type, sample_type, bool, 0664);
/*0:system-wide, 1:task-wide*/
static bool pevent_dimension;
module_param_named(pevent_dimension, pevent_dimension, bool, 0664);
static int sample_events[MONITOR_SIZE];
static int sample_events_cnt = MONITOR_SIZE;
module_param_array(sample_events, int, &sample_events_cnt, 0664);
static int monitor_type;
static atomic_t input_trigger, input_start;

static DEFINE_MUTEX(list_mutex_lock);

static int frame_cnt_interval = 10;

static int frame_cnt_interval_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int val;

	if ((sscanf(buf, "%u\n", &val) <= 0) || (val > 100)) {
		pr_err("error setting argument. argument should be positive and <= 100\n");
		return -EINVAL;
	}

	frame_cnt_interval = val;
	osml_sample_rate = frame_cnt_interval * 1000/60;
	return 0;
}

static int frame_cnt_interval_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", frame_cnt_interval);
}

static const struct kernel_param_ops frame_cnt_interval_ops = {
	.set = frame_cnt_interval_store,
	.get = frame_cnt_interval_show,
};
module_param_cb(frame_cnt_interval, &frame_cnt_interval_ops, NULL, 0664);


enum {
	RL_MONITOR_MSG_UNDEFINE,
	RL_MONITOR_MSG_SET_ANDROID_PID,
	RL_MONITOR_MSG_REPORT_INFO,
	__RL_MONITOR_MSG_MAX,
};
#define RL_MONITOR_MSG_MAX (__RL_MONITOR_MSG_MAX - 1)
enum {
	RL_MONITOR_CMD_UNSPEC,
	RL_MONITOR_CMD_DOWNLINK,
	RL_MONITOR_CMD_UPLINK,
	__RL_MONITOR_CMD_MAX
};
#define RL_MONITOR_CMD_MAX (__RL_MONITOR_CMD_MAX - 1)
#define RL_MONITOR_FAMILY_VERSION	1
#define RL_MONITOR_FAMILY_NAME "rl_monitor"
static int rl_monitor_netlink_pid;

static int rl_monitor_netlink_rcv_msg(struct sk_buff *skb, struct genl_info *info)
{
	struct nlmsghdr *nlhdr;
	struct genlmsghdr *genlhdr;
	struct nlattr *nla;

	nlhdr = nlmsg_hdr(skb);
	genlhdr = nlmsg_data(nlhdr);
	nla = genlmsg_data(genlhdr);
	if (rl_monitor_netlink_pid != nlhdr->nlmsg_pid) {
		rl_monitor_netlink_pid = nlhdr->nlmsg_pid;
		pr_info("update rl_monitor_netlink_pid=%u", rl_monitor_netlink_pid);
	}

	/* to do: may need to some head check here*/
	return 0;
}
static const struct genl_ops rl_monitor_genl_ops[] = {
	{
		.cmd = RL_MONITOR_CMD_DOWNLINK,
		.flags = 0,
		.doit = rl_monitor_netlink_rcv_msg,
		.dumpit = NULL,
	},
};
static struct genl_family rl_monitor_genl_family = {
	.id = 0,
	.hdrsize = 0,
	.name = RL_MONITOR_FAMILY_NAME,
	.version = RL_MONITOR_FAMILY_VERSION,
	.maxattr = RL_MONITOR_MSG_MAX,
	.ops = rl_monitor_genl_ops,
	.n_ops = ARRAY_SIZE(rl_monitor_genl_ops),
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
	.resv_start_op = __RL_MONITOR_CMD_MAX,
#endif
};

static int rl_monitor_netlink_init(void)
{
	int ret;

	ret = genl_register_family(&rl_monitor_genl_family);
	if (ret) {
		pr_info("osml:genl_register_family failed,ret = %d\n", ret);
		return ret;
	}
	pr_info("osml:genl_register_family complete, id = %d!\n", rl_monitor_genl_family.id);
	return 0;
}
static void rl_monitor_netlink_exit(void)
{
	genl_unregister_family(&rl_monitor_genl_family);
}

static inline int genl_msg_prepare_usr_msg(u8 cmd, size_t size, pid_t pid, struct sk_buff **skbp)
{
	struct sk_buff *skb;
	/* create a new netlink msg */
	skb = genlmsg_new(size, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	/* Add a new netlink message to an skb */
	genlmsg_put(skb, pid, 0, &rl_monitor_genl_family, 0, cmd);
	*skbp = skb;
	return 0;
}
static inline int genl_msg_mk_usr_msg(struct sk_buff *skb, int type, void *data, int len)
{
	int ret;
	/* add a netlink attribute to a socket buffer */
	ret = nla_put(skb, type, len, data);
	if (ret != 0)
		return ret;

	return 0;
}
static int rl_monitor_send_netlink_msg(int msg_type, char *payload, int payload_len)
{
	int ret = 0;
	struct sk_buff *skbuff;
	void *head;
	size_t size;

	if (!rl_monitor_netlink_pid) {
		pr_info("%s rl_monitor_netlink_pid=0\n", __func__);
		return -1;
	}

	size = nla_total_size(payload_len);
	ret = genl_msg_prepare_usr_msg(RL_MONITOR_CMD_UPLINK, size, rl_monitor_netlink_pid, &skbuff);
	if (ret)
		return ret;

	ret = genl_msg_mk_usr_msg(skbuff, msg_type, payload, payload_len);
	if (ret) {
		kfree_skb(skbuff);
		return ret;
	}

	head = genlmsg_data(nlmsg_data(nlmsg_hdr(skbuff)));
	genlmsg_end(skbuff, head);

	/* send data */
	ret = genlmsg_unicast(&init_net, skbuff, rl_monitor_netlink_pid);
	if (ret < 0) {
		if (net_ratelimit())
			pr_info("%s error, ret = %d", __func__, ret);
		return -1;
	}
	return 0;
}

static int osml_touchscreen_store(const char *buf, const struct kernel_param *kp)
{
	int val;

	if (sscanf(buf, "%d\n", &val) < 0)
		return -EINVAL;

	val = !!val;

	if (val) {
		atomic_set(&input_trigger, 1);
		atomic_set(&input_start, 1);
	} else
		atomic_set(&input_start, 0);

	return 0;
}

static const struct kernel_param_ops osml_touchscreen_ops = {
	.set = osml_touchscreen_store,
};
module_param_cb(touchscreen, &osml_touchscreen_ops, NULL, 0664);

static int osml_report_proc_show(struct seq_file *m, void *v)
{
	unsigned int local_report_cnt = 0;
	int row, col, idx = 0;
	struct event_data *event;

	if (!pmonitor.buf) {
		seq_puts(m, "sample buffer not init\n");
		return 0;
	}

	local_report_cnt = min(record_cnt, (unsigned int) MAX_REPORT_SIZE);

	if (!local_report_cnt) {
		seq_puts(m, "no data recorded\n");
		return 0;
	}

	mutex_lock(&list_mutex_lock);
	list_for_each_entry(event, &list_event_head[OSML_EVENT], osml_event_node) {
		seq_printf(m, "%s,", event->title);
	}
	mutex_unlock(&list_mutex_lock);
	seq_puts(m, "\n");

	for (row = 0; row < local_report_cnt; row++) {
		idx = row * pmonitor.event_size;
		for (col = 0; col < pmonitor.event_size; col++)
			seq_printf(m, "%lld,", pmonitor.buf[idx + col]);
		seq_puts(m, "\n");
	}

	return 0;
}

static int osml_report_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, osml_report_proc_show, NULL);
}

static const struct proc_ops osml_report_proc_fops = {
	.proc_open = osml_report_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int osml_reset_store(const char *buf, const struct kernel_param *kp)
{
	int val;

	if (sscanf(buf, "%d\n", &val) < 0)
		return -EINVAL;

	if (val != 1)
		return 0;

	if (pmonitor.buf)
		memset(pmonitor.buf, 0, sizeof(long long) * pmonitor.event_size * MAX_REPORT_SIZE);

	record_cnt = 0;
	atomic_set(&frame_number, -1);
	fg_pid = 0;

	pr_info("sample data reset\n");
	return 0;
}

static const struct kernel_param_ops osml_reset_ops = {
	.set = osml_reset_store,
};
module_param_cb(reset, &osml_reset_ops, NULL, 0664);

static int osml_sample_rate_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int val;

	if ((sscanf(buf, "%u\n", &val) <= 0) || (val > 1670)) {
		pr_err("error setting argument. argument should be positive and <= 1670\n");
		return -EINVAL;
	}

	osml_sample_rate = val;
	return 0;
}

static inline int osml_sample_rate_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", osml_sample_rate);
}

static const struct kernel_param_ops osml_sample_rate_ops = {
	.set = osml_sample_rate_store,
	.get = osml_sample_rate_show,
};
module_param_cb(sample_rate_ms, &osml_sample_rate_ops, NULL, 0664);

static int get_thread_count(int ioc_pid, int *event_uid)
{
	struct task_struct *task, *p, *t;
	u64 pid, count = 0;

	pid = osml_pid ? osml_pid : fg_pid;
	pid = (ioc_pid == -1) ? pid : ioc_pid;
	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (!task) {
		rcu_read_unlock();
		pr_err("%s find_task_by_vpid failed.", __func__);
		return 1;
	}
	*event_uid = __task_cred((task))->uid.val;

	for_each_process_thread(p, t) {
		if (__task_cred((t))->uid.val == *event_uid)
			count++;
	}
	rcu_read_unlock();

	return count;
}

static void init_perf_event(int event_config, int event_type, struct event_data *eventd, bool dimension)
{
	struct perf_event_attr attr;
	struct perf_event *event;
	struct task_struct *p, *t;
	int i = 0;

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.config = event_config;
	attr.type = event_type;
	attr.size = sizeof(struct perf_event_attr);
	attr.inherit = 1;

	if (!dimension) {
		for (i = 0; i < eventd->pdata_cnt; i++) {
			event = perf_event_create_kernel_counter(&attr, i, NULL, NULL, NULL);
			if (IS_ERR(event)) {
				(eventd->pdata + i)->pevent = NULL;
				pr_err("osml event create failed. error no: %ld, event_config: %d, event_type: %d", PTR_ERR(event), event_config, event_type);
				break;
			}
			perf_event_enable(event);
			(eventd->pdata + i)->pevent = event;
		}
	} else {
		rcu_read_lock();
		for_each_process_thread(p, t) {
			if (__task_cred((t))->uid.val == eventd->uid && eventd->pdata_cnt > i) {
				get_task_struct(t);
				rcu_read_unlock();

				event = perf_event_create_kernel_counter(&attr, -1, t, NULL, NULL);
				if (IS_ERR(event)) {
					(eventd->pdata + i)->pevent = NULL;
					pr_err("osml per task event create failed. error no: %ld, event_config: %d, event_type: %d", PTR_ERR(event), event_config, event_type);
				} else {
					perf_event_enable(event);
					(eventd->pdata + i)->pevent = event;
				}
				i++;

				rcu_read_lock();
				put_task_struct(t);
			}
		}
		rcu_read_unlock();
	}
}

static int init_sample_events(void)
{
	int index, pcount = 0, offset = 0, total_pevent = 0;
	int i;
	bool monitor_dimension = pevent_dimension;
	bool pevent_enable = 0;
	struct event_data *event;
	/*CPU_INST ... CPU_LLC_MISS_RD*/
	int event_id[] = {0x8, 0x11, 0x36, 0x37};

	pr_info("%s initialize", __func__);

	for (index = 0; index < pmonitor.event_size; index++) {
		int sample_event = sample_events[index - offset];

		if ((sample_event >= MONITOR_SIZE || sample_event < 0)) {
			pr_err("event:%d isn't exist!, option is 0 ~ %d\n", sample_event, MONITOR_SIZE - 1);
			return 1;
		}

		if (sample_event == CUSTOM_PEVENT && pcount != pevent_cnt)
			pevent_enable = 1;

		event = kzalloc(sizeof(struct event_data), GFP_KERNEL);
		if (!event)
			continue;

		event->buf_idx = index;
		event->event_idx = pevent_enable ? CUSTOM_PEVENT : sample_event;
		event->title = pevent_enable ? pevent_name[pcount] : monitor_case[event->event_idx];

		switch (event->event_idx) {
		case THERMAL:
			for (i = 0; i < SHELL_MAX; i++) {
				thermal[i] = thermal_zone_get_zone_by_name(thermal_name[i]);
				if (IS_ERR(thermal[i]))
					pr_err("Thermal can't get %s", thermal_name[i]);
			}
			break;
		case POWER_VOLTAGE ... POWER_CURRENT:
			if (!psy)
				psy = power_supply_get_by_name("battery");
			break;
		case CPU_INST ... CPU_LLC_MISS_RD:
		case CUSTOM_PEVENT:
			if (total_pevent < MAX_PEVENT_SIZE) {
				if (event->event_idx == CUSTOM_PEVENT)
					event->pdata_cnt = !monitor_dimension ? nr_cores : get_thread_count(-1, &(event->uid));
				else
					event->pdata_cnt = nr_cores;
				event->pdata = kzalloc(sizeof(struct perf_data) * event->pdata_cnt, GFP_KERNEL);
				if (!event->pdata) {
					pr_err("%s event->pdata kzalloc failed.", __func__);
					kfree(event);
					continue;
				}
				if (event->event_idx == CUSTOM_PEVENT) {
					init_perf_event(pevent_id[pcount], pevent_type[pcount], event, monitor_dimension);
					pcount++;
					if (pcount == pevent_cnt)
						pevent_enable = 0;
					else
						offset++;
				} else
					init_perf_event(event_id[event->event_idx - CPU_INST], PERF_TYPE_RAW, event, 0);
				total_pevent++;
			} else
				pr_warn("4 perf-events are the limit.");
			break;
		default:
			break;
		}
		INIT_LIST_HEAD(&event->osml_event_node);
		mutex_lock(&list_mutex_lock);
		list_add_tail(&(event->osml_event_node), &list_event_head[OSML_EVENT]);
		mutex_unlock(&list_mutex_lock);
		if (osml_debug)
			pr_info("sample_event %p %d %p", &list_event_head[OSML_EVENT], event->event_idx, &event->osml_event_node);
	}
	return 0;
}

static void release_event(int list_type)
{
	int event_idx = 0;
	struct event_data *event, *next;

	mutex_lock(&list_mutex_lock);
	list_for_each_entry_safe(event, next, &list_event_head[list_type], osml_event_node) {
		if (osml_debug)
			pr_info("%s %p %d %p\n", __func__, &list_event_head[list_type], event->event_idx, &event->osml_event_node);

		if (event->pdata) {
			for (event_idx = 0; event_idx < event->pdata_cnt; event_idx++) {
				if (!(event->pdata + event_idx)->pevent)
					continue;
				perf_event_disable((event->pdata + event_idx)->pevent);
				perf_event_release_kernel((event->pdata + event_idx)->pevent);
				(event->pdata + event_idx)->pevent = NULL;
			}
			kfree(event->pdata);
		}
		list_del(&event->osml_event_node);
		kfree(event);
	}
	mutex_unlock(&list_mutex_lock);
	pr_info("%s sample event released.", __func__);
}

static DEFINE_MUTEX(enable_mutex_lock);
static int osml_enable_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int val, i;

	if (sscanf(buf, "%u\n", &val) < 0) {
		pr_err("error setting argument. argument should be 1 or 0\n");
		return -EINVAL;
	}

	if (osml_enable == !!val)
		return 0;

	mutex_lock(&enable_mutex_lock);
	osml_enable = !!val;
	if (osml_enable) {
		monitor_type = sample_type;
		pmonitor.event_size = 0;

		for (i = 0; i < sample_events_cnt; i++) {
			if (sample_events[i] == CUSTOM_PEVENT) {
				pmonitor.event_size = pevent_cnt - 1;
				break;
			}
		}

		pmonitor.event_size += sample_events_cnt;
		if (!pmonitor.buf) {
			pmonitor.buf = (long long *)vzalloc(sizeof(long long) * pmonitor.event_size * MAX_REPORT_SIZE);
			if (!pmonitor.buf)
				goto err_buf;
		}

		if (init_sample_events())
			goto osml_disable;

		if (monitor_type) {
			if (IS_ERR(osml_polling_tsk))
				goto osml_disable;
			wake_up_process(osml_polling_tsk);
		}

		pr_info("osml monitor enable.");
	} else
		goto osml_disable;

	mutex_unlock(&enable_mutex_lock);
	return 0;

osml_disable:
	release_event(OSML_EVENT);
err_buf:
	vfree(pmonitor.buf);
	pmonitor.buf = NULL;
	osml_enable = 0;
	record_cnt = 0;
	fg_pid = 0;
	atomic_set(&frame_number, -1);
	atomic_set(&frame_owner, -1);
	atomic_set(&frame_duration, -1);
	atomic_set(&vsync_period, -1);

	if (psy) {
		power_supply_put(psy);
		psy = NULL;
	}

	pr_info("osml monitor disable.\n");

	mutex_unlock(&enable_mutex_lock);
	return 0;
}

static inline int osml_enable_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", osml_enable);
}

static const struct kernel_param_ops osml_enable_ops = {
	.set = osml_enable_store,
	.get = osml_enable_show,
};
module_param_cb(enable, &osml_enable_ops, NULL, 0664);

static int osml_pid_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) < 0) {
		pr_err("error setting argument. argument should be positive\n");
		return -EINVAL;
	}

	if (osml_enable) {
		pr_warn("need to disable osml before assign pid!\n");
		return 0;
	}

	osml_pid = val;
	pr_info("osml assign osml_pid %d.", osml_pid);
	return 0;
}

static inline int osml_pid_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", osml_pid);
}

static const struct kernel_param_ops osml_pid_ops = {
	.set = osml_pid_store,
	.get = osml_pid_show,
};
module_param_cb(pid, &osml_pid_ops, NULL, 0664);


static inline u64 read_pevent(struct perf_data *pdatas, int pdata_cnt, int event_idx, int frame_cnt)
{
	u64 total, enabled, running, sum = 0;
	struct perf_data *pdata;
	int i;

	if (event_idx == CUSTOM_PEVENT && (frame_cnt % pevent_sample_rate))
		return -1;

	for (i = 0; i < pdata_cnt; i++) {
		pdata = pdatas + i;
		if (!pdata->pevent)
			continue;

		total = perf_event_read_value(pdata->pevent, &enabled,
				&running);
		if (!pdata->prev_count)
			pdata->last_delta = 0;
		else
			pdata->last_delta = total - pdata->prev_count;

		pdata->prev_count = total;
		sum += pdata->last_delta;
	}

	return sum;
}


#if defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST) || defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST_MODULE)
static int osml_task_cpu(int idx)
{
	struct task_struct *p, *t;
	int pid, count = 0;
	u64 time = 0;
	int ret = -1;

	pid = osml_pid ? osml_pid : fg_pid;
	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (!p) {
		rcu_read_unlock();
		pr_err("%s did not find task", __func__);
		return ret;
	}
	get_task_struct(p);

	if (idx == MAINTHREAD_CPU)
		ret = task_cpu(p);
	else {
		for_each_thread(p, t) {
			if (!strncmp(t->comm, "RenderThread", 12)) {
				struct oplus_task_struct *ots = (struct oplus_task_struct *) t->android_oem_data1;

				if (time <= ots->enqueue_time) {
					time = ots->enqueue_time;
					ret = task_cpu(t);
				}
				if (osml_debug) {
					pr_info("count %d pid %d, tgid %d, ktime %llu, Thread enqueue time %llu",
							count, t->pid, t->tgid, ktime_to_ns(ktime_get()), ots->enqueue_time);
					count++;
				}
			}
		}
	}
	rcu_read_unlock();
	put_task_struct(p);

	return ret;
}
#endif

/* keep this light way and not get to sleep
 * 1. called from walt irq work
 * 2. called with spin lock held
 * TODO maybe this part can off load to kworker
 */
static int osml_update_cpu_load(int idx)
{
	static u64 prev_busy_time[OSML_MAX_CLUSTER] = {0, 0, 0, 0};
	static u64 prev_wall_time[OSML_MAX_CLUSTER] = {0, 0, 0, 0};
	u64 busy_time = 0, wall_time, load, duration;
	u64 sload = 0;
	int cpu;

	if (cpu_clus[idx].clus_id == -1)
		return -1;

	for_each_online_cpu(cpu) {
		if (cpu_clus[idx].clus_id <= cpu &&
				(cpu_clus[idx].clus_id + cpu_clus[idx].num_cpu) > cpu) {
			busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
			busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
			busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
			busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
			busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
			busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];
		}
	}

	wall_time = jiffies64_to_nsecs(get_jiffies_64());

	load = busy_time - prev_busy_time[idx];
	duration = wall_time - prev_wall_time[idx];
	prev_busy_time[idx] = busy_time;
	prev_wall_time[idx] = wall_time;

	if (duration == 0) {
		if (osml_debug)
			pr_err("osml sload: %lld %lld\n", load, duration);
		return -1;
	}

	sload = load * 100 / duration;

	if (osml_debug)
		pr_info("osml idx %d, sload: %lld, duration: %lld, busy_time %lld",
				idx, sload, duration, load);

	return sload;
}

static int update_task_load(void)
{
	static u64 prev_busy_time;
	static u64 prev_wall_time;
	struct task_struct *p;
	u64 utime, stime, duration, busy_time, wall_time;
	int pid;

	prev_busy_time = 0;
	prev_wall_time = 0;
	pid = osml_pid ? osml_pid : fg_pid;
	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (!p) {
		rcu_read_unlock();
		pr_err("%s did not find task", __func__);
		return -1;
	}
	get_task_struct(p);
	rcu_read_unlock();
	thread_group_cputime_adjusted(p, &utime, &stime);
	put_task_struct(p);

	wall_time = ktime_to_ns(ktime_get());
	busy_time = (utime + stime) - prev_busy_time;
	duration = wall_time - prev_wall_time;
	prev_busy_time = utime + stime;
	prev_wall_time = wall_time;

	if (osml_debug)
		pr_info("%s busy_time %llu, duration %llu", __func__,
				busy_time, duration);

	if (duration == 0)
		return -1;

	return busy_time * 100 / duration;
}


static void collect_info(void)
{
#define RL_MSG_SIZE (38 * sizeof(long long))
	int ll_size = sizeof(long long);
	char send_msg[RL_MSG_SIZE] = {0};
	int i;

	u64 row, idx, select_idx;
	struct cpufreq_policy *policy;
	static long long last_ts_us;
	union power_supply_propval val;
	struct event_data *event;
	int ret;
	static unsigned int frame_cnt;
	ktime_t ktime = ktime_get();

	frame_cnt = 0;
	if (record_cnt == 0)
		frame_cnt = 0;

	mutex_lock(&enable_mutex_lock);

	if (!pmonitor.buf) {
		mutex_unlock(&enable_mutex_lock);
		return;
	}

	row = (record_cnt % (unsigned int) MAX_REPORT_SIZE) * pmonitor.event_size;
	mutex_lock(&list_mutex_lock);
	list_for_each_entry(event, &list_event_head[OSML_EVENT], osml_event_node) {
		idx = row + event->buf_idx;
		if (osml_debug)
			pr_info("%s event_idx %d, uid %d, pdata_cnt %d", event->title, event->event_idx, event->uid, event->pdata_cnt);

		switch (event->event_idx) {
		case TS:
			pmonitor.buf[idx] = ktime_to_ms(ktime);
			break;
		case FRAME_DURATION:
			pmonitor.buf[idx] = atomic_read(&frame_duration);
			break;
		case FRAME_ALLOW_DURATION:
			pmonitor.buf[idx] = atomic_read(&vsync_period) - DEADLINE_MS;
			break;
		case MAINTHREAD_CPU ... RENDERTHREAD_CPU:
			pmonitor.buf[idx] = -1;
#if defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST) || defined(CONFIG_OPLUS_FEATURE_SCHED_ASSIST_MODULE)
			pmonitor.buf[idx] = osml_task_cpu(event->event_idx);
#endif
			break;
		case TASK_LOAD:
			pmonitor.buf[idx] = update_task_load();
			break;
		case CPU_LOAD_CLUSTER_0 ... CPU_LOAD_CLUSTER_1:
#if (OSML_CLUSTER > 2)
		case CPU_LOAD_CLUSTER_2:
#if (OSML_CLUSTER > 3)
		case CPU_LOAD_CLUSTER_3:
#endif
#endif
			pmonitor.buf[idx] = osml_update_cpu_load(event->event_idx - CPU_LOAD_CLUSTER_0);
			break;
		case CPU_FREQ_CLUSTER_0 ... CPU_FREQ_CLUSTER_1:
#if (OSML_CLUSTER > 2)
		case CPU_FREQ_CLUSTER_2:
#if (OSML_CLUSTER > 3)
		case CPU_FREQ_CLUSTER_3:
#endif
#endif
			select_idx = event->event_idx - CPU_FREQ_CLUSTER_0;
			if (cpu_clus[select_idx].clus_id == -1)
				break;
			policy = cpufreq_cpu_get_raw(cpu_clus[select_idx].clus_id);
			if (policy)
				pmonitor.buf[idx] = policy->cur;
			break;
#if defined(CONFIG_MTK_PLATFORM)
		case DDR_FREQ:
			pmonitor.buf[idx] = (long long)mtk_dramc_get_data_rate();
			break;
		case GPU_FREQ:
			pmonitor.buf[idx] = (long long)mt_gpufreq_get_cur_freq();
			break;
#else
		case DDR_FREQ:
			pmonitor.buf[idx] = 0;
			break;
		case GPU_FREQ:
			pmonitor.buf[idx] = 125000000;
			break;
#endif
		case THERMAL:
			pmonitor.buf[idx] = -1;
			for (select_idx = 0; select_idx < SHELL_MAX; select_idx++) {
				if (IS_ERR(thermal[select_idx]))
					continue;
				thermal_zone_get_temp(thermal[select_idx], &ret);
				if (ret > pmonitor.buf[idx])
					pmonitor.buf[idx] = ret / 100;
			}
			break;
		case TOUCHSCREEN:
			pmonitor.buf[idx] = atomic_read(&input_trigger);
			if (!atomic_read(&input_start))
				atomic_set(&input_trigger, 0);
			break;
		case POWER_VOLTAGE ... POWER_CURRENT:
			if (!psy)
				continue;
			select_idx = (event->event_idx - POWER_VOLTAGE) ? POWER_SUPPLY_PROP_CURRENT_NOW : POWER_SUPPLY_PROP_VOLTAGE_NOW;
			ret = power_supply_get_property(psy, select_idx, &val);
			if (ret) {
				pr_err("power_supply_get_propperty %s failed, error no: %d\n", event->title, ret);
				break;
			}
			pmonitor.buf[idx] = val.intval;
			break;
		case CPU_SCALING_MAX_FREQ_0 ... CPU_SCALING_MIN_FREQ_1:
#if (OSML_CLUSTER > 2)
		case CPU_SCALING_MAX_FREQ_2 ... CPU_SCALING_MIN_FREQ_2:
#if (OSML_CLUSTER > 3)
		case CPU_SCALING_MAX_FREQ_3 ... CPU_SCALING_MIN_FREQ_3:
#endif
#endif
			select_idx = (event->event_idx - CPU_SCALING_MAX_FREQ_0) / 2;
			if (cpu_clus[select_idx].clus_id == -1)
				break;
			policy = cpufreq_cpu_get_raw(cpu_clus[select_idx].clus_id);
			if (policy) {
				select_idx = (event->event_idx - CPU_SCALING_MAX_FREQ_0) % 2;
				if (select_idx)
					pmonitor.buf[idx] = policy->min;
				else
					pmonitor.buf[idx] = policy->max;
			}
			break;
#if !defined(CONFIG_MTK_PLATFORM)
		case GPU_MAX_CLOCK:
			pmonitor.buf[idx] = 900000000;
			break;
		case GPU_MIN_CLOCK:
			pmonitor.buf[idx] = 125000000;
			break;
#endif
		case CPU_INST ... CPU_LLC_MISS_RD:
			if (event->pdata)
				pmonitor.buf[idx] = (read_pevent(event->pdata, event->pdata_cnt, event->event_idx, frame_cnt))/frame_cnt_interval;
			break;
		case CUSTOM_PEVENT:
			if (event->pdata)
				pmonitor.buf[idx] = read_pevent(event->pdata, event->pdata_cnt, event->event_idx, frame_cnt);
			break;
		}
	}
	mutex_unlock(&list_mutex_lock);
	last_ts_us = ktime_to_us(ktime);
	record_cnt++;
	if (!monitor_type)
		frame_cnt++;

	memset(send_msg, -1, RL_MSG_SIZE);
	for (i = 0; i < pmonitor.event_size; i++)
		memcpy(send_msg + (i * ll_size), &(pmonitor.buf[row + i]), ll_size);
	rl_monitor_send_netlink_msg(RL_MONITOR_MSG_REPORT_INFO, (char *) send_msg, sizeof(send_msg));
	if (osml_debug)
		pr_info("Rlsche time osml send: %lld - %lld\n", ktime_to_ms(ktime_get()), ktime_to_ms(ktime));

	mutex_unlock(&enable_mutex_lock);
}

static void osml_trigger_fn(struct work_struct *work)
{
	if (osml_enable)
		collect_info();
}
static DECLARE_WORK(osml_work, osml_trigger_fn);

static int osml_frame_store(const char *buf, const struct kernel_param *kp)
{
	unsigned int pid_in, duration_in, expect_duration_in;

	if (!osml_enable)
		return 0;

	if (sscanf(buf, "%u,%u,%u\n", &pid_in, &duration_in, &expect_duration_in) < 0) {
		pr_err("error setting argument. argument should be positive\n");
		return -EINVAL;
	}

	if (osml_debug)
		pr_info("osml_frame pid: %d == %d\n", pid_in, osml_pid);

	if (pid_in == osml_pid)
		fg_pid = pid_in;

	if (monitor_type)
		return 0;

	if (osml_debug)
		pr_info("osml_frame %s\n", buf);

	if (osml_workq) {
		if (fg_pid != pid_in)
			return 0;

		atomic_inc(&frame_number);
		atomic_set(&frame_owner, pid_in);
		atomic_set(&frame_duration, duration_in);
		atomic_set(&vsync_period, expect_duration_in);
		if (atomic_read(&frame_number)%frame_cnt_interval == 0) {
			if (osml_debug)
				pr_info("osml debug frame : %d(%d) - %lld/n", atomic_read(&frame_number), frame_cnt_interval, ktime_to_ms(ktime_get()));
			queue_work(osml_workq, &osml_work);
		}
	}

	return 0;
}

static int osml_frame_show(char *buf, const struct kernel_param *kp)
{
	return snprintf(buf, PAGE_SIZE, "%u,%u,%u\n", atomic_read(&frame_owner), atomic_read(&frame_duration), atomic_read(&vsync_period));
}

static const struct kernel_param_ops osml_frame_ops = {
	.set = osml_frame_store,
	.get = osml_frame_show,
};
module_param_cb(frame_duration, &osml_frame_ops, NULL, 0664);

static int osml_cluster_info_show(char *buf, const struct kernel_param *kp)
{
	char *start = buf;
	u32 cpu;
	u32 cluster_id;
	for_each_possible_cpu(cpu) {
		cluster_id = topology_cluster_id(cpu);
		buf += sprintf(buf, "%d,", cluster_id);
	}
	return buf - start;
}

static struct kernel_param_ops osml_cluster_info_ops = {
	.get = osml_cluster_info_show,
};
module_param_cb(cluster_info, &osml_cluster_info_ops, NULL, 0664);

static int osml_polling_fn(void *p)
{
	while (1) {
		if (!osml_enable) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule();
			set_current_state(TASK_RUNNING);
		} else
			msleep(osml_sample_rate);

		if (osml_enable)
			collect_info();

		if (kthread_should_stop())
			break;
	}
	return 0;
}

static void prepare_cpuinfo(void)
{
	unsigned int cpu, tmp_idx, clus_idx = 0;

	for_each_possible_cpu(cpu) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
		tmp_idx = topology_cluster_id(cpu);
#else
		tmp_idx = topology_physical_package_id(cpu);
#endif
		if (cpu == 0 || clus_idx != tmp_idx) {
			clus_idx = tmp_idx;
			cpu_clus[clus_idx].clus_id = cpu;
			cpu_clus[clus_idx].num_cpu = 1;
		} else {
			cpu_clus[clus_idx].num_cpu++;
		}
	}
}

static int __init osml_init(void)
{
	int i;

	osml_enable = 0;
	osml_pid = 0;
	record_cnt = 0;
	fg_pid = 0;
	osml_debug = 0;
	pevent_dimension = 0;
	rl_monitor_netlink_pid = 0;
	pr_info("osml init");

	nr_cores = num_possible_cpus();

	prepare_cpuinfo();


	for (i = 0; i < MONITOR_SIZE; i++)
		sample_events[i] = i;

	proc_create("osml_report", S_IFREG | 0444, NULL, &osml_report_proc_fops);

	osml_polling_tsk = kthread_run(osml_polling_fn, 0, "osml_monitor");
	if (IS_ERR(osml_polling_tsk))
		pr_err("Failed to start osml_polling_task");

	osml_workq = alloc_ordered_workqueue("osml_wq", WQ_HIGHPRI);
	if (!osml_workq)
		pr_err("alloc work queue fail");

	if (rl_monitor_netlink_init() < 0)
		pr_err("%s oplus_apps_monitor_init module failed to init netlink.\n", __func__);
	else if (osml_debug)
		pr_info("%s oplus_apps_monitor_init module init netlink successfully.\n",  __func__);

	return 0;
}

static void __exit osml_exit(void)
{
	struct workqueue_struct *tmp;
	int i;

	if (osml_polling_tsk)
		kthread_stop(osml_polling_tsk);
	if (osml_workq) {
		tmp = osml_workq;
		osml_workq = NULL;
		destroy_workqueue(tmp);
	}

	if (osml_enable) {
		osml_enable = 0;
		release_event(OSML_EVENT);
		vfree(pmonitor.buf);
		pmonitor.buf = NULL;
		if (psy) {
			power_supply_put(psy);
			psy = NULL;
		}
	}

	for (i = 0; i < OSML_MAX_CLUSTER; i++) {
		kfree(cpu_clus[i].pwr_tbl);
		cpu_clus[i].pwr_tbl = NULL;
	}

	rl_monitor_netlink_exit();

	pr_info("%s module exit\n", __func__);
}

module_init(osml_init);
module_exit(osml_exit);
MODULE_DESCRIPTION("OSML monitor");
MODULE_LICENSE("GPL v2");

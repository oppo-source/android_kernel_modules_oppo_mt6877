// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#include "linux/printk.h"
#include <linux/cgroup.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>
#include <trace/hooks/vendor_hooks.h>
#include <trace/hooks/sched.h>
#include "qos_sched_cgrp.h"
#include "qos_sched.h"


static inline struct task_group *css_tg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct task_group, css) : NULL;
}

int qs_get_cgrp_qos_level(struct task_struct *task)
{
	struct cgroup_subsys_state *css;
	struct task_group *tg;
	struct oplus_task_group *otg;
	int level = -1;

	rcu_read_lock();
	css = task_css(task, cpu_cgrp_id);
	tg = css_tg(css);
	otg = (struct oplus_task_group *)tg->android_kabi_reserved4;
	if (!otg) {
		rcu_read_unlock();
		return -1;
	}
	level = otg->qos_level;
	rcu_read_unlock();

	return level;
}

static s64 qos_level_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct task_group *tg = css_tg(css);
	struct oplus_task_group *otg =
		(struct oplus_task_group *)tg->android_kabi_reserved4;

	if (!otg)
		return -ENOENT;
	return otg->qos_level;
}

static int qos_level_write(struct cgroup_subsys_state *css, struct cftype *cft,
			   s64 level)
{
	struct task_group *tg = css_tg(css);
	struct oplus_task_group *otg =
		(struct oplus_task_group *)tg->android_kabi_reserved4;

	if (!otg)
		return -ENOENT;
	otg->qos_level = level;

	return 0;
}

static s64 qos_latency_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct task_group *tg = css_tg(css);
	struct oplus_task_group *otg =
		(struct oplus_task_group *)tg->android_kabi_reserved4;

	if (!otg)
		return -ENOENT;
	return otg->qos_latency;
}

static int qos_latency_write(struct cgroup_subsys_state *css, struct cftype *cft,
				s64 latency)
{
	struct task_group *tg = css_tg(css);
	struct oplus_task_group *otg =
		(struct oplus_task_group *)tg->android_kabi_reserved4;

	if (!otg)
		return -ENOENT;
	otg->qos_latency = latency;

	return 0;
}

static struct cftype qos_ctrl_legacy_files[] = {
	{
		.name = "qos_level",
		.read_s64 = qos_level_read,
		.write_s64 = qos_level_write,
	},
	{
		.name = "qos_latency",
		.read_s64 = qos_latency_read,
		.write_s64 = qos_latency_write,
	},
	{}, /* terminate */
};

void android_vh_cpu_cgroup_css_alloc_handler(void *unused, struct task_group *tg, struct cgroup_subsys_state *parent_css)
{
	struct oplus_task_group *otg;

	otg = kzalloc(sizeof(*otg), GFP_ATOMIC);
	if (!otg)
		return;

	otg->qos_level = -1;
	tg->android_kabi_reserved4 = (u64)otg;
}

void android_vh_android_vh_cpu_cgroup_css_free_handler(void *unused, struct cgroup_subsys_state *css)
{
	struct task_group *tg = css_tg(css);
	struct oplus_task_group *otg;

	otg = (struct oplus_task_group *)tg->android_kabi_reserved4;
	if (!otg)
		return;

	/* Release per CPUs boost group support */
	kfree(otg);
}

static void qos_sched_pre_init(void)
{
	struct cgroup_subsys_state *root_css = &root_task_group.css, *css;

	rcu_read_lock();
	css_for_each_child(css, root_css)
		android_vh_cpu_cgroup_css_alloc_handler(NULL, css_tg(css),
							css->parent);
	rcu_read_unlock();
	pr_info("%s successfully.\n", __func__);
}

static int qos_sched_register_vendor_hooks(void)
{
	int ret = 0;

	/* register vendor hook in kernel/sched/core.c*/
	ret |= register_trace_android_vh_cpu_cgroup_css_alloc(
		android_vh_cpu_cgroup_css_alloc_handler, NULL);
	ret |= register_trace_android_vh_cpu_cgroup_css_free(
		android_vh_android_vh_cpu_cgroup_css_free_handler, NULL);
	if (ret) {
		pr_err("%s failed!\n", __func__);
		return ret;
	}

	return 0;
}

static int qos_sched_unregister_vendor_hooks(void)
{
	int ret = 0;

	/* register vendor hook in kernel/sched/core.c*/
	ret |= unregister_trace_android_vh_cpu_cgroup_css_alloc(
			android_vh_cpu_cgroup_css_alloc_handler, NULL);
	ret |= unregister_trace_android_vh_cpu_cgroup_css_free(
		android_vh_android_vh_cpu_cgroup_css_free_handler, NULL);
	if (ret) {
		pr_err("%s failed!\n", __func__);
		return ret;
	}

	return 0;
}

int qos_sched_init_cgroup(void)
{
	int ret;

	qos_sched_pre_init();

	ret = qos_sched_register_vendor_hooks();
	if (ret)
		goto out;

	ret = cgroup_add_legacy_cftypes(&cpu_cgrp_subsys,
				qos_ctrl_legacy_files);

out:
	return ret;
}

int qos_sched_deinit_cgroup(void)
{
	int ret;

	ret = qos_sched_unregister_vendor_hooks();
	if (ret)
		goto out;

	ret = cgroup_rm_cftypes(qos_ctrl_legacy_files);

out:
	return ret;
}

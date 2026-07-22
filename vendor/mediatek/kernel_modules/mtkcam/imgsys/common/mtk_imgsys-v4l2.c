// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 *
 * Author: Frederic Chen <frederic.chen@mediatek.com>
 *
 */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of_device.h>

#ifndef OPLUS_FEATURE_CAMERA_COMMON
#define OPLUS_FEATURE_CAMERA_COMMON
#endif

#ifdef OPLUS_FEATURE_CAMERA_COMMON
#include <linux/kthread.h>
#include <linux/err.h>

struct imgsys_daemon_info
{
	struct task_struct *daemon_task;
	wait_queue_head_t wq;
	atomic_t is_thread_run;
};
#endif

int imgsys_dbg_en;
module_param(imgsys_dbg_en, int, 0644);

int imgsys_slc_dbg_en;
module_param(imgsys_slc_dbg_en, int, 0644);

bool imgsys_dbg_enable(void)
{
	return imgsys_dbg_en;
}
EXPORT_SYMBOL(imgsys_dbg_enable);

bool imgsys_slc_dbg_enable(void)
{
	return imgsys_slc_dbg_en;
}
EXPORT_SYMBOL(imgsys_slc_dbg_enable);

#ifdef OPLUS_FEATURE_CAMERA_COMMON
int imgsys_daemon_create(struct imgsys_daemon_info *info, int (*threadfn)(void *data), const char name[], bool is_fifo)
{
	pr_info("%s: E\n", __func__);
	if (info->daemon_task == NULL)
	{
		info->daemon_task = kthread_create(threadfn, NULL, name);
		if (IS_ERR(info->daemon_task)) {
			pr_info("%s: kthread_run %s failed\n", __func__, name);
			return PTR_ERR(info->daemon_task);
		}
		init_waitqueue_head(&info->wq);
		atomic_set(&info->is_thread_run, 0);

		if (is_fifo)
		{
			sched_set_fifo_low(info->daemon_task);
		}
		else
		{
			sched_set_normal(info->daemon_task, -20);
		}
		pr_info("%s: %s wake up ", __func__, name);
		wake_up_process(info->daemon_task);
	}
	return 0;
}
EXPORT_SYMBOL(imgsys_daemon_create);
#endif
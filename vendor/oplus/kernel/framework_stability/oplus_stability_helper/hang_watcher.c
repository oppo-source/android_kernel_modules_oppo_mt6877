// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025-2030 Oplus. All rights reserved.
 */

#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/cred.h>
#include <linux/panic.h>
#include <linux/rcupdate.h>
#include <linux/types.h>
#include <linux/debug_locks.h>
#include <linux/nmi.h>
#include <linux/time.h>
#include <linux/delay.h>
#include "hang_watcher.h"

#define CREATE_TRACE_POINTS
#include "hang_watcher_trace.h"

#define SYSTEM_SERVER_COMM				"system_server"
#define WATCHDOG_COMM					"watchdog"
#define EVENT_ID 					"SystemServerHang"
#define LOG_TAG 					"30413003"
#define APP_ID						30413
#define CHECK_HANG_DELAY_MS				60000
#define SYSTEM_UID					1000

static pid_t g_system_server_pid = 0;

static void do_check_system_server_hang(struct work_struct *work);
static DECLARE_DELAYED_WORK(check_hang_work, do_check_system_server_hang);

static long get_timestamp_ms(void)
{
	struct timespec64 now;
	ktime_get_real_ts64(&now);
	return timespec64_to_ns(&now) / NSEC_PER_MSEC;
}

static bool is_system_server(struct task_struct *t)
{
	if (!t) {
		return false;
	} else if ((task_uid(t).val == SYSTEM_UID)
			&& !strcmp(t->comm, SYSTEM_SERVER_COMM)) {
		return true;
	}
	return false;
}

static void do_check_system_server_hang(struct work_struct *work)
{
	struct task_struct *p;
	struct task_struct *t;
	long block_time = 0;
	rcu_read_lock();
	p = find_task_by_vpid(g_system_server_pid);
	if (p) {
		/* check found task is still system_server */
		if (is_system_server(p)) {
			/* check state of system_server's every thread */
			for_each_thread(p, t) {
				unsigned int state;
				if (t) {
					state = READ_ONCE(t->__state);
					block_time = (jiffies - t->last_switch_time) / HZ;
					if ((state & TASK_UNINTERRUPTIBLE)
							&& !(state & TASK_WAKEKILL)
							&& !(state & TASK_NOLOAD)) {
						pr_err("system_server task %s:%d blocked for %ld seconds!",
								t->comm, t->pid, block_time);
						/* dump trace */
						sched_show_task(t);
						debug_show_held_locks(t);
						/* report through tracepoint */
						trace_system_server_hang(APP_ID, LOG_TAG,
							EVENT_ID, get_timestamp_ms(), t->comm, block_time);
					}
				}
			}
		}
	}
	rcu_read_unlock();
	if (p) {
		trigger_all_cpu_backtrace();
	}
}

void check_system_server_hang(int sig, struct task_struct *src, struct task_struct *dst)
{
	if (sig == SIGKILL && !strcmp(src->comm, WATCHDOG_COMM)
			&& !strcmp(dst->comm, SYSTEM_SERVER_COMM)) {
		/* system_server watchdog happend */
		g_system_server_pid = dst->tgid;
		schedule_delayed_work(&check_hang_work, msecs_to_jiffies(CHECK_HANG_DELAY_MS));
	}
}

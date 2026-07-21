// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#include <linux/seq_file.h>
#include <../drivers/android/binder_internal.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <trace/hooks/binder.h>
#include <linux/random.h>
#include <linux/of.h>
#include <../kernel/oplus_cpu/sched/sched_assist/sa_common.h>
#include "binder_sched.h"

extern int refs_debug;
extern int refs_debug_client_id;
extern int refs_debug_server_id;
extern int refs_debug_interval;

#define refs_dmesg(debug_mask, fmt, ...) \
	do { \
		if (refs_debug & debug_mask) { \
			pr_info("[lfc refs][curr:%d %d %s] " fmt, current->pid, current->tgid, current->comm, ##__VA_ARGS__); \
		} \
	} while (0)

enum {
	DEBUG_DISABLE = 0,
	DEBUG_DMESG = 1,
	DEBUG_DMESG_ALLREF = 2,
	DEBUG_DMESG_T_INFO = 4,
	DEBUG_DMESG_TO_NODE_REFS = 8,
	DEBUG_CLIENT_SYSTEMSERVER = 16,
	DEBUG_CLIENT_REFERENCEQD = 32,
	DEBUG_PROC_REFS = 64,
	DEBUG_PROC_STATS = 128,
	DEBUG_THREAD_STATS = 256,
	DEBUG_CLIENT_SPECIFIED_PROC = 512,
	DEBUG_CLIENT_SPECIFIED_TID = 1024,
	DEBUG_ONLY_BC_INCDEC_REFS = 2048,
	DEBUG_SYSTEM_SERVER_PROC = 4096,
	DEBUG_SERVER_SPECIFIED_PROC = 8192,
};

static const char * const binder_return_strings[] = {
	"BR_ERROR",
	"BR_OK",
	"BR_TRANSACTION",
	"BR_REPLY",
	"BR_ACQUIRE_RESULT",
	"BR_DEAD_REPLY",
	"BR_TRANSACTION_COMPLETE",
	"BR_INCREFS",
	"BR_ACQUIRE",
	"BR_RELEASE",
	"BR_DECREFS",
	"BR_ATTEMPT_ACQUIRE",
	"BR_NOOP",
	"BR_SPAWN_LOOPER",
	"BR_FINISHED",
	"BR_DEAD_BINDER",
	"BR_CLEAR_DEATH_NOTIFICATION_DONE",
	"BR_FAILED_REPLY",
	"BR_FROZEN_REPLY",
	"BR_ONEWAY_SPAM_SUSPECT",
	"BR_TRANSACTION_PENDING_FROZEN",
};

static const char * const binder_command_strings[] = {
	"BC_TRANSACTION",
	"BC_REPLY",
	"BC_ACQUIRE_RESULT",
	"BC_FREE_BUFFER",
	"BC_INCREFS",
	"BC_ACQUIRE",
	"BC_RELEASE",
	"BC_DECREFS",
	"BC_INCREFS_DONE",
	"BC_ACQUIRE_DONE",
	"BC_ATTEMPT_ACQUIRE",
	"BC_REGISTER_LOOPER",
	"BC_ENTER_LOOPER",
	"BC_EXIT_LOOPER",
	"BC_REQUEST_DEATH_NOTIFICATION",
	"BC_CLEAR_DEATH_NOTIFICATION",
	"BC_DEAD_BINDER_DONE",
	"BC_TRANSACTION_SG",
	"BC_REPLY_SG",
};

#define BC_INCREFS_INDEX 	4
#define BC_DECREFS_INDEX 	7

static const char * const binder_objstat_strings[] = {
	"proc",
	"thread",
	"node",
	"ref",
	"death",
	"transaction",
	"transaction_complete",
};

#define binder_node_lock(node) _binder_node_lock(node, __LINE__)
static void _binder_node_lock(struct binder_node *node, int line)
	__acquires(&node->lock)
{
	spin_lock(&node->lock);
}

#define binder_node_unlock(node) _binder_node_unlock(node, __LINE__)
static void _binder_node_unlock(struct binder_node *node, int line)
			__releases(&node->lock)
{
	spin_unlock(&node->lock);
}

static void print_binder_stats_locked(struct task_struct *task,
	struct binder_stats *stats, int count, int caller)
{
	int i, temp;

	if (!refs_debug)
		return;
	if (!stats || !task)
		return;

	pr_info("\n");
	for (i = 0; i < ARRAY_SIZE(stats->bc); i++) {
		temp = atomic_read(&stats->bc[i]);
		if (refs_debug & DEBUG_ONLY_BC_INCDEC_REFS) {
			if (i != BC_INCREFS_INDEX && i != BC_DECREFS_INDEX)
				continue;
		}
		refs_dmesg(DEBUG_DMESG, "task[pid:%d tgid:%d comm:%s] caller:%d(2:proc 3:thread) BC %s: %d, th_cnt:%d\n",
			task->pid, task->tgid, task->comm, caller, binder_command_strings[i], temp, count);
	}
	pr_info("\n");
	if (refs_debug & DEBUG_ONLY_BC_INCDEC_REFS) {
		return;
	}
	for (i = 0; i < ARRAY_SIZE(stats->br); i++) {
		temp = atomic_read(&stats->br[i]);
		refs_dmesg(DEBUG_DMESG, "task[pid:%d tgid:%d comm:%s] caller:%d(2:proc 3:thread) BR %s: %d, th_cnt:%d\n",
			task->pid, task->tgid, task->comm, caller, binder_return_strings[i], temp, count);
	}

	for (i = 0; i < ARRAY_SIZE(stats->obj_created); i++) {
		int created = atomic_read(&stats->obj_created[i]);
		int deleted = atomic_read(&stats->obj_deleted[i]);

		if (created || deleted) {
			refs_dmesg(DEBUG_DMESG, "task[pid:%d tgid:%d comm:%s] caller:%d(2:proc 3:thread) %s: active %d total %d  th_cnt:%d\n",
				task->pid, task->tgid, task->comm, caller, binder_objstat_strings[i], created - deleted, created,  count);
		}
	}
	pr_info("\n");
}

void print_proc_stats_locked(struct binder_proc *proc, int caller)
{
	if ((refs_debug & DEBUG_PROC_STATS) == 0)
		return;
	if (!proc)
		return;
	print_binder_stats_locked(proc->tsk, &proc->stats, -1, caller);
}

void print_thread_stats_locked(struct binder_proc *proc, int caller)
{
	struct rb_node *n;
	struct binder_thread *thread;
	int count = 0;

	if ((refs_debug & DEBUG_THREAD_STATS) == 0)
		return;

	if (!proc)
		return;

	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
		count++;
		thread = rb_entry(n, struct binder_thread, rb_node);
		print_binder_stats_locked(thread->task, &thread->stats, count, caller);
	}
}

void print_proc_refs_by_desc(struct binder_proc *proc, int caller)
{
	struct rb_node *n = NULL;
	struct binder_node *node = NULL;
	struct binder_proc *node_proc = NULL;
	struct task_struct *node_proc_task = NULL;
	int count = 0, strong = 0, weak = 0, ref_data_null = 0;
	int death = 0, freeze = 0;

	if ((refs_debug & DEBUG_PROC_REFS) == 0)
		return;
	if (!proc) {
		refs_dmesg(DEBUG_DMESG_ALLREF, "proc null, return\n");
		return;
	}
	if (!proc->tsk) {
		refs_dmesg(DEBUG_DMESG_ALLREF, "proc->tsk null, return\n");
		return;
	}
	if (refs_debug & DEBUG_DMESG_ALLREF)
		pr_info("\n");
	for (n = rb_first(&proc->refs_by_desc); n != NULL; n = rb_next(n)) {
		struct binder_ref *ref = rb_entry(n, struct binder_ref,
						  rb_node_desc);
		count++;
		if (ref) {
			strong += ref->data.strong;
			weak += ref->data.weak;
		} else {
			ref_data_null++;
		}
		node = ref->node;
		if (node)
			node_proc = node->proc;
		if (node_proc)
			node_proc_task = node_proc->tsk;
		if (ref->death)
			death = 1;
		if (ref->freeze)
			freeze = 1;
		refs_dmesg(DEBUG_DMESG_ALLREF, "caller:%d count:%d, proc[pid:%d comm:%s], ref[node:%d node_proc_pid:%d comm:%s d:%d f:%d ]\n",
			caller, count, proc->tsk->pid, proc->tsk->comm,
			node ? node->debug_id : 0, node_proc_task ? node_proc_task->pid : 0,
			node_proc_task ? node_proc_task->comm : "null", death, freeze);
	}
	refs_dmesg(DEBUG_DMESG, "caller:%d proc->refs count:%d, s:%d w:%d, null:%d, proc[pid:%d comm:%s]\n",
		caller, count, strong, weak, ref_data_null, proc->tsk->pid, proc->tsk->comm);
}

void dump_t_info(struct binder_proc *proc, struct binder_transaction *t,
	struct task_struct *task, bool pending_async, bool sync, int caller, bool need_lock)
{
	struct task_struct *proc_task = NULL;
	struct task_struct *to_proc_task = NULL;
	struct task_struct *to_thread_task = NULL;
	struct binder_node *to_node = NULL;
	struct binder_ref *ref = NULL;
	struct binder_proc *ref_proc = NULL;
	struct task_struct *ref_proc_task = NULL;
	int count = 0;

	if ((refs_debug & DEBUG_DMESG_T_INFO) == 0)
		return;

	if (!t || !proc)
		return;
	proc_task = proc->tsk;
	if (t->to_proc)
		to_proc_task = t->to_proc->tsk;
	if (t->to_thread)
		to_thread_task = t->to_thread->task;

	if (t->buffer)
		to_node = t->buffer->target_node;

	pr_info("[lfc refs] caller:%d t[%d from pid:%d tid:%d code:%d flags:0x%x sync:%d p:%d] proc[pid:%d comm:%s] to_node:%d\n",
		caller, t->debug_id, t->from_pid, t->from_tid, t->code, t->flags, sync, pending_async,
		proc_task ? proc_task->pid : 0, proc_task ? proc_task->comm : "null",
		to_node ? to_node->debug_id : 0);

	pr_info("[lfc refs] caller:%d t[%d] task[%d %s] to_proc[%d %s] to_thread[%d %s]\n",
		caller, t->debug_id, task ? task->pid : 0, task ? task->comm : "null",
		to_proc_task ? to_proc_task->pid : 0, to_proc_task ? to_proc_task->comm : "null",
		to_thread_task ? to_thread_task->pid : 0, to_thread_task ? to_thread_task->comm : "null");

	if (to_node) {
		if (need_lock)
			binder_node_lock(to_node);
		if (refs_debug & DEBUG_DMESG_TO_NODE_REFS)
			pr_info("\n\n");
		hlist_for_each_entry(ref, &to_node->refs, node_entry) {
			count++;
			if (ref)
				ref_proc = ref->proc;
			if (ref_proc)
				ref_proc_task = ref_proc->tsk;
			if (refs_debug & DEBUG_DMESG_TO_NODE_REFS) {
				pr_info("[lfc refs] caller:%d t[%d] count:%d to_node[%d i_s:%d l_w:%d l_s:%d tmp:%d] ref[%d proc:%d tsk:%d death:%d freeze:%d]\n",
					caller, t->debug_id, count, to_node->debug_id, to_node->internal_strong_refs,
					to_node->local_weak_refs, to_node->local_strong_refs, to_node->tmp_refs,
					ref ? ref->data.debug_id : 0, ref_proc ? 1 : 0, ref_proc_task ? ref_proc_task->pid : 0,
					ref->death ? 1 : 0, ref->freeze ? 1 : 0);
			}
		}
		if (need_lock)
			binder_node_unlock(to_node);
		if (refs_debug & DEBUG_DMESG_TO_NODE_REFS)
			pr_info("\n\n");
	}
}

static bool debug_client_systemserver(struct task_struct *task)
{
	struct task_struct *group_leader = NULL;

	if ((refs_debug & DEBUG_CLIENT_SYSTEMSERVER) == 0)
		return false;
	if (!task)
		return false;
	group_leader = task->group_leader;
	if (!strncmp(group_leader->comm, "system_server", TASK_COMM_LEN)) {
		return true;
	} else {
		return false;
	}
}

static bool debug_client_referenceqd(struct task_struct *task)
{
	struct task_struct *group_leader = NULL;

	if ((refs_debug & DEBUG_CLIENT_REFERENCEQD) == 0)
		return false;
	if (!task)
		return false;
	group_leader = task->group_leader;
	if (strncmp(group_leader->comm, "system_server", TASK_COMM_LEN)) {
		return false;
	}
	if (strstr(task->comm, "ReferenceQu"))
		return true;
	else
		return false;
}

static bool debug_client_specified_process(struct task_struct *task)
{
	struct task_struct *group_leader = NULL;

	if ((refs_debug & DEBUG_CLIENT_SPECIFIED_PROC) == 0)
		return false;
	if (!task)
		return false;
	group_leader = task->group_leader;
	if (!group_leader)
		return false;

	if (group_leader->pid == refs_debug_client_id)
		return true;
	else
		return false;
}

static bool debug_client_specified_thread(struct task_struct *task)
{
	if ((refs_debug & DEBUG_CLIENT_SPECIFIED_TID) == 0)
		return false;
	if (!task)
		return false;

	if (task->pid == refs_debug_client_id)
		return true;
	else
		return false;
}

static bool debug_server_specified_process(struct task_struct *task)
{
	if ((refs_debug & DEBUG_SERVER_SPECIFIED_PROC) == 0)
		return false;
	if (!task)
		return false;

	if (task->pid == refs_debug_server_id)
		return true;
	else
		return false;
}

static bool debug_system_server_proc(struct binder_proc *proc)
{
	if ((refs_debug & DEBUG_SYSTEM_SERVER_PROC) == 0)
		return false;
	if (!proc)
		return false;

	if (!strncmp(proc->tsk->comm, SYSTEM_SERVER_NAME, TASK_COMM_LEN)) {
		return true;
	} else {
		return false;
	}
}

void binder_proc_transaction_finish_debug(struct binder_proc *proc,
	struct binder_transaction *t, struct task_struct *task,
	bool pending_async, bool sync)
{
	bool debug = false;
	static unsigned int delay_print = 0;

	if (!refs_debug) {
		return;
	}
	delay_print++;
	if (delay_print < refs_debug_interval) {
		return;
	}

	if (debug_system_server_proc(proc))
		debug = true;
	else if (debug_client_systemserver(current))
		debug = true;
	else if (debug_client_referenceqd(current))
		debug = true;
	else if (debug_client_specified_process(current))
		debug = true;
	else if (debug_client_specified_thread(current))
		debug = true;
	else if (debug_server_specified_process(proc->tsk))
		debug = true;

	if (debug) {
		delay_print = 0;
		dump_t_info(proc, t, task, pending_async, sync, 1, false);
		print_proc_stats_locked(proc, 2);
		print_thread_stats_locked(proc, 3);
		print_proc_refs_by_desc(proc, 4);
	}
}

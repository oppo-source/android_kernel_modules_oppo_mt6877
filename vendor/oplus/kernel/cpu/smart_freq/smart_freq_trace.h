/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM smart_freq
#if !defined(_TRACE_SMART_FREQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SMART_FREQ_H
#include <linux/sched.h>
#include <linux/types.h>

#include <linux/tracepoint.h>
TRACE_EVENT(ipc_freq,

	TP_PROTO(int id, int cpu, int index, unsigned int freq, u64 time, u64 deactivate_ns,
		 int curr_cpu, unsigned long ipc_cnt),

	TP_ARGS(id, cpu, index, freq, time, deactivate_ns, curr_cpu, ipc_cnt),

	TP_STRUCT__entry(
		__field(int, id)
		__field(int, cpu)
		__field(int, index)
		__field(unsigned int, freq)
		__field(u64, time)
		__field(u64, deactivate_ns)
		__field(int, curr_cpu)
		__field(unsigned long, ipc_cnt)
	),

	TP_fast_assign(
		__entry->id = id;
		__entry->cpu = cpu;
		__entry->index = index;
		__entry->freq = freq;
		__entry->time = time;
		__entry->deactivate_ns = deactivate_ns;
		__entry->curr_cpu = curr_cpu;
		__entry->ipc_cnt = ipc_cnt;
	),

	TP_printk("cluster=%d winning_cpu=%d winning_index=%d winning_freq=%u curr_time=%llu dactivate_time=%llu current_cpu=%d current_cpu_ipc_count=%lu",
		  __entry->id, __entry->cpu, __entry->index, __entry->freq,
		  __entry->time, __entry->deactivate_ns, __entry->curr_cpu, __entry->ipc_cnt)
);

TRACE_EVENT(ipc_update,

	TP_PROTO(int cpu, unsigned long cycle_cnt, unsigned long intr_cnt, unsigned long ipc_cnt,
		 unsigned long last_ipc_update, u64 deactivate_ns, u64 now),

	TP_ARGS(cpu, cycle_cnt, intr_cnt, ipc_cnt, last_ipc_update, deactivate_ns, now),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(unsigned long, cycle_cnt)
		__field(unsigned long, intr_cnt)
		__field(unsigned long, ipc_cnt)
		__field(unsigned long, last_ipc_update)
		__field(u64, deactivate_ns)
		__field(u64, now)
	),

	TP_fast_assign(
		__entry->cpu = cpu;
		__entry->cycle_cnt = cycle_cnt;
		__entry->intr_cnt = intr_cnt;
		__entry->ipc_cnt = ipc_cnt;
		__entry->last_ipc_update = last_ipc_update;
		__entry->deactivate_ns = deactivate_ns;
		__entry->now = now;
	),

	TP_printk("cpu=%d cycle_cnt=%lu intr_cnt=%lu ipc_count=%lu last_ipc_update=%lu ipc_deactivate_ns=%llu now=%llu",
		  __entry->cpu, __entry->cycle_cnt, __entry->intr_cnt,  __entry->ipc_cnt,
		  __entry->last_ipc_update, __entry->deactivate_ns, __entry->now)
);

#endif	/*_TRACE_SMART_FREQ_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../kernel/oplus_cpu/smart_freq

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE smart_freq_trace
/* This part must be outside protection */
#include <trace/define_trace.h>


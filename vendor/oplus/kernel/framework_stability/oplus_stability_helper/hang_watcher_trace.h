// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025-2030 Oplus. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hang_watcher_trace

#if !defined(_HANG_WATCHER_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _HANG_WATCHER_TRACE_H_

#include <linux/tracepoint.h>

TRACE_EVENT(system_server_hang,
	TP_PROTO(int app_id, const char *log_tag, const char *event_id, long fault_timestamp_ms, const char *thread_comm, long block_time),
	TP_ARGS(app_id, log_tag, event_id, fault_timestamp_ms, thread_comm, block_time),
	TP_STRUCT__entry(
		__field(int,			app_id)
		__string(log_tag,		log_tag)
		__string(event_id,		event_id)
		__field(long,			fault_timestamp_ms)
		__string(thread_comm,		thread_comm)
		__field(long,			block_time)),

	TP_fast_assign(
		__entry->app_id =		app_id;
		__assign_str(log_tag,		log_tag);
		__assign_str(event_id,		event_id);
		__entry->fault_timestamp_ms =	fault_timestamp_ms;
		__assign_str(thread_comm,	thread_comm);
		__entry->block_time =		block_time;),
	TP_printk("app_id:%d log_tag:%s event_id:%s fault_timestamp_ms:%ld thread_comm:%s block_time:%ld",
		__entry->app_id, __get_str(log_tag), __get_str(event_id), __entry->fault_timestamp_ms, __get_str(thread_comm), __entry->block_time)
);

#endif /* _HANG_WATCHER_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../../vendor/oplus/kernel/framework_stability/oplus_stability_helper
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hang_watcher_trace

/* This part must be outside protection */
#include <trace/define_trace.h>

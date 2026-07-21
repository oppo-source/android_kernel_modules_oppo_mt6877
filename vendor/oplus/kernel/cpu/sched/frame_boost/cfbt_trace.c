/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include "cfbt_trace.h"
#include "cfbt_boost_struct.h"
#include "cfbt_boost.h"
#include "frame_debug.h"

enum {
	TRACE_CFBT_UTIL,
	TRACE_CFBT_FRAME_STATE,
	TRACE_CFBT_STAGE,
	TRACE_CFBT_TASK,
	TRACE_CFBT_RESCUE,
	TRACE_CFBT_ENHANCE,
	TRACE_CFBT_RUTIL,
	TRACE_CFBT_UFRAMEID,
	MAX_CFBT_MSG_ID,
};

// Static variables to hold the last values for comparison
struct cfbt_trace_filed {
	unsigned long last_val_util;
	unsigned long last_val_frame_state;
	unsigned long last_val_stage;
	unsigned long last_val_task;
	unsigned long last_val_rescue;
	unsigned long last_val_enhance;
	unsigned long last_val_rutil;
	unsigned long last_val_uframeid;
};

struct cfbt_trace_filed trace_fileds [CFBT_MAX_GROUP_NUM] = {0};

inline bool test_frameid_valid(int grp_id)
{
	if (grp_id < 0 || grp_id > (CFBT_MAX_GROUP_NUM - 1))
		return false;
	return true;
}

noinline int tracing_mark_write(const char *buf)
{
	trace_printk(buf);
	return 0;
}

unsigned long *get_last_val_util(int grp_id)
{
	return &trace_fileds[grp_id].last_val_util;
}

unsigned long *get_last_val_frame_state(int grp_id)
{
	return &trace_fileds[grp_id].last_val_frame_state;
}

unsigned long *get_last_val_stage(int grp_id)
{
	return &trace_fileds[grp_id].last_val_stage;
}

unsigned long *get_last_val_task(int grp_id)
{
	return &trace_fileds[grp_id].last_val_task;
}

unsigned long *get_last_val_rescue(int grp_id)
{
	return &trace_fileds[grp_id].last_val_rescue;
}

unsigned long *get_last_val_enhance(int grp_id)
{
	return &trace_fileds[grp_id].last_val_enhance;
}

unsigned long *get_last_val_rutil(int grp_id)
{
	return &trace_fileds[grp_id].last_val_rutil;
}

unsigned long *get_last_val_uframeid(int grp_id)
{
	return &trace_fileds[grp_id].last_val_uframeid;
}

void cfbt_val_systrace_c(int grp_id, unsigned long val, const char *msg, int msg_id)
{
	char buf[256];

	if ((grp_id < 0) || (grp_id >= CFBT_MAX_GROUP_NUM) ||
		(msg_id < TRACE_CFBT_UTIL || msg_id >= MAX_CFBT_MSG_ID)) {
		return;
	}

	snprintf(buf, sizeof(buf), "C|10000|grp[%d]%s|%lu\n", grp_id, msg, val);
	tracing_mark_write(buf);
}

void cfbt_systrace_c(char *msg, unsigned long val)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "C|10000|%s|%lu\n", msg, val);
	tracing_mark_write(buf);
}

static void trace_value(int frame_id, unsigned long val, const char *msg, int msg_id, unsigned long *last_val)
{
	if (!is_tracing_enabled())
		return;
	if (val != *last_val) {
		*last_val = val; // Update last value
		cfbt_val_systrace_c(frame_id, val, msg, msg_id);
	}
}

void trace_cfbt_util(int frame_id, unsigned long val)
{
	if (!test_frameid_valid(frame_id))
		return;
	trace_value(frame_id, val, "cfbt_util", TRACE_CFBT_UTIL, get_last_val_util(frame_id));
}

void trace_cfbt_frame_state(int frame_id, unsigned long val)
{
	if (!test_frameid_valid(frame_id))
		return;
	trace_value(frame_id, val, "cfbt_frame_state", TRACE_CFBT_FRAME_STATE, get_last_val_frame_state(frame_id));
}

void trace_cfbt_stage(int frame_id, unsigned long val)
{
	if (!test_frameid_valid(frame_id))
		return;
	trace_value(frame_id, val, "cfbt_stage", TRACE_CFBT_STAGE, get_last_val_stage(frame_id));
}

void trace_cfbt_task(int frame_id, unsigned long val)
{
	if (!test_frameid_valid(frame_id))
		return;
	trace_value(frame_id, val, "cfbt_task", TRACE_CFBT_TASK, get_last_val_task(frame_id));
}

void trace_cfbt_rescue(int frame_id, unsigned long val)
{
	if (!test_frameid_valid(frame_id))
		return;
	trace_value(frame_id, val, "cfbt_rescue", TRACE_CFBT_RESCUE, get_last_val_rescue(frame_id));
}

void trace_cfbt_enhance(int frame_id, unsigned long val)
{
	if (!test_frameid_valid(frame_id))
		return;
	trace_value(frame_id, val, "cfbt_enhance", TRACE_CFBT_ENHANCE, get_last_val_enhance(frame_id));
}

void trace_cfbt_rutil(int frame_id, unsigned long val)
{
	if (!test_frameid_valid(frame_id))
		return;
	trace_value(frame_id, val, "cfbt_rutil", TRACE_CFBT_RUTIL, get_last_val_rutil(frame_id));
}

void trace_cfbt_uframeid(int frame_id, unsigned long val)
{
	if (!test_frameid_valid(frame_id))
		return;
	trace_value(frame_id, val, "cfbt_uframeid", TRACE_CFBT_UFRAMEID, get_last_val_uframeid(frame_id));
}

void trace_cfbt_systrace_c(char *msg, unsigned long val)
{
	if (!is_tracing_enabled())
		return;
	cfbt_systrace_c(msg, val);
}

void trace_frame_end(int frame_id)
{
	trace_cfbt_util(frame_id, 0);
	trace_cfbt_frame_state(frame_id, 0);
	trace_cfbt_stage(frame_id, 0);
	trace_cfbt_rescue(frame_id, 0);
	trace_cfbt_enhance(frame_id, 0);
	trace_cfbt_rutil(frame_id, 0);
	trace_cfbt_uframeid(frame_id, 0);
}

void cfbt_trace_init(void)
{
	memset(&trace_fileds, 0, sizeof(trace_fileds));
}

void cfbt_trace_notify_err(int err)
{
	trace_cfbt_systrace_c("cfbt_notify err", err);
}
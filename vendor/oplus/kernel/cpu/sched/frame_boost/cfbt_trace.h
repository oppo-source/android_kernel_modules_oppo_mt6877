/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#ifndef CFBT_TRACE_H
#define CFBT_TRACE_H

/**
 * trace_cfbt_util - Trace the CFBT utility value
 * @frame_id: The identifier for the frame
 * @val: The utility value to trace
 *
 * This function records the utility value for the specified frame.
 */
void trace_cfbt_util(int frame_id, unsigned long val);

/**
 * trace_cfbt_frame_state - Trace the state of the frame
 * @frame_id: The identifier for the frame
 * @val: The state value to trace
 *
 * This function records the state of the specified frame.
 */
void trace_cfbt_frame_state(int frame_id, unsigned long val);

/**
 * trace_cfbt_stage - Trace the current stage of processing
 * @frame_id: The identifier for the frame
 * @val: The stage value to trace
 *
 * This function records the current processing stage for the specified frame.
 */
void trace_cfbt_stage(int frame_id, unsigned long val);

/**
 * trace_cfbt_task - Trace the task associated with the frame
 * @frame_id: The identifier for the frame
 * @val: The task value to trace
 *
 * This function records the task information for the specified frame.
 */
void trace_cfbt_task(int frame_id, unsigned long val);

/**
 * trace_cfbt_rescue - Trace the rescue status
 * @frame_id: The identifier for the frame
 * @val: The rescue value to trace
 *
 * This function records the rescue status for the specified frame.
 */
void trace_cfbt_rescue(int frame_id, unsigned long val);

/**
 * trace_cfbt_enhance - Trace the enhancement value
 * @frame_id: The identifier for the frame
 * @val: The enhancement value to trace
 *
 * This function records the enhancement information for the specified frame.
 */
void trace_cfbt_enhance(int frame_id, unsigned long val);

/**
 * trace_cfbt_rutil - Trace the resource utilization
 * @frame_id: The identifier for the frame
 * @val: The resource utilization value to trace
 *
 * This function records the resource utilization for the specified frame.
 */
void trace_cfbt_rutil(int frame_id, unsigned long val);

/**
 * trace_cfbt_systrace_c - Trace a message with associated value
 * @msg: The message to trace
 * @val: The value associated with the message
 *
 * This function records a message along with an associated value for tracing.
 */
void trace_cfbt_systrace_c(char *msg, unsigned long val);
void trace_cfbt_uframeid(int frame_id, unsigned long val);

void trace_frame_end(int frame_id);
void cfbt_trace_init(void);
void cfbt_trace_notify_err(int err);
#endif // CFBT_TRACE_H

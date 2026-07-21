/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#ifndef CFBT_RESCURE_H
#define CFBT_RESCURE_H

#include "cfbt_boost.h"

/**
 * update_stage_running_time - Update the running time for a specific stage
 * @grp: Pointer to the cfbt_frame_group structure
 * @running_time: The running time to be set
 * @stage: The stage index to be updated
 *
 * This function updates the running time for a given stage in the frame group.
 */
void update_stage_running_time(struct cfbt_frame_group *grp, u64 running_time, int stage);

/**
 * update_frame_running_time - Update the running time for the entire frame
 * @grp: Pointer to the cfbt_frame_group structure
 * @running_time: The running time to be set
 *
 * This function updates the running time for the entire frame group.
 */
void update_frame_running_time(struct cfbt_frame_group *grp, u64 running_time);

/**
 * retrieve_rescue_time - Retrieve the rescue time information
 * @buf: Buffer to hold the output string
 * @len: Length of the output buffer
 *
 * This function populates the provided buffer with rescue time information
 * and returns the status.
 *
 * Returns 0 on success.
 */
int retrieve_rescue_time(char *buf, int len);

/**
 * clear_stage_rescue - Clear rescue flags for the specified stage
 * @grp: Pointer to the cfbt_frame_group structure
 *
 * This function clears any rescue flags associated with the specified stage in the frame group.
 */
void clear_stage_rescue(struct cfbt_frame_group *grp);

/**
 * clear_frame_rescue - Clear rescue flags for the entire frame
 * @grp: Pointer to the cfbt_frame_group structure
 *
 * This function clears any rescue flags associated with the entire frame group.
 */
void clear_frame_rescue(struct cfbt_frame_group *grp);

/**
 * activate_stage_rescue - Set rescue flags for the specified stage
 * @grp: Pointer to the cfbt_frame_group structure
 *
 * This function sets rescue flags for the specified stage in the frame group.
 */
void activate_stage_rescue(struct cfbt_frame_group *grp);

/**
 * activate_frame_rescue - Set rescue flags for the entire frame
 * @grp: Pointer to the cfbt_frame_group structure
 *
 * This function sets rescue flags for the entire frame group.
 */
void activate_frame_rescue(struct cfbt_frame_group *grp);

/**
 * get_rescue_utilization - Calculate the rescue utility value
 * @grp: Pointer to the cfbt_frame_group structure
 * @fbg_util: Current framebuffer utility value
 *
 * This function calculates and returns the rescue utility based on the
 * current framebuffer utility and the state of the frame group.
 *
 * Returns the calculated rescue utility value.
 */
unsigned long get_rescue_utilization(struct cfbt_frame_group *grp, unsigned long fbg_util);

/**
 * start_cfbt_timer - Start the CFBT timer for a frame group
 * @grp: Pointer to the cfbt_frame_group structure
 *
 * This function starts the CFBT timer for the specified frame group.
 */
void start_cfbt_timer(struct cfbt_frame_group *grp);

/**
 * init_cfbt_rescue - Initialize the CFBT rescue subsystem
 *
 * This function performs the necessary initialization for the CFBT rescue
 * subsystem. It should be called at the start of the program or module.
 */
void init_cfbt_rescue(void);

#endif // CFBT_RESCURE_H


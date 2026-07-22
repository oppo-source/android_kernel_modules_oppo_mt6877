/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#ifndef CFBT_CONFIG_H
#define CFBT_CONFIG_H

#include <linux/types.h>
#include <linux/seq_file.h>

enum {
	CFBT_CONF_RESCUE_ENABLE = 0,
	CFBT_CONF_STAGE_ENHANCE = 1,
	CFBT_CONF_FRAME_ENHANCE = 2,
	CFBT_CONF_SKIP_CPU      = 3,
};

#define MAX_USER_CFG_COUNT 6
#define CAMERA_FRAME_INTERVAL (80000000L)

extern int user_configurations[MAX_USER_CFG_COUNT];

/**
 * is_rescue_enabled - Check if rescue functionality is enabled
 *
 * Returns 1 if rescue functionality is enabled, otherwise returns 0.
 */
int is_rescue_enabled(void);

/**
 * get_stage_enhancement_value - Get the stage enhancement value
 *
 * Returns the enhancement value for the stage configuration.
 */
int get_stage_enhancement_value(void);

/**
 * get_frame_enhancement_value - Get the frame enhancement value
 *
 * Returns the enhancement value for the frame configuration.
 */
int get_frame_enhancement_value(void);

/**
 * get_max_stage_count - Get the maximum number of stages for a given scene
 * @scene: The scene for which to retrieve the maximum stage count
 *
 * This function returns the maximum number of stages for the specified scene.
 *
 * Returns the maximum stage count.
 */
int get_max_stage_count(int scene);

/**
 * get_default_stage_timeout - Get the default time for a specified stage
 * @stage: The stage index for which to retrieve the default time
 *
 * Returns the default stage time in milliseconds.
 */
int get_default_stage_timeout(int stage);

/**
 * get_target_time_for_scene - Get the target time for a specified scene
 * @scene: The scene for which to retrieve the target time
 *
 * Returns the target time in nanoseconds.
 */
u64 get_target_time_for_scene(int scene);

/**
 * get_configuration_version - Get the version of the configuration
 *
 * Returns the version number of the configuration.
 */
int get_configuration_version(void);

/**
 * enable_tracing - Enable or disable tracing
 * @val: 1 to enable tracing, 0 to disable
 *
 * This function sets the tracing configuration value.
 */
void enable_tracing(int val);

/**
 * is_tracing_enabled - Check if tracing is enabled
 *
 * Returns 1 if tracing is enabled, otherwise returns 0.
 */
int is_tracing_enabled(void);

/**
 * enable_selection_option - Set the option for selection
 * @val: 1 to enable selection option, 0 to disable
 *
 * This function sets the configuration for selection options.
 */
void enable_selection_option(int val);

/**
 * is_selection_option_enabled - Check if the selection option is enabled
 *
 * Returns 1 if the selection option is enabled, otherwise returns 0.
 */
int is_selection_option_enabled(void);

int update_configuration_stage(int scene, int stage);

int update_scene_interval(int scene, u64 interval);
void print_scene_configs(struct seq_file *m);

void enable_cfbt(int value);
int is_cfbt_enabled(void);

void suspend_cfbt(int value);
int is_cfbt_suspend(void);

int get_skip_cpu_by_user_config(void);

void set_cfbt_util_down(int value);
int get_cfbt_util_down(void);

#endif // CFBT_CONFIG_H
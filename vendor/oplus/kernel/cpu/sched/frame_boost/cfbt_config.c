/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */


#include "cfbt_config.h"
#include "cfbt_boost_struct.h"

#define MAX_CFBT_STAGES 5

int user_configurations[MAX_USER_CFG_COUNT] = {1, 512, 512, 7, 0, 0};
EXPORT_SYMBOL(user_configurations);

int tracing_enabled = 0;
EXPORT_SYMBOL(tracing_enabled);

int selection_option_enabled = 0;
EXPORT_SYMBOL(selection_option_enabled);

int cfbt_enable = 0;
int cfbt_suspend = 0;
int cfbt_down_util = 1024;
int stage_timeouts[MAX_CFBT_STAGES] = {20, 16, 16, 33, 20};

struct scene_stage_mapping {
	int scene;
	int stage_count;
	u64 frame_interval;
	int dynamic_stage_count;
	u64 dynamic_interval;
};

struct scene_stage_mapping scene_stage_map[] = {
	{CFBT_CAMERA_4K_60FPS, 4, CAMERA_FRAME_INTERVAL, 0, 0},
};

// User configuration functions
int get_user_configuration(int index)
{
	if (index < 0 || index >= MAX_USER_CFG_COUNT)
		return -1;

	return user_configurations[index];
}

int is_rescue_enabled(void)
{
	return get_user_configuration(CFBT_CONF_RESCUE_ENABLE);
}

int get_stage_enhancement_value(void)
{
	return get_user_configuration(CFBT_CONF_STAGE_ENHANCE);
}

int get_frame_enhancement_value(void)
{
	return get_user_configuration(CFBT_CONF_FRAME_ENHANCE);
}

int get_skip_cpu_by_user_config(void)
{
	return get_user_configuration(CFBT_CONF_SKIP_CPU);
}

// Stage time functions
int get_default_stage_timeout(int stage)
{
	if (stage < 0 || stage >= MAX_CFBT_STAGES)
		return -1;

	return stage_timeouts[stage];
}

struct scene_stage_mapping *get_config_by_scene(int scene)
{
	for (int i = 0; i < ARRAY_SIZE(scene_stage_map); i++) {
		if (scene_stage_map[i].scene == scene) {
			return &scene_stage_map[i];
		}
	}
	return NULL;
}

int get_max_stage_count(int scene)
{
	struct scene_stage_mapping *config = get_config_by_scene(scene);

	if (config == NULL)
		return 0;

	return config->stage_count > config->dynamic_stage_count ? \
		config->stage_count : config->dynamic_stage_count;
}

u64 get_target_time_for_scene(int scene)
{
	for (int i = 0; i < ARRAY_SIZE(scene_stage_map); i++) {
		if (scene_stage_map[i].scene == scene) {
			return scene_stage_map[i].dynamic_interval > 0 ? \
				scene_stage_map[i].dynamic_interval : scene_stage_map[i].frame_interval;
		}
	}
	return CAMERA_FRAME_INTERVAL; // Default if not found
}

int update_scene_interval(int scene, u64 interval)
{
	struct scene_stage_mapping *config;
	int ret = 0;

	config = get_config_by_scene(scene);
	if (config) {
		config->dynamic_interval = interval;
		pr_info("Updated scene:%d interval:%llu\n", scene, interval);
	}
	return ret;
}
EXPORT_SYMBOL(update_scene_interval);

void print_scene_configs(struct seq_file *m)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(scene_stage_map); i++) {
		seq_printf(m,
			"Scene:%d | stage_count:%d | frame_interval:%llu | "
			"dynamic_stage_count:%d | dynamic_interval:%llu\n",
			scene_stage_map[i].scene,
			scene_stage_map[i].stage_count,
			scene_stage_map[i].frame_interval,
			scene_stage_map[i].dynamic_stage_count,
			scene_stage_map[i].dynamic_interval
		);
	}
}
EXPORT_SYMBOL(print_scene_configs);

int get_configuration_version(void)
{
	return 20241219;
}

int update_configuration_stage(int scene, int stage)
{
	struct scene_stage_mapping *config = get_config_by_scene(scene);

	if (config == NULL)
		return 0;

	if (stage <= config->stage_count)
		return 0;

	if (stage <= config->dynamic_stage_count)
		return 0;

	config->dynamic_stage_count = stage;

	return 0;
}

// Tracing functions
void enable_tracing(int value)
{
	tracing_enabled = value;
}
EXPORT_SYMBOL(enable_tracing);

int is_tracing_enabled(void)
{
	return tracing_enabled;
}
EXPORT_SYMBOL(is_tracing_enabled);

// Selection option functions
void enable_selection_option(int value)
{
	selection_option_enabled = value;
}
EXPORT_SYMBOL(enable_selection_option);

int is_selection_option_enabled(void)
{
	return selection_option_enabled;
}
EXPORT_SYMBOL(is_selection_option_enabled);

extern int __cfbt_set_scene_end(struct cfbt_struct *data);
void enable_cfbt(int value)
{
	struct cfbt_struct tmp;
	cfbt_enable = value;

	if (!value)
		__cfbt_set_scene_end(&tmp);
}
EXPORT_SYMBOL(enable_cfbt);

int is_cfbt_enabled(void)
{
	return cfbt_enable;
}
EXPORT_SYMBOL(is_cfbt_enabled);

void suspend_cfbt(int value)
{
	cfbt_suspend = value;
}
EXPORT_SYMBOL(suspend_cfbt);

int is_cfbt_suspend(void)
{
	return cfbt_suspend;
}
EXPORT_SYMBOL(is_cfbt_suspend);

void set_cfbt_util_down(int value)
{
	cfbt_down_util = value;
}
EXPORT_SYMBOL(set_cfbt_util_down);

int get_cfbt_util_down(void)
{
	return cfbt_down_util;
}
EXPORT_SYMBOL(get_cfbt_util_down);

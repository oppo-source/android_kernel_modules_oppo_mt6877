/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 MediaTek Inc.
 *
 */

#include<linux/leds-mtk.h>
#include "../drivers/gpu/drm/mediatek/mediatek_v2/mtk_panel_ext.h"

extern struct list_head leds_list;

struct led_debug_info {
	unsigned long long current_t;
	unsigned long long last_t;
	char buffer[4096];
	int count;
};

struct led_desp {
	int index;
	int connector_id;
	char name[16];
};

struct mt_led_data {
	struct led_desp desp;
	struct led_conf_info	conf;
	int last_brightness;
	int hw_brightness;
	int last_hw_brightness;
	struct led_debug_info debug;
	int (*mtk_hw_brightness_set)(struct mt_led_data *m_data,
		int brightness, unsigned int params, unsigned int params_flag);
	int (*mtk_conn_id_get)(struct mt_led_data *m_data,
		int flag);
	struct mutex	led_access;
	int last_percent;
};
#ifdef OPLUS_FEATURE_DISPLAY_APOLLO
unsigned int mBrightnessValue = 1;
bool mApolloDisable = false;
bool oplus_apollo_unsupported(void);
void apollo_set_brightness_for_show(unsigned int level);
#endif /* OPLUS_FEATURE_DISPLAY_APOLLO */

#ifdef OPLUS_FEATURE_DISPLAY
unsigned int m_brightness2_value = 1;
bool m_brightness2_enable = false;
bool oplus_brightness2_supported(void);
void set_brightness2_for_show(unsigned int level);
#endif /* OPLUS_FEATURE_DISPLAY */

#ifdef OPLUS_FEATURE_DISPLAY
bool oplus_silky_support = false;
bool oplus_project_silky_support(void);
bool oplus_public_brightness_support = false;
bool oplus_project_public_brightness_support(void);
#endif /* OPLUS_FEATURE_DISPLAY */

int mt_leds_parse_dt(struct mt_led_data *mdev, struct fwnode_handle *fwnode);
int mt_leds_classdev_register(struct device *parent,
					 struct mt_led_data *led_dat);
void mt_leds_classdev_unregister(struct device *parent,
				     struct mt_led_data *led_dat);
int mt_leds_call_notifier(unsigned long action, void *data);

extern int mtkfb_set_backlight_level(unsigned int level,
		unsigned int params, unsigned int params_flag);
extern int mtk_drm_set_conn_backlight_level(unsigned int conn_id, unsigned int level,
		unsigned int params, unsigned int params_flag);
extern unsigned int mtk_drm_get_conn_obj_id_from_idx(unsigned int disp_idx, int flag);

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef TYPEC_SWITCH_I2C_H
#define TYPEC_SWITCH_I2C_H

#include <linux/of.h>
#include <linux/notifier.h>
#include "common_reg.h"

enum typec_switch_function {
	TYPEC_SWITCH_MIC_GND_SWAP,
	TYPEC_SWITCH_USBC_ORIENTATION_CC1,
	TYPEC_SWITCH_USBC_ORIENTATION_CC2,
	TYPEC_SWITCH_USBC_DISPLAYPORT_DISCONNECTED,
/* Add DIO4480 support */
	TYPEC_SWITCH_CONNECT_LR,
	TYPEC_SWITCH_EVENT_MAX,
};

#if IS_ENABLED(CONFIG_OPLUS_TYPEC_SWITCH_I2C)
int typec_switch_switch_event(struct device_node *node,
			 enum typec_switch_function event);
int typec_switch_reg_notifier(struct notifier_block *nb,
			 struct device_node *node);
int typec_switch_unreg_notifier(struct notifier_block *nb,
			   struct device_node *node);

/* Add DIO4480 support */
int typec_switch_get_chip_vendor(struct device_node *node);
int typec_switch_check_cross_conn(struct device_node *node);

#else
static inline int typec_switch_switch_event(struct device_node *node,
				       enum typec_switch_function event)
{
	return 0;
}

static inline int typec_switch_reg_notifier(struct notifier_block *nb,
				       struct device_node *node)
{
	return 0;
}

static inline int typec_switch_unreg_notifier(struct notifier_block *nb,
					 struct device_node *node)
{
	return 0;
}

/* Add DIO4480 support */
static inline int typec_switch_get_chip_vendor(struct device_node *node)
{
    return 0;
}

static inline int typec_switch_check_cross_conn(struct device_node *node)
{
    return 0;
}
#endif /* CONFIG_OPLUS_TYPEC_SWITCH_I2C */

#endif /* TYPEC_SWITCH_I2C_H */


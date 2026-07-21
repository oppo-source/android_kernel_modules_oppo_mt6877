// oplus_flashlight_notify.c
// This file contains the implementation of the oplus_flashlight_notify class.
// Copyright 2025 OPPO. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
// This file contains the implementation of the oplus_flashlight_notify class.
// Author: Huyan Xing
// Created: 2025-12-03
// Notes: flashlight notify

#ifndef __FLASHLIGHT_NOTIFY_H__
#define __FLASHLIGHT_NOTIFY_H__

#include <linux/notifier.h>
#include "../flashlight-core.h"


//#include "hf_manager.h"
//#include "hf_sensor_type.h"
//#include "sensor_comm.h"
#include "../../sensor/2.0/core/hf_manager.h"
#include "../../sensor/2.0/core/hf_sensor_type.h"
#include "../../sensor/2.0/sensorhub/sensor_comm.h"

enum {
	FLASHLIGHT_STATUS_TYPE = 1,
};
enum {
	FLASHLIGHT_STATUS_OFF = 0,
	FLASHLIGHT_STATUS_ON  = 1,
};

int register_flashlight_notifier(struct notifier_block *nb);
int unregister_flashlight_notifier(struct notifier_block *nb);
void flashlight_notify(unsigned long val, void *v);
int flashlight_cs_notify_init(void);
//#endif
#endif

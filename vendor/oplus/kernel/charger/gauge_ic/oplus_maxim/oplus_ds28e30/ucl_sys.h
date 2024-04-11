// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2024 Oplus. All rights reserved.
 */

#ifndef _UCL_SYS_H_
#define _UCL_SYS_H_
#include <linux/slab.h> /* kfree() */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/types.h>
int __API__ ucl_init(void);
#endif /* _UCL_SYS_H_ */

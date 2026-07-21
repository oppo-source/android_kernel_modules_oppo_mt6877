/******************************************************************************
** File: - oplus_tfa98xx_feedback.h
**
** Copyright (C), 2022-2024, OPLUS Mobile Comm Corp., Ltd
**
** Description:
**     Implementation of tfa98xx reg error or speaker r0 or f0 error feedback.
**
** Version: 1.0
** --------------------------- Revision History: ------------------------------
**      <author>                                       <date>                  <desc>
*******************************************************************************/
#ifndef OPLUS_TFA98XX_FEEDBACK_H
#define OPLUS_TFA98XX_FEEDBACK_H

#define pr_fmt(fmt) "%s(): " fmt, __func__

#include "oplus_tfa9865_reg.h"

#define TFA98XX_STATUS_FLAGS0 0x10
#define TFA98XX_STATUS_FLAGS1 0x11
#define TFA98XX_STATUS_FLAGS2 0x12
#define TFA98XX_STATUS_FLAGS3 0x13
#define TFA98XX_STATUS_FLAGS4 0x14

#define MAX_STATUS_REG_CHECKED_COUNT (2)

struct oplus_tfa98xx_check_info {
	int device_revision;
	int reg_checked_count;
	int reg_checked_addr[MAX_STATUS_REG_CHECKED_COUNT];
	int reg_checked_mask[MAX_STATUS_REG_CHECKED_COUNT];
	int expected_value[MAX_STATUS_REG_CHECKED_COUNT];
};

#endif /* OPLUS_TFA98XX_FEEDBACK_H */


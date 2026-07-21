/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 *
 * Health report string macros for chips directory
 * This file contains all macro definitions used in hbp_healthinfo_report
 * and hbp_dev_healthinfo_report functions within chips directory
 */

#ifndef _CHIPS_HEALTHINFO_REPORT_H_
#define _CHIPS_HEALTHINFO_REPORT_H_

#include "../hbp_healthinfo.h"

/* Health report string macros - used in chips/goodix/gt99x6/gt99x6_core.c */
#define CHIPS_REPORT_GOODIX_SPI_READ_FAIL              "chips_goodix_spi_read_fail"
#define CHIPS_REPORT_GOODIX_SPI_WRITE_FAIL             "chips_goodix_spi_write_fail"
#define CHIPS_REPORT_GOODIX_GESTURE_INVALID_HEAD       "chips_goodix_gesture_invalid_head"
#define CHIPS_REPORT_GOODIX_GESTURE_CHECKSUM_FAIL      "chips_goodix_gesture_checksum_fail"
#define CHIPS_REPORT_GOODIX_GESTURE_UNKNOWN_TYPE       "chips_goodix_gesture_unknown_type"
#define CHIPS_REPORT_GOODIX_FRAME_INVALID_HEAD         "chips_goodix_frame_invalid_head"
#define CHIPS_REPORT_GOODIX_FRAME_CHECKSUM_FAIL       "chips_goodix_frame_checksum_fail"
#define CHIPS_REPORT_GOODIX_FRAME_INVALID_LEN         "chips_goodix_frame_invalid_len"
#define CHIPS_REPORT_GOODIX_UNKNOWN_IC_NAME            "chips_goodix_unknown_ic_name"
#define CHIPS_REPORT_GOODIX_PROBE_ALLOC_FAIL          "chips_goodix_probe_alloc_fail"
#define CHIPS_REPORT_GOODIX_PROBE_REGISTER_FAIL       "chips_goodix_probe_register_fail"
#define CHIPS_REPORT_GOODIX_RESET_ERROR_FAIL          "chips_goodix_reset_error_fail"
#define CHIPS_REPORT_GOODIX_RESET_WATCHDOG_FAIL       "chips_goodix_reset_watchdog_fail"

/* Health report string macros - used in chips/focal/ft3683g/fhp_core.c and chips/focal/ft3685g/fhp_core.c */
/* Common macros for same exception branches - used by both ft3683g and ft3685g */
#define CHIPS_REPORT_FOCAL_CHIP_WRITE_FAIL            "chips_focal_chip_write_fail"
#define CHIPS_REPORT_FOCAL_CHIP_READ_FAIL             "chips_focal_chip_read_fail"
#define CHIPS_REPORT_FOCAL_GESTURE_READ_FAIL          "chips_focal_gesture_read_fail"
#define CHIPS_REPORT_FOCAL_FOD_INFO_READ_FAIL         "chips_focal_fod_info_read_fail"
#define CHIPS_REPORT_FOCAL_FOD_ERROR_READ_FAIL        "chips_focal_fod_error_read_fail"
#define CHIPS_REPORT_FOCAL_AOD_INFO_READ_FAIL         "chips_focal_aod_info_read_fail"
#define CHIPS_REPORT_FOCAL_IRQ_REASON_READ_FAIL       "chips_focal_irq_reason_read_fail"
#define CHIPS_REPORT_FOCAL_TOUCH_POINTS_READ_FAIL     "chips_focal_touch_points_read_fail"
#define CHIPS_REPORT_FOCAL_TOUCH_INVALID_POINT_NUM    "chips_focal_touch_invalid_point_num"
#define CHIPS_REPORT_FOCAL_TOUCH_INVALID_ID           "chips_focal_touch_invalid_id"
#define CHIPS_REPORT_FOCAL_TOUCH_ABNORMAL_DATA        "chips_focal_touch_abnormal_data"
#define CHIPS_REPORT_FOCAL_TOUCH_NO_POINT_INFO        "chips_focal_touch_no_point_info"
#define CHIPS_REPORT_FOCAL_TOUCH_INVALID_EVENT_NUM    "chips_focal_touch_invalid_event_num"
#define CHIPS_REPORT_FOCAL_PROBE_ALLOC_FAIL           "chips_focal_probe_alloc_fail"
#define CHIPS_REPORT_FOCAL_PROBE_REGISTER_FAIL        "chips_focal_probe_register_fail"
#endif /* _CHIPS_HEALTHINFO_REPORT_H_ */

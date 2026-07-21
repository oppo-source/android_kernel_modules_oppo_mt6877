/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Universal Flash Storage Host Initiated Defrag
 *
 * Copyright (C) 2019 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *	Yongmyung Lee <ymhungry.lee@samsung.com>
 *	Jinyoung Choi <j-young.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * See the COPYING file in the top-level directory or visit
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This program is provided "AS IS" and "WITH ALL FAULTS" and
 * without warranty of any kind. You are solely responsible for
 * determining the appropriateness of using and distributing
 * the program and assume all risks associated with your exercise
 * of rights with respect to the program, including but not limited
 * to infringement of third party rights, the risks and costs of
 * program errors, damage to or loss of data, programs or equipment,
 * and unavailability or interruption of operations. Under no
 * circumstances will the contributor of this Program be liable for
 * any damages of any kind arising from your use or distribution of
 * this program.
 *
 * The Linux Foundation chooses to take subject only to the GPLv2
 * license terms, and distributes only under these terms.
 */

#ifndef _UFSHID_H_
#define _UFSHID_H_

#include "ufshid-common.h"
#include <linux/blktrace_api.h>
#include <linux/blkdev.h>
#include <linux/bitfield.h>
#include <scsi/scsi_cmnd.h>

#include "../../../block/blk.h"

#define UFSHID_VER					0x0305
#define UFSHID_V7_DD_VER				0x031101
#define UFSHID_V7_DD_VER_POST				""

#define UFS_FEATURE_SUPPORT_V7_HID_BIT			(1 << 0)

#define HID_FRAG_LEVEL_MASK		0xF
#define HID_FRAG_UPDATE_MODE_SHIFT	29
#define HID_FRAG_UPDATE_STAT_SHIFT	30
#define HID_EXECUTE_REQ_STAT_SHIFT	31
#define HID_FRAG_UPDATE_MODE(val)	((val >> HID_FRAG_UPDATE_MODE_SHIFT) & 0x1)
#define HID_FRAG_UPDATE_STAT(val)	((val >> HID_FRAG_UPDATE_STAT_SHIFT) & 0x1)
#define HID_EXECUTE_REQ_STAT(val)	((val >> HID_EXECUTE_REQ_STAT_SHIFT) & 0x1)

#define HID_WB_TIMEOUT			(10 * HZ)
#define HID_MAX_RANGE_CNT		(1 << 8)

#define HID_L2P_COMMAND_MODE			0x1D
#define HID_L2P_DEFRAG_THRESHOLD_DEFAULT	0x0
#define HID_L2P_MAX_THRESHOLD			0xA
#define HID_L2P_DEFRAG_SUP_MASK			(1 << 0)
#define HID_L2P_DEFRAG_LVL_UNKNOWN		0xB
#define QUERY_ATTR_IDN_HID_OPERATION			0x80
#define QUERY_ATTR_IDN_HID_FRAG_LEVEL			0x81
#define QUERY_ATTR_IDN_HID_SIZE				0x8A
#define QUERY_ATTR_IDN_HID_AVAIL_SIZE			0x8B
#define QUERY_ATTR_IDN_HID_PROGRESS_RATIO		0x8C
#define QUERY_ATTR_IDN_HID_STATE			0x8D
#define QUERY_ATTR_IDN_HID_L2P_FRAG_LEVEL		0x8E
#define QUERY_ATTR_IDN_HID_L2P_DEFRAG_THRESHOLD		0x8F
#define QUERY_ATTR_IDN_HID_FEAT_SUP			0x90

#define QUERY_ATTR_IDN_VENDOR_EE_CONTROL		0x97
#define QUERY_ATTR_IDN_VENDOR_EE_STATUS			0x98

/* Device descriptor parameters offsets in bytes*/
#define DEVICE_DESC_PARAM_EX_FEAT_SUP			0x4F
#define DEVICE_DESC_PARAM_SAMSUNG_SUP			0xFB
#define DEVICE_DESC_PARAM_HID_VER			0xF7

/* Geometry descriptor parameters offsets in bytes */
#define GEOMETRY_DESC_HID_MAX_LBA_RANGE_CNT	0xF8
#define GEOMETRY_DESC_HID_MAX_LBA_RANGE_SIZE	0xF9

#endif /* End of Header */

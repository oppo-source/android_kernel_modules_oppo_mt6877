/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Universal Flash Storage Jedec Host Initiated Defrag
 *
 * Copyright (C) 2019 Samsung Electronics Co., Ltd.
 *
 * Authors:
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

#ifndef _UFS_HID_H_
#define _UFS_HID_H_

#include "ufshid-common.h"

#define UFS_FEATURE_SUPPORT_JEDEC_HID_BIT	(1 << 13)

#define AVAIL_ANALYSIS_REQUIRED			0xFFFFFFFF

#define UFSHID_JEDEC_DD_VER				0x010103
#define UFSHID_JEDEC_DD_VER_POST			""

/* JEDEC specific IDN enum */
enum ufshid_idn_idx {
	/* Attribute */
	QUERY_ATTR_IDN_HID_OP_IDX,
	QUERY_ATTR_IDN_HID_SZ_IDX,
	QUERY_ATTR_IDN_HID_AVAIL_SZ_IDX,
	QUERY_ATTR_IDN_HID_PROGRESS_RATIO_IDX,
	QUERY_ATTR_IDN_HID_STATE_IDX,
	HID_JEDEC_IDN_IDX_END,
};

#endif /* End of Header */

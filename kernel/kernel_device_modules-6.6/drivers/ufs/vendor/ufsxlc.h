/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Universal Flash Storage xLC Buffering
 *
 * Copyright (C) 2025-2025 Samsung Electronics Co., Ltd.
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

#ifndef _UFSXLC_H_
#define _UFSXLC_H_

#include <linux/kernel.h>

#define UFSXLC_DD_VER		0x010102
#define UFSXLC_DD_VER_POST	""

#define UFS_FEATURE_SUPPORT_XLC_BIT		(1 << 11)

struct ufsxlc_offset {
	u8 offset;
};

enum ufsxlc_idn_idx {
	QUERY_ATTR_IDN_XLC_BUF_NAND_MODE_IDX,
	QUERY_FLAG_IDN_XLC_PGA_EN_IDX,
	XLC_IDN_INDX_END,
};

enum UFSXLC_STATE {
	XLC_NEED_INIT = 0,
	XLC_PRESENT = 1,
	XLC_FAILED = -2,
	XLC_RESET = -3,
};

struct ufsxlc_dev {
	struct ufsf_feature *ufsf;

	/* for sysfs & procfs */
	struct kobject kobj;
	struct mutex sysfs_lock;
	struct ufsxlc_sysfs_entry *sysfs_entries;
};

struct ufsxlc_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct ufsxlc_dev *xlc, char *buf);
	ssize_t (*store)(struct ufsxlc_dev *xlc, const char *buf,
			 size_t count);
};

int ufsxlc_get_state(struct ufsf_feature *ufsf);
void ufsxlc_set_state(struct ufsf_feature *ufsf, int state);
void ufsxlc_init(struct ufsf_feature *ufsf);
void ufsxlc_get_dev_info(struct ufsf_feature *ufsf, u8 *desc_buf);
void ufsxlc_remove(struct ufsf_feature *ufsf);
void ufsxlc_reset(struct ufsf_feature *ufsf);
void ufsxlc_reset_host(struct ufsf_feature *ufsf);
#endif /* ENd of Header */

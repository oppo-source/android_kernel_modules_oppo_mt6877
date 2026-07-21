/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Universal Flash Storage TurboPocket
 *
 * Copyright (C) 2025 Samsung Electronics Co., Ltd.
 *
 * Author:
 *	Keoseong Park <keosung.park@samsung.com>
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

#ifndef _UFSTP_H_
#define _UFSTP_H_

#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/hashtable.h>
#include <linux/pagemap.h>

#define UFSTP_DD_VER					0x010102
#define UFSTP_DD_VER_POST				""

#define UFS_FEATURE_SUPPORT_TP_BIT BIT(10)

#define UFSTP_WRITE_BUFFER_MODE 0x1
#define UFSTP_WRITE_BUFFER_ID 0x1
#define UFSTP_READ_BUFFER_MODE 0x1
#define UFSTP_READ_BUFFER_ID 0x1
#define UFSTP_MAX_ENTRY_COUNT 128
#define UFSTP_MAX_TOTAL_LENGTH 0x40000

#define UFSTP_READ_BUFFER_MAX_BUF_SIZE 32768

#define TP_GROUP_NUMBER 0x12

#define TP_DEFAULT_INODES 512
#define TP_MAX_INODES 1024
#define TP_INO_HASH_BITS 10 /* 2^10 = 1024 */

enum UFSTP_STATE {
	TP_NEED_INIT = 0,
	TP_PRESENT = 1,
	TP_RESET = -2,
	TP_FAILED = -3,
};

struct ufstp_wb_header {
	__be16 entry_cnt;
	__be16 lun;
} __packed;

struct ufstp_wb_entry {
	__be32 lba;
	__be32 length;
} __packed;

struct ufstp_dev_info {
	/* from Vendor Specific Device Descriptor */
	u16 tp_ver;
	u32 tp_shared_alloc_units;

	/* from Vendor Specific Geometry Descriptor */
	u32 tp_max_alloc_units;
};

struct wb_req {
	int lun;
	u8 buf[PAGE_SIZE];
	size_t buf_size;
};

struct rb_req {
	int lun;
	u8 buf[UFSTP_READ_BUFFER_MAX_BUF_SIZE];
	size_t allocation_len;
};

struct ufstp_ino {
	unsigned long ino;
	struct hlist_node hnode;
	struct rcu_head rcu;
};

struct ufstp_dev {
	struct ufsf_feature *ufsf;
	bool tp_enable;
	u32 tp_size;

	struct wb_req wb;
	struct rb_req rb;

	unsigned int max_inodes;
	DECLARE_HASHTABLE(ino_ht, TP_INO_HASH_BITS);
	atomic_t ino_cnt;

	/* for sysfs */
	struct kobject kobj;
	struct mutex sysfs_lock;
	struct ufstp_sysfs_entry *sysfs_entries;

	/* for procfs */
	struct proc_dir_entry *tp_proc_root;
};

struct ufstp_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct ufstp_dev *tp, char *buf);
	ssize_t (*store)(struct ufstp_dev *tp, const char *buf, size_t count);
};

struct ufstp_offset {
	u8 offset;
};

struct ufshcd_lrb;

int ufstp_get_state(struct ufsf_feature *ufsf);
void ufstp_set_state(struct ufsf_feature *ufsf, int state);
void ufstp_get_dev_info(struct ufsf_feature *ufsf, u8 *desc_buf);
void ufstp_get_geo_info(struct ufsf_feature *ufsf, u8 *geo_buf);
void ufstp_get_conf_info(struct ufsf_feature *ufsf);
void ufstp_init(struct ufsf_feature *ufsf);
void ufstp_reset_host(struct ufsf_feature *ufsf);
void ufstp_reset(struct ufsf_feature *ufsf);
void ufstp_remove(struct ufsf_feature *ufsf);
void ufstp_prep_fn(struct ufsf_feature *ufsf, struct ufshcd_lrb *lrbp);

#endif /* End of Header */

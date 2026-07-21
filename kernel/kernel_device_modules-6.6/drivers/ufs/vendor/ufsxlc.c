// SPDX-License-Identifier: GPL-2.0
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

#include "ufshcd-priv.h"
#include "ufsfeature.h"
#include "ufsxlc.h"

static int ufsxlc_create_sysfs(struct ufsxlc_dev *xlc);

const struct ufsxlc_offset ufsxlc_idn[] = {
	/* Attribute */
	[QUERY_ATTR_IDN_XLC_BUF_NAND_MODE_IDX] = { 0xA6 },
	/* Flag */
	[QUERY_FLAG_IDN_XLC_PGA_EN_IDX] = { 0x87 },
};

inline int ufsxlc_get_state(struct ufsf_feature *ufsf)
{
	return atomic_read(&ufsf->xlc_state);
}

inline void ufsxlc_set_state(struct ufsf_feature *ufsf, int state)
{
	atomic_set(&ufsf->xlc_state, state);
}

static inline int ufsxlc_is_not_present(struct ufsxlc_dev *xlc)
{
	enum UFSXLC_STATE cur_state = ufsxlc_get_state(xlc->ufsf);

	if (cur_state != XLC_PRESENT) {
		INFO_MSG("xlc_state != XLC_PRESENT (%d)", cur_state);
		return -ENODEV;
	}
	return 0;
}

static inline u8 ufsxlc_get_idn(struct ufsf_feature *ufsf,
				enum ufsxlc_idn_idx name)
{
	return ufsxlc_idn[name].offset;
}

void ufsxlc_get_dev_info(struct ufsf_feature *ufsf, u8 *desc_buf)
{
	ufsf->xlc_dev = NULL;

	if (!(get_unaligned_be32(desc_buf +
			DEVICE_DESC_PARAM_SAMSUNG_SUP) &
			UFS_FEATURE_SUPPORT_XLC_BIT)) {
		INFO_MSG("xLC Buffering not support");
		goto err_out;
	}

	INFO_MSG("xLC Buffering support");
	INFO_MSG("xLC Buffering D/D version (%.6X%s)", UFSXLC_DD_VER,
		 UFSXLC_DD_VER_POST);

	ufsf->xlc_dev = kzalloc(sizeof(struct ufsxlc_dev), GFP_KERNEL);
	if (!ufsf->xlc_dev) {
		ERR_MSG("xlc_dev memalloc fail");
		goto err_out;
	}

	ufsf->xlc_dev->ufsf = ufsf;
	return;

err_out:
	ufsxlc_set_state(ufsf, XLC_FAILED);
}

void ufsxlc_reset_host(struct ufsf_feature *ufsf)
{
	struct ufsxlc_dev *xlc = ufsf->xlc_dev;

	if (!xlc)
		return;

	ufsxlc_set_state(ufsf, XLC_RESET);
}

void ufsxlc_reset(struct ufsf_feature *ufsf)
{
	struct ufsxlc_dev *xlc = ufsf->xlc_dev;

	if (!xlc)
		return;

	ufsxlc_set_state(ufsf, XLC_PRESENT);

	INFO_MSG("reset completed.");
}

static inline void ufsxlc_remove_sysfs(struct ufsxlc_dev *xlc)
{
	int ret;

	ret = kobject_uevent(&xlc->kobj, KOBJ_REMOVE);
	INFO_MSG("kobject removed (%d)", ret);
	kobject_del(&xlc->kobj);
	kobject_put(&xlc->kobj);
}

void ufsxlc_init(struct ufsf_feature *ufsf)
{
	struct ufsxlc_dev *xlc;
	int ret;

	INFO_MSG("xLC_Buffering_INIT_START");

	xlc = ufsf->xlc_dev;
	if (!xlc) {
		ERR_MSG("xlc_dev was not allocated. so disable xlc.");
		ufsxlc_set_state(ufsf, XLC_FAILED);
		return;
	}

	ret = ufsxlc_create_sysfs(xlc);
	if (ret) {
		ERR_MSG("Creating UFS xLC Buffering sysfs files. (%d)", ret);
		goto create_sysfs_fail;
	}

	INFO_MSG("UFS xLC Buffering create sysfs finished");

	ufsxlc_set_state(ufsf, XLC_PRESENT);
	return;

create_sysfs_fail:
	kfree(ufsf->xlc_dev);
	ufsf->xlc_dev = NULL;
	ufsxlc_set_state(ufsf, XLC_FAILED);
}

void ufsxlc_remove(struct ufsf_feature *ufsf)
{
	struct ufsxlc_dev *xlc = ufsf->xlc_dev;

	if (!xlc)
		return;

	INFO_MSG("start xLC Buffering release");

	ufsxlc_set_state(ufsf, XLC_FAILED);

	ufsxlc_remove_sysfs(xlc);

	kfree(xlc);
	ufsf->xlc_dev = NULL;

	INFO_MSG("end xLC Buffering release");
}

/***********************************************************************
 * There are functions for SYSFS in below.
 **********************************************************************/
static ssize_t
ufsxlc_sysfs_show_bBufferNandMode(struct ufsxlc_dev *xlc, char *buf) {
	struct ufsf_feature *ufsf = xlc->ufsf;
	struct ufs_hba *hba = ufsf->hba;
	int ret = 0;
	u8 idn;
	u32 status;

	ufshcd_rpm_get_sync(hba);

	idn = ufsxlc_get_idn(ufsf, QUERY_ATTR_IDN_XLC_BUF_NAND_MODE_IDX);
	ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
					idn, 0, 0, &status);

	ufshcd_rpm_put_sync(hba);

	if (ret) {
		ERR_MSG("bBufferNandMode read query fail (%d)", ret);
		return -ENODEV;
	}

	INFO_MSG("bBufferNandMode read query success (%d)", status);

	return snprintf(buf, PAGE_SIZE, "%d\n",status);
}

static ssize_t
ufsxlc_sysfs_store_bBufferNandMode(struct ufsxlc_dev *xlc, const char *buf, size_t count) {
	struct ufsf_feature *ufsf = xlc->ufsf;
	struct ufs_hba *hba = ufsf->hba;
	int ret = 0;
	u32 val;
	u8 idn;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	ufshcd_rpm_get_sync(hba);

	idn = ufsxlc_get_idn(ufsf, QUERY_ATTR_IDN_XLC_BUF_NAND_MODE_IDX);
	ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_WRITE_ATTR,
				      idn, 0, 0, &val);

	ufshcd_rpm_put_sync(hba);

	if (ret) {
		ERR_MSG("bBufferNandMode write query fail (%d)", ret);
		return -ENODEV;
	}

	INFO_MSG("bBufferNandMode write query success (%d)", val);

	return count;
}

static ssize_t
ufsxlc_sysfs_show_fPGAEn(struct ufsxlc_dev *xlc, char *buf) {
	struct ufsf_feature *ufsf = xlc->ufsf;
	struct ufs_hba *hba = ufsf->hba;
	int ret = 0;
	bool flag_res = false;
	u8 idn;

	ufshcd_rpm_get_sync(hba);

	idn = ufsxlc_get_idn(ufsf, QUERY_FLAG_IDN_XLC_PGA_EN_IDX);
	ret = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_READ_FLAG,
				      idn, 0, &flag_res);

	ufshcd_rpm_put_sync(hba);

	if (ret) {
		ERR_MSG("fPGA En read query fail (%d)", ret);
		return -ENODEV;
	}

	INFO_MSG("fPGA En read query success (%d)", flag_res);

	return snprintf(buf, PAGE_SIZE, "%d\n",flag_res);
}

static ssize_t
ufsxlc_sysfs_store_fPGAEn(struct ufsxlc_dev *xlc, const char *buf, size_t count) {
	struct ufsf_feature *ufsf = xlc->ufsf;
	struct ufs_hba *hba = ufsf->hba;
	u32 val;
	enum query_opcode op = 0;
	u8 idn;
	int ret = 0;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	op = val ? (UPIU_QUERY_OPCODE_SET_FLAG) :
		(UPIU_QUERY_OPCODE_CLEAR_FLAG);

	ufshcd_rpm_get_sync(hba);

	idn = ufsxlc_get_idn(ufsf, QUERY_FLAG_IDN_XLC_PGA_EN_IDX);
	ret = ufshcd_query_flag_retry(hba, op, idn, 0, NULL);

	ufshcd_rpm_put_sync(hba);

	if (ret) {
		ERR_MSG("fPGA En write query fail (%d)", ret);
		return -ENODEV;
	}

	INFO_MSG("fPGA_EN write query success (%d)", val);

	return count;
}

#define define_xlc_sysfs_rw(_name) __ATTR(_name, 0644,			\
				      ufsxlc_sysfs_show_##_name,	\
				      ufsxlc_sysfs_store_##_name)
static struct ufsxlc_sysfs_entry ufsxlc_sysfs_entries[] = {
	define_xlc_sysfs_rw(bBufferNandMode),
	define_xlc_sysfs_rw(fPGAEn),
	__ATTR_NULL
};

static ssize_t ufsxlc_attr_show(struct kobject *kobj,
				struct attribute *attr, char *page)
{
	struct ufsxlc_sysfs_entry *entry;
	struct ufsxlc_dev *xlc;
	ssize_t error;

	entry = container_of(attr, struct ufsxlc_sysfs_entry, attr);
	if (!entry->show)
		return -EIO;

	xlc = container_of(kobj, struct ufsxlc_dev, kobj);
	if (ufsxlc_is_not_present(xlc))
		return -ENODEV;

	mutex_lock(&xlc->sysfs_lock);
	error = entry->show(xlc, page);
	mutex_unlock(&xlc->sysfs_lock);

	return error;
}

static ssize_t ufsxlc_attr_store(struct kobject *kobj,
				 struct attribute *attr, const char *page,
				 size_t length)
{
	struct ufsxlc_sysfs_entry *entry;
	struct ufsxlc_dev *xlc;
	ssize_t error;

	entry = container_of(attr, struct ufsxlc_sysfs_entry, attr);
	if (!entry->store)
		return -EIO;

	xlc = container_of(kobj, struct ufsxlc_dev, kobj);
	if (ufsxlc_is_not_present(xlc))
		return -ENODEV;

	mutex_lock(&xlc->sysfs_lock);
	error = entry->store(xlc, page, length);
	mutex_unlock(&xlc->sysfs_lock);

	return error;
}

static const struct sysfs_ops ufsxlc_sysfs_ops = {
	.show = ufsxlc_attr_show,
	.store = ufsxlc_attr_store,
};

static struct kobj_type ufsxlc_ktype = {
	.sysfs_ops = &ufsxlc_sysfs_ops,
	.release = NULL,
};

static int ufsxlc_create_sysfs(struct ufsxlc_dev *xlc)
{
	struct device *dev = xlc->ufsf->hba->dev;
	struct ufsxlc_sysfs_entry *entry;
	int err;

	xlc->sysfs_entries = ufsxlc_sysfs_entries;

	kobject_init(&xlc->kobj, &ufsxlc_ktype);
	mutex_init(&xlc->sysfs_lock);

	INFO_MSG("ufsxlc creates sysfs ufsxlc %p dev->kobj %p",
		 &xlc->kobj, &dev->kobj);

	err = kobject_add(&xlc->kobj, kobject_get(&dev->kobj),
			  "ufsxlc");
	if (!err) {
		for (entry = xlc->sysfs_entries; entry->attr.name != NULL;
		     entry++) {
			INFO_MSG("ufsxlc sysfs attr creates: %s",
				 entry->attr.name);
			err = sysfs_create_file(&xlc->kobj, &entry->attr);
			if (err) {
				ERR_MSG("create entry(%s) failed",
					entry->attr.name);
				goto kobj_del;
			}
		}
		kobject_uevent(&xlc->kobj, KOBJ_ADD);
	} else {
		ERR_MSG("kobject_add failed");
	}

	return err;

kobj_del:
	err = kobject_uevent(&xlc->kobj, KOBJ_REMOVE);
	INFO_MSG("kobject removed (%d)", err);
	kobject_del(&xlc->kobj);
	kobject_put(&xlc->kobj);
	return -EINVAL;
}

MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
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

#include "ufshcd-priv.h"
#include "ufsfeature.h"
#include "ufstp.h"

enum ufstp_idn_idx {
	/* Attribute */
	QUERY_ATTR_IDN_TP_AVAIL_SZ_IDX,
	/* Flag */
	QUERY_FLAG_IDN_TP_EN_IDX,
	TP_IDN_IDX_END,
};

enum ufstp_desc_idx {
	/* Vendor Device */
	DEV_DESC_PARAM_TP_SHARED_ALLOC_UNITS_IDX,
	DEV_DESC_PARAM_TP_VER_IDX,
	/* Vendor Geometry */
	GEO_DESC_TP_SIZE_MAX_ALLOC_UNITS_IDX,
	TP_DESC_IDX_END,
};

const struct ufstp_offset ufstp_idn[] = {
	/* Attribute */
	[QUERY_ATTR_IDN_TP_AVAIL_SZ_IDX] = { 0x99 },
	/* Flag */
	[QUERY_FLAG_IDN_TP_EN_IDX] = { 0x86 },
};

const struct ufstp_offset ufstp_desc[] = {
	/* Vendor Device */
	[DEV_DESC_PARAM_TP_SHARED_ALLOC_UNITS_IDX] = { 0xDB },
	[DEV_DESC_PARAM_TP_VER_IDX] = { 0xDF },
	/* Vendor Geometry */
	[GEO_DESC_TP_SIZE_MAX_ALLOC_UNITS_IDX] = { 0xF3 },
};

static inline u8 ufstp_get_idn(enum ufstp_idn_idx name)
{
	return ufstp_idn[name].offset;
}

static inline u8 ufstp_get_desc(enum ufstp_desc_idx name)
{
	return ufstp_desc[name].offset;
}

inline int ufstp_get_state(struct ufsf_feature *ufsf)
{
	return atomic_read(&ufsf->tp_state);
}

inline void ufstp_set_state(struct ufsf_feature *ufsf, int state)
{
	atomic_set(&ufsf->tp_state, state);
}

static inline void ufstp_print_version(struct ufstp_dev_info *tp_dev_info)
{
	INFO_MSG("Support TP Spec: %.4x", tp_dev_info->tp_ver);
	INFO_MSG("TP Driver Version: %.6X%s", UFSTP_DD_VER, UFSTP_DD_VER_POST);
}

static int ufstp_set_flag(struct ufstp_dev *tp, u8 idn, bool *flag_res)
{
	struct ufs_hba *hba = tp->ufsf->hba;
	int ret;

	ret = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_SET_FLAG, idn,
				      0, NULL);
	if (ret) {
		ERR_MSG("Set flag [0x%.2X] failed. (%d)", idn, ret);
		return ret;
	}

	if (flag_res) {
		*flag_res = true;

		INFO_MSG("Set flag [0x%.2X] success. (%u)", idn, *flag_res);
	}

	return ret;
}

static int ufstp_clear_flag(struct ufstp_dev *tp, u8 idn, bool *flag_res)
{
	struct ufs_hba *hba = tp->ufsf->hba;
	int ret;

	ret = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_CLEAR_FLAG, idn,
				      0, NULL);
	if (ret) {
		ERR_MSG("Clear flag [0x%.2X] failed. (%d)", idn, ret);
		return ret;
	}

	*flag_res = false;

	INFO_MSG("Clear flag [0x%.2X] success. (%u)", idn, *flag_res);

	return ret;
}

static int ufstp_read_flag(struct ufstp_dev *tp, u8 idn, bool *flag_res)
{
	struct ufs_hba *hba = tp->ufsf->hba;
	int ret;
	bool val;

	ret = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_READ_FLAG, idn,
				      0, &val);
	if (ret) {
		ERR_MSG("Read flag [0x%.2X] failed. (%d)", idn, ret);
		return ret;
	}

	*flag_res = val;

	INFO_MSG("Read flag [0x%.2X] success. (%u)", idn, *flag_res);

	return ret;
}

void ufstp_get_dev_info(struct ufsf_feature *ufsf, u8 *desc_buf)
{
	struct ufstp_dev_info *tp_dev_info = &ufsf->tp_dev_info;
	u8 desc;

	if (!(get_unaligned_be32(desc_buf + DEVICE_DESC_PARAM_SAMSUNG_SUP) &
		     UFS_FEATURE_SUPPORT_TP_BIT)) {
		ERR_MSG("dExtendedSamsungFeaturesSupport: TP not support");
		ufstp_set_state(ufsf, TP_FAILED);
		return;
	}

	desc = ufstp_get_desc(DEV_DESC_PARAM_TP_VER_IDX);
	tp_dev_info->tp_ver = get_unaligned_be16(desc_buf + desc);

	desc = ufstp_get_desc(DEV_DESC_PARAM_TP_SHARED_ALLOC_UNITS_IDX);
	tp_dev_info->tp_shared_alloc_units = get_unaligned_be32(
							desc_buf + desc);

	ufstp_print_version(tp_dev_info);

	if (!tp_dev_info->tp_shared_alloc_units) {
		ERR_MSG("dLUSharedTurboPocketAllocUnits is 0");
		ufstp_set_state(ufsf, TP_FAILED);
		return;
	}

	ufsf->tp_dev = kzalloc(sizeof(struct ufstp_dev), GFP_KERNEL);
	if (!ufsf->tp_dev) {
		ERR_MSG("tp_dev mem-alloc failed");
		ufstp_set_state(ufsf, TP_FAILED);
		return;
	}

	ufsf->tp_dev->ufsf = ufsf;
}

void ufstp_get_geo_info(struct ufsf_feature *ufsf, u8 *geo_buf)
{
	struct ufstp_dev_info *tp_dev_info = &ufsf->tp_dev_info;
	u8 desc;

	desc = ufstp_get_desc(GEO_DESC_TP_SIZE_MAX_ALLOC_UNITS_IDX);
	tp_dev_info->tp_max_alloc_units = get_unaligned_be32(geo_buf + desc);
	if (!tp_dev_info->tp_max_alloc_units) {
		ERR_MSG("TurboPocket is not supported");
		ufstp_set_state(ufsf, TP_FAILED);
		return;
	}

	INFO_MSG("TP-GEOMETRY [0x%.2X] dTurboPocketSizeMaxNAllocUnits (%u)",
		 desc, tp_dev_info->tp_max_alloc_units);
}

static int ufstp_check_param(struct ufstp_dev *tp,
			     unsigned char *buf, unsigned int size,
			     unsigned int *lba_count)
{
	struct ufstp_wb_header *wb_header;
	struct ufstp_wb_entry *entry;
	const char *p = buf;
	int entry_cnt, total_entry, entry_size, total_length = 0;
	u32 lba, len;

	INFO_MSG("Parameter size %u", size);

	wb_header = (struct ufstp_wb_header *)p;
	total_entry = get_unaligned_be16(&wb_header->entry_cnt);
	if (!total_entry || total_entry > UFSTP_MAX_ENTRY_COUNT) {
		ERR_MSG("Invalid total entry count (%d)", total_entry);
		return -EINVAL;
	}

	p += sizeof(struct ufstp_wb_header);

	entry_size = sizeof(struct ufstp_wb_entry);
	for (entry_cnt = 0; entry_cnt < total_entry; entry_cnt++, p += entry_size) {
		entry = (struct ufstp_wb_entry *)p;
		lba = get_unaligned_be32(&entry->lba);
		len = get_unaligned_be32(&entry->length);

		if (!lba || !len) {
			ERR_MSG("Entry[%d] information is not valid", entry_cnt);
			return -EINVAL;
		}

		if (total_length + len > UFSTP_MAX_TOTAL_LENGTH) {
			ERR_MSG("Exceed UFSTP_MAX_TOTAL_LENGTH");
			return -EINVAL;
		}

		total_length += len;
	}

	*lba_count = total_length;

	return 0;
}

static void ufstp_set_param(struct ufstp_dev *tp, int lun,
			    const unsigned char *buf, unsigned int size,
			    unsigned int *lba_count)
{
	struct wb_req *wb = &tp->wb;
	struct rb_req *rb = &tp->rb;

	wb->lun = lun;
	memcpy(wb->buf, buf, size);
	wb->buf_size = size;

	rb->lun = lun;
	rb->allocation_len = *lba_count;
	memset(rb->buf, 0, rb->allocation_len);
}

/*
 * This is for saving the WRITE/READ BUFFER parameter.
 * If the parameter is not explicitly cleared by the user,
 * the driver saves and uses it.
 */
static void ufstp_save_buffer_param(struct ufstp_dev *tp, struct ufshcd_lrb *lrbp)
{
	struct scsi_cmnd *cmd = lrbp->cmd;
	struct request *rq = scsi_cmd_to_rq(cmd);
	struct bio *bio = rq->bio;
	struct page *page;
	unsigned char *buf;
	unsigned int len, lba_count;

	if (cmd->cmnd[1] != UFSTP_WRITE_BUFFER_MODE ||
	    cmd->cmnd[2] != UFSTP_WRITE_BUFFER_ID)
		return;

	page = bio->bi_io_vec->bv_page;
	len = bio->bi_io_vec->bv_len;

	if (tp->wb.buf_size == len &&
	    !memcmp(tp->wb.buf, page_address(page), len)) {
		INFO_MSG("WRITE BUFFER parameter is same as before");
		return;
	}

	buf = page_address(page);

	if (ufstp_check_param(tp, buf, len, &lba_count)) {
		ERR_MSG("Wrong WRITE BUFFER parameter");
		return;
	}

	put_unaligned_be16(lrbp->lun, buf + 2);

	lba_count = (lba_count + 7) / 8;

	ufstp_set_param(tp, lrbp->lun, buf, len, &lba_count);
}

static unsigned long ufstp_get_ino(struct request *req)
{
	struct bio *bio;
	struct address_space *f_mapping;
	struct page *page;
	struct inode *inode;

	if (unlikely(!req))
		return 0;

	bio = req->bio;

	if (unlikely(!bio || !bio_has_data(bio) || !bio->bi_io_vec ||
		     !bio->bi_io_vec->bv_page))
		return 0;

	page = bio->bi_io_vec->bv_page;

	/*
	 * NOTE: Constraints for recovering i_ino from DUN
	 *
	 * - Supported policy: recovery is ONLY possible when the fscrypt
	 *   policy is FSCRYPT_POLICY_FLAG_IV_INO_LBLK_64. Other policies
	 *   do not encode i_ino in a reversible form.
	 *
	 * - Android: fscrypt_generate_iv() not called for /data itself;
	 *   only applies under /data subdirectories. Additionally, the inode
	 *   number can be recovered via this helper only under subdirectories
	 *   that already exist within /data (e.g., /data/data, /data/app).
	 */
	if (PageAnon(page)) { /* For direct IO */
		struct bio_crypt_ctx *bc;

		if (!bio_has_crypt_ctx(bio))
			return 0;

		bc = bio->bi_crypt_context;

		/*
		 * Under certain policy (see NOTE), the upper 32 bits
		 * are the inode number.
		 */
		return (unsigned long)(bc->bc_dun[0] >> 32);
	}

	f_mapping = page_file_mapping(page);
	if (!f_mapping)
		return 0;

	inode = f_mapping->host;
	if (!inode)
		return 0;

	return inode->i_ino;
}

static inline bool ufstp_ino_exists(struct ufstp_dev *tp, unsigned long ino)
{
	struct ufstp_ino *n;

	rcu_read_lock();
	hash_for_each_possible_rcu(tp->ino_ht, n, hnode, ino) {
		if (n->ino == ino) {
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();
	return false;
}

static void ufstp_set_tp_in_by_inode(struct ufstp_dev *tp,
				     struct scsi_cmnd *cmd)
{
	struct request *req = scsi_cmd_to_rq(cmd);
	unsigned long ino;

	if (!tp->tp_enable)
		return;

	if (!atomic_read(&tp->ino_cnt))
		return;

	ino = ufstp_get_ino(req);
	if (!ino)
		return;

	if (!ufstp_ino_exists(tp, ino))
		return;

	if (cmd->cmnd[0] == WRITE_10)
		cmd->cmnd[6] = TP_GROUP_NUMBER;
	else if (cmd->cmnd[0] == WRITE_16)
		cmd->cmnd[14] = TP_GROUP_NUMBER;
}

void ufstp_prep_fn(struct ufsf_feature *ufsf, struct ufshcd_lrb *lrbp)
{
	struct scsi_cmnd *cmd = lrbp->cmd;
	struct ufstp_dev *tp = ufsf->tp_dev;

	switch (cmd->cmnd[0]) {
	case WRITE_10:
	case WRITE_16:
		ufstp_set_tp_in_by_inode(tp, cmd);
		break;
	case WRITE_BUFFER:
		ufstp_save_buffer_param(tp, lrbp);
		break;
	default:
		break;
	}
}

void ufstp_reset_host(struct ufsf_feature *ufsf)
{
	if (ufstp_get_state(ufsf) != TP_PRESENT)
		return;

	ufstp_set_state(ufsf, TP_RESET);
}

void ufstp_reset(struct ufsf_feature *ufsf)
{
	ufstp_set_state(ufsf, TP_PRESENT);
}

static int ufstp_issue_write_buffer(struct ufstp_dev *tp)
{
	struct wb_req *req = &tp->wb;
	struct ufsf_feature *ufsf = tp->ufsf;
	unsigned char cdb[10] = {0};
	struct scsi_device *sdev;
	struct scsi_sense_hdr sshdr;
	int ret = 0, retries;
	const struct scsi_exec_args args = {
		.sshdr = &sshdr,
	};

	if (!req->buf_size) {
		ERR_MSG("Parameter buffer size is 0");
		return -EINVAL;
	}

	cdb[0] = WRITE_BUFFER;
	cdb[1] = UFSTP_WRITE_BUFFER_MODE;
	cdb[2] = UFSTP_WRITE_BUFFER_ID;
	put_unaligned_be24(req->buf_size, cdb + 6);

	sdev = ufsf->sdev_ufs_lu[req->lun];
	if (!sdev) {
		ERR_MSG("Cannot find sdev [%d]", req->lun);
		return -ENODEV;
	}

	for (retries = 0; retries < 3; retries++) {
		ret = scsi_execute_cmd(sdev, cdb, REQ_OP_DRV_OUT, req->buf,
				       req->buf_size, msecs_to_jiffies(30000),
				       0, &args);
		if (ret)
			ERR_MSG("WRITE BUFFER for TP failed. (%d) retries %d",
				ret, retries);
		else
			break;
	}

	INFO_MSG("WRITE BUFFER for TP %s", ret ? "failed" : "success");

	if (ret) {
		ERR_MSG("code %x sense_key %x asc %x ascq %x",
			sshdr.response_code,
			sshdr.sense_key, sshdr.asc, sshdr.ascq);
		ERR_MSG("byte4 %x byte5 %x byte6 %x additional_len %x",
			sshdr.byte4, sshdr.byte5,
			sshdr.byte6, sshdr.additional_length);
	}

	return ret;
}

static int ufstp_issue_read_buffer(struct ufstp_dev *tp)
{
	struct rb_req *req = &tp->rb;
	struct ufsf_feature *ufsf = tp->ufsf;
	unsigned char cdb[10] = {0};
	struct scsi_device *sdev;
	struct scsi_sense_hdr sshdr;
	int ret = 0, retries;
	const struct scsi_exec_args args = {
		.sshdr = &sshdr,
	};

	if (!req->allocation_len) {
		ERR_MSG("Parameter allocation length is 0");
		return -EINVAL;
	}

	cdb[0] = READ_BUFFER;
	cdb[1] = UFSTP_READ_BUFFER_MODE;
	cdb[2] = UFSTP_READ_BUFFER_ID;
	put_unaligned_be24(req->allocation_len, cdb + 6);

	sdev = ufsf->sdev_ufs_lu[req->lun];
	if (!sdev) {
		ERR_MSG("Cannot find sdev [%d]", req->lun);
		return -ENODEV;
	}

	for (retries = 0; retries < 3; retries++) {
		ret = scsi_execute_cmd(sdev, cdb, REQ_OP_DRV_IN, req->buf,
				       req->allocation_len,
				       msecs_to_jiffies(30000), 0, &args);
		if (ret)
			ERR_MSG("READ BUFFER for TP failed. (%d) retries %d",
				ret, retries);
		else
			break;
	}

	INFO_MSG("READ BUFFER for TP %s", ret ? "failed" : "success");

	if (ret) {
		ERR_MSG("code %x sense_key %x asc %x ascq %x",
			sshdr.response_code,
			sshdr.sense_key, sshdr.asc, sshdr.ascq);
		ERR_MSG("byte4 %x byte5 %x byte6 %x additional_len %x",
			sshdr.byte4, sshdr.byte5,
			sshdr.byte6, sshdr.additional_length);
	}

	return ret;
}

static ssize_t ufstp_sysfs_show_tp_enable(struct ufstp_dev *tp, char *buf)
{
	struct ufs_hba *hba = tp->ufsf->hba;
	int ret;
	u8 idn;

	if (ufstp_get_state(tp->ufsf) != TP_PRESENT)
		return -ENODEV;

	idn = ufstp_get_idn(QUERY_FLAG_IDN_TP_EN_IDX);

	ufshcd_rpm_get_sync(hba);
	ret = ufstp_read_flag(tp, idn, &tp->tp_enable);
	ufshcd_rpm_put_sync(hba);
	if (ret)
		return -ENODEV;

	return sysfs_emit(buf, "%u\n", tp->tp_enable);
}

static ssize_t ufstp_sysfs_store_tp_enable(struct ufstp_dev *tp,
					   const char *buf, size_t count)
{
	struct ufs_hba *hba = tp->ufsf->hba;
	bool val;
	ssize_t ret = count;
	u8 idn;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	if (ufstp_get_state(tp->ufsf) != TP_PRESENT)
		return -ENODEV;

	idn = ufstp_get_idn(QUERY_FLAG_IDN_TP_EN_IDX);

	ufshcd_rpm_get_sync(hba);
	if (val) {
		if (ufstp_set_flag(tp, idn, &tp->tp_enable))
			ret = -ENODEV;
	} else {
		if (ufstp_clear_flag(tp, idn, &tp->tp_enable))
			ret = -ENODEV;
	}
	ufshcd_rpm_put_sync(hba);

	return ret;
}

static ssize_t ufstp_sysfs_show_available_tp_size(struct ufstp_dev *tp,
						  char *buf)
{
	struct ufs_hba *hba = tp->ufsf->hba;
	int ret;
	u8 idn;

	if (ufstp_get_state(tp->ufsf) != TP_PRESENT)
		return -ENODEV;

	idn = ufstp_get_idn(QUERY_ATTR_IDN_TP_AVAIL_SZ_IDX);

	ufshcd_rpm_get_sync(hba);
	ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
				      idn, 0, 0, &tp->tp_size);
	ufshcd_rpm_put_sync(hba);
	if (ret)
		return -ENODEV;

	return sysfs_emit(buf, "%u\n", tp->tp_size);
}

static ssize_t ufstp_sysfs_show_max_inodes(struct ufstp_dev *tp, char *buf)
{
	return sysfs_emit(buf, "%d\n", tp->max_inodes);
}

static ssize_t ufstp_sysfs_store_max_inodes(struct ufstp_dev *tp,
					    const char *buf, size_t count)
{
	unsigned int val;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val > TP_MAX_INODES || val < atomic_read(&tp->ino_cnt))
		return -EINVAL;

	tp->max_inodes = val;

	return count;
}

static int ufstp_ino_add(struct ufstp_dev *tp, unsigned long ino)
{
	struct ufstp_ino *n;

	n = kmalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	n->ino = ino;

	hash_add_rcu(tp->ino_ht, &n->hnode, ino);
	atomic_inc(&tp->ino_cnt);

	return 0;
}

static ssize_t ufstp_sysfs_store_add_inode(struct ufstp_dev *tp,
					   const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (atomic_read(&tp->ino_cnt) >= tp->max_inodes) {
		INFO_MSG("inode list full (%u/%u)", atomic_read(&tp->ino_cnt),
			 tp->max_inodes);
		return -EINVAL;
	}

	ret = ufstp_ino_add(tp, val);
	if (ret)
		return ret;

	return count;
}

static int ufstp_ino_del(struct ufstp_dev *tp, unsigned long ino)
{
	struct ufstp_ino *n;

	hash_for_each_possible(tp->ino_ht, n, hnode, ino) {
		if (n->ino == ino) {
			hash_del_rcu(&n->hnode);
			atomic_dec(&tp->ino_cnt);
			kfree_rcu(n, rcu);

			return 0;
		}
	}
	return -ENOENT;
}

static ssize_t ufstp_sysfs_store_del_inode(struct ufstp_dev *tp,
					   const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (!ufstp_ino_exists(tp, val))
		return -EINVAL;

	ret = ufstp_ino_del(tp, val);
	if (ret)
		return ret;

	return count;
}

#define ufstp_sysfs_attr_ro(_name) __ATTR(_name, 0444,\
					  ufstp_sysfs_show_##_name, NULL)
#define ufstp_sysfs_attr_wo(_name) __ATTR(_name, 0200,\
					  NULL, ufstp_sysfs_store_##_name)
#define ufstp_sysfs_attr_rw(_name) __ATTR(_name, 0644,\
					  ufstp_sysfs_show_##_name,\
					  ufstp_sysfs_store_##_name)

static struct ufstp_sysfs_entry ufstp_sysfs_entries[] = {
	/* Flag */
	ufstp_sysfs_attr_rw(tp_enable),
	/* Attribute */
	ufstp_sysfs_attr_ro(available_tp_size),

	/* For TurboPocket In */
	ufstp_sysfs_attr_rw(max_inodes),
	ufstp_sysfs_attr_wo(add_inode),
	ufstp_sysfs_attr_wo(del_inode),
	__ATTR_NULL,
};

static ssize_t ufstp_attr_show(struct kobject *kobj, struct attribute *attr,
			       char *page)
{
	struct ufstp_sysfs_entry *entry;
	struct ufstp_dev *tp;
	ssize_t error;

	entry = container_of(attr, struct ufstp_sysfs_entry, attr);
	if (!entry->show)
		return -EIO;

	tp = container_of(kobj, struct ufstp_dev, kobj);

	mutex_lock(&tp->sysfs_lock);
	error = entry->show(tp, page);
	mutex_unlock(&tp->sysfs_lock);
	return error;
}

static ssize_t ufstp_attr_store(struct kobject *kobj, struct attribute *attr,
				const char *page, size_t length)
{
	struct ufstp_sysfs_entry *entry;
	struct ufstp_dev *tp;
	ssize_t error;

	entry = container_of(attr, struct ufstp_sysfs_entry, attr);
	if (!entry->store)
		return -EIO;

	tp = container_of(kobj, struct ufstp_dev, kobj);

	mutex_lock(&tp->sysfs_lock);
	error = entry->store(tp, page, length);
	mutex_unlock(&tp->sysfs_lock);
	return error;
}

static const struct sysfs_ops ufstp_sysfs_ops = {
	.show = ufstp_attr_show,
	.store = ufstp_attr_store,
};

static struct kobj_type ufstp_ktype = {
	.sysfs_ops = &ufstp_sysfs_ops,
	.release = NULL,
};

static int ufstp_create_sysfs(struct ufsf_feature *ufsf, struct ufstp_dev *tp)
{
	struct device *dev = ufsf->hba->dev;
	struct ufstp_sysfs_entry *entry;
	int err;

	tp->sysfs_entries = ufstp_sysfs_entries;

	kobject_init(&tp->kobj, &ufstp_ktype);
	mutex_init(&tp->sysfs_lock);

	INFO_MSG("ufstp creates sysfs ufstp %p dev->kobj %p",
		 &tp->kobj, &dev->kobj);

	err = kobject_add(&tp->kobj, kobject_get(&dev->kobj), "ufstp");
	if (!err) {
		for (entry = tp->sysfs_entries; entry->attr.name != NULL;
		     entry++) {
			INFO_MSG("ufstp sysfs attr creates: %s",
				 entry->attr.name);

			err = sysfs_create_file(&tp->kobj, &entry->attr);
			if (err) {
				ERR_MSG("create entry(%s) failed",
					entry->attr.name);
				goto kobj_del;
			}
		}
		kobject_uevent(&tp->kobj, KOBJ_ADD);
	} else {
		ERR_MSG("kobject_add failed");
	}

	return err;

kobj_del:
	err = kobject_uevent(&tp->kobj, KOBJ_REMOVE);
	INFO_MSG("kobject removed (%d)", err);
	kobject_del(&tp->kobj);
	kobject_put(&tp->kobj);

	return -EINVAL;
}

static inline void ufstp_remove_sysfs(struct ufstp_dev *tp)
{
	int ret;

	ret = kobject_uevent(&tp->kobj, KOBJ_REMOVE);
	INFO_MSG("kobject removed (%d)", ret);
	kobject_del(&tp->kobj);
	kobject_put(&tp->kobj);
}

static inline void ufstp_remove_procfs(struct ufstp_dev *tp)
{
	struct proc_dir_entry *tp_proc_root = tp->tp_proc_root;

	if (tp_proc_root) {
		remove_proc_entry("retrieve_cell_type", tp_proc_root);
		remove_proc_entry("inode_list", tp_proc_root);
		remove_proc_entry("ufstp", NULL);
		tp->tp_proc_root = NULL;
		INFO_MSG("procfs is removed");
	}
}

static void ufstp_inode_hash_purge(struct ufstp_dev *tp)
{
	struct ufstp_ino *n;
	struct hlist_node *tmp;
	int bkt;

	mutex_lock(&tp->sysfs_lock);
	hash_for_each_safe(tp->ino_ht, bkt, tmp, n, hnode) {
		hash_del_rcu(&n->hnode);
		kfree_rcu(n, rcu);
	}

	atomic_set(&tp->ino_cnt, 0);
	mutex_unlock(&tp->sysfs_lock);

	rcu_barrier();
}

void ufstp_remove(struct ufsf_feature *ufsf)
{
	struct ufstp_dev *tp = ufsf->tp_dev;

	if (!tp)
		return;

	INFO_MSG("Start release");

	ufstp_set_state(ufsf, TP_FAILED);
	ufstp_inode_hash_purge(tp);
	ufstp_remove_sysfs(tp);
	ufstp_remove_procfs(tp);

	kfree(tp);

	ufsf->tp_dev = NULL;

	INFO_MSG("End release");
}

static void ufstp_show_msg(struct ufsf_feature *ufsf, struct seq_file *file,
			   void *buf)
{
	struct ufstp_dev *tp = ufsf->tp_dev;
	int buffer_size = tp->rb.allocation_len;
	int i, j;
	const char *p = buf;
	int byte_count = 0;

	INFO_MSG("Buffer size %d", buffer_size);

	for (i = 0; i < buffer_size; i++) {
		for (j = 7; j >= 0; j--) {
			if (((p[i] >> j) & 1) == 0)
				seq_printf(file, "X");
			else
				seq_printf(file, "O");
		}

		byte_count++;

		if (byte_count == 4) {
			seq_printf(file, "\n");
			byte_count = 0;
		} else {
			seq_printf(file, " ");
		}
	}

	if (byte_count != 0)
		seq_printf(file, "\n");
}

static int ufstp_proc_print_show(struct seq_file *file, void *data)
{
	struct ufsf_feature *ufsf = (struct ufsf_feature *)file->private;

	INFO_MSG("Print...");

	ufstp_show_msg(ufsf, file, &ufsf->tp_dev->rb.buf);

	return 0;
}

static int ufstp_proc_print_open(struct inode *inode, struct file *file)
{
	struct ufsf_feature *ufsf = (struct ufsf_feature *)pde_data(inode);
	struct ufstp_dev *tp = ufsf->tp_dev;
	struct ufs_hba *hba = ufsf->hba;
	int ret;

	if (ufstp_get_state(tp->ufsf) != TP_PRESENT)
		return -ENODEV;

	mutex_lock(&tp->sysfs_lock);

	ufshcd_rpm_get_sync(hba);
	ret = ufstp_issue_write_buffer(tp);
	if (ret)
		goto end;

	ret = ufstp_issue_read_buffer(tp);
	if (ret)
		ERR_MSG("READ BUFFER failed. Check response code. (%d)", ret);

end:
	ufshcd_rpm_put_sync(hba);

	mutex_unlock(&tp->sysfs_lock);

	return ret ? ret : single_open(file, ufstp_proc_print_show,
				       pde_data(inode));
}

static int ufstp_proc_inode_list_show(struct seq_file *file, void *data)
{
	struct ufsf_feature *ufsf = (struct ufsf_feature *)file->private;
	struct ufstp_dev *tp = ufsf->tp_dev;
	struct ufstp_ino *n;
	int bkt;

	INFO_MSG("Print...");

	rcu_read_lock();
	hash_for_each_rcu(tp->ino_ht, bkt, n, hnode)
		seq_printf(file, "%lu\n", n->ino);
	rcu_read_unlock();

	return 0;
}

static int ufstp_proc_inode_list_open(struct inode *inode, struct file *file)
{
	struct ufsf_feature *ufsf = (struct ufsf_feature *)pde_data(inode);

	if (ufstp_get_state(ufsf) != TP_PRESENT)
		return -ENODEV;

	return single_open(file, ufstp_proc_inode_list_show, pde_data(inode));
}

static const struct proc_ops fops_proc_print = {
	.proc_open = ufstp_proc_print_open,
	.proc_read = seq_read,
	.proc_release = single_release,
};

static const struct proc_ops fops_proc_inode_list = {
	.proc_open = ufstp_proc_inode_list_open,
	.proc_read = seq_read,
	.proc_release = single_release,
};

static int ufstp_create_procfs(struct ufsf_feature *ufsf)
{
	struct proc_dir_entry *tp_proc_root;

	tp_proc_root = proc_mkdir("ufstp", NULL);
	if (!tp_proc_root) {
		ERR_MSG("ufstp directory creation failure");
		return -ENODEV;
	}

	proc_create_data("retrieve_cell_type", 0444, tp_proc_root,
			 &fops_proc_print, ufsf);
	proc_create_data("inode_list", 0444, tp_proc_root,
			 &fops_proc_inode_list, ufsf);

	ufsf->tp_dev->tp_proc_root = tp_proc_root;

	return 0;
}

void ufstp_init(struct ufsf_feature *ufsf)
{
	struct ufstp_dev *tp = ufsf->tp_dev;
	int ret;

	if (!tp) {
		ufstp_set_state(ufsf, TP_FAILED);
		return;
	}

	tp->max_inodes = TP_DEFAULT_INODES;
	hash_init(tp->ino_ht);
	atomic_set(&tp->ino_cnt, 0);

	ret = ufstp_create_sysfs(ufsf, tp);
	if (ret) {
		ERR_MSG("TurboPocket initialization failed");
		kfree(tp);
		ufstp_set_state(ufsf, TP_FAILED);
		return;
	}

	ret = ufstp_create_procfs(ufsf);
	if (ret) {
		ufstp_remove_sysfs(tp);
		kfree(tp);
		ufstp_set_state(ufsf, TP_FAILED);
	}

	INFO_MSG("UFS TP create sysfs finished");

	ufstp_set_state(ufsf, TP_PRESENT);
}

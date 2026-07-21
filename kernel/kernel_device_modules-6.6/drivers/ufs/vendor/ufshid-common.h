/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Universal Flash Storage Host Initiated Defrag - Common Definitions
 *
 * Copyright (C) 2019 Samsung Electronics Co., Ltd.
 */

#ifndef _UFSHID_COMMON_H_
#define _UFSHID_COMMON_H_

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>

/* Common HID constants */
#define HID_TRIGGER_WORKER_DELAY_MS_DEFAULT	2000
#define HID_TRIGGER_WORKER_DELAY_MS_MIN		100
#define HID_TRIGGER_WORKER_DELAY_MS_MAX		10000

#define HID_SIZE_DEFAULT			0xFFFFFFFF
#define HID_SIZE_UNIT				4096
#define KB_PER_HID_SIZE_UNIT			(HID_SIZE_UNIT / 1024)

#define RESULT_NOT_DEFRAG_REQUIRED		1

#define WAIT_HID_RESUME_TIMEOUT			(2 * HZ)

#define SPM_ACTIVE_POWER_LEVEL			1

/* SYSFS helper macros */
#define define_sysfs_ro(_name) __ATTR(_name, 0444,			\
				      ufshid_sysfs_show_##_name, NULL)
#define define_sysfs_rw(_name) __ATTR(_name, 0644,			\
				      ufshid_sysfs_show_##_name,	\
				      ufshid_sysfs_store_##_name)

#define HID_DEBUG(hid, msg, args...)					\
	do { if (hid->hid_debug)					\
		pr_err("%40s:%3d [%01d%02d%02d] " msg "\n",		\
		       __func__, __LINE__,				\
		       hid->hid_trigger,				\
		       atomic_read(&hid->ufsf->hba->dev->power.usage_count),\
		       hid->ufsf->hba->clk_gating.active_reqs, ##args);	\
	} while (0)

/* Common HID state definitions */
enum UFSHID_STATE {
	HID_NEED_INIT = 0,
	HID_PRESENT = 1,
	HID_SUSPEND = 2,
	HID_FAILED = -2,
	HID_RESET = -3,
};

/* Common HID device state definitions */
enum UFSHID_DEV_STATE {
	HID_ANALYSIS_REQUIRED		= 0x0,
	HID_ANALYSIS_IN_PROGRESS	= 0x1,
	HID_DEFRAG_REQUIRED		= 0x2,
	HID_DEFRAG_IN_PROGRESS		= 0x3,
	HID_DEFRAG_COMPLETION		= 0x4,
	HID_DEFRAG_IS_NOT_REQUIRED	= 0x5,
	HID_NUM_DEV_STATES		= 0x6,
};

/* Common HID operation enum - with version-specific extensions */
enum UFSHID_OP {
	HID_OP_DISABLE		= 0,
	HID_OP_ANALYZE		= 1,
	HID_OP_EXECUTE		= 2,
#ifdef CONFIG_UFSHID
	HID_OP_LBA_EXECUTE	= 3,
#endif
	HID_OP_MAX
};

/* Forward declarations */
struct ufsf_feature;
struct ufshcd_lrb;

/* Common offset structure */
struct ufshid_offset {
	u8 offset;
};

/* V7 specific structures */
enum ufshid_v7_param {
	HID_NO_PARAM	= 0,
	HID_WITH_PARAM	= 1,
};

enum ufshid_v7_level {
	HID_LEV_GRAY	= 0,
	HID_LEV_GREEN	= 1,
	HID_LEV_YELLOW	= 2,
	HID_LEV_RED	= 3,
	HID_LEV_UNKNOWN	= 4,
};

struct ufshid_blk_desc {
	__be32 lba;
	__be32 blk_cnt;
} __packed;

struct ufshid_blk_desc_header {
	__u8 hid_blk_desc_cnt;
	__u8 reserved[7];
};

struct ufshid_req {
	int lun;
	u8 buf[PAGE_SIZE];
	size_t buf_size;
};

/* Unified ufshid_dev structure - contains all fields from both versions */
struct ufshid_dev {
	struct ufsf_feature *ufsf;

	/* Common fields */
	unsigned int hid_trigger;
	struct delayed_work hid_trigger_work;
	unsigned int hid_trigger_delay;

	u32 ahit;
	bool is_auto_enabled;

	u32 hid_size;

	/* V7 specific fields */
	struct ufshid_req hid_req;
	bool lba_trigger_mode;
	u32 max_lba_range_size;
	u8 max_lba_range_cnt;
	bool l2p_defrag_sup;
	u8 l2p_defrag_threshold;

	/* JEDEC specific fields */
	bool is_analyze;

	/* for sysfs */
	struct kobject kobj;
	struct mutex sysfs_lock;
	struct ufshid_sysfs_entry *sysfs_entries;

	/* for debug */
	bool hid_debug;
#if defined(CONFIG_UFSHID_POC) || defined(CONFIG_UFS_JEDEC_HID_POC)
	bool block_suspend;
#endif
	struct completion resume_compl;
};

/* Common sysfs entry structure */
struct ufshid_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct ufshid_dev *hid, char *buf);
	ssize_t (*store)(struct ufshid_dev *hid, const char *buf, size_t count);
};

/* V7 specific function declarations */
void ufshid_v7_get_dev_info(struct ufsf_feature *ufsf, u8 *desc_buf);
void ufshid_v7_get_geo_info(struct ufsf_feature *ufsf, u8 *geo_buf);
void ufshid_v7_init(struct ufsf_feature *ufsf);
void ufshid_v7_reset(struct ufsf_feature *ufsf);
void ufshid_v7_remove(struct ufsf_feature *ufsf);
void ufshid_v7_suspend(struct ufsf_feature *ufsf, bool is_system_pm);
void ufshid_v7_resume(struct ufsf_feature *ufsf, bool is_link_off);
void ufshid_v7_prep_fn(struct ufsf_feature *ufsf, struct ufshcd_lrb *lrbp);

/* JEDEC specific function declarations */
void ufshid_jedec_get_dev_info(struct ufsf_feature *ufsf);
void ufshid_jedec_init(struct ufsf_feature *ufsf);
void ufshid_jedec_reset(struct ufsf_feature *ufsf);
void ufshid_jedec_remove(struct ufsf_feature *ufsf);
void ufshid_jedec_suspend(struct ufsf_feature *ufsf, bool is_system_pm);
void ufshid_jedec_resume(struct ufsf_feature *ufsf);

#endif /* _UFSHID_COMMON_H_ */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025-2026 Oplus. All rights reserved.
 */
#include <linux/blk_types.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <trace/hooks/sd.h>
#include <scsi/scsi_proto.h>
#include <scsi/scsi_cmnd.h>
#include <asm-generic/unaligned.h>
#include "drivers/scsi/sd.h"

#define CREATE_TRACE_POINTS

#define UFS_FEATURE_DIR "ufs_feature"
#define FASTDISCARD_DIR "fastdiscard_en"
#define MULTI_DISCARD_SEGMENTS_DIR "dev_max_discard_segments"

static bool fastdiscard_en = 0;
static unsigned int dev_max_discard_segments = 1;

static ssize_t fastdiscard_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *ppos)
{
	int ret = 0;

	ret = kstrtobool_from_user(buffer, count, &fastdiscard_en);
	if (ret)
		pr_err("update fastdiscard status failed!\n");

	fastdiscard_en = !!fastdiscard_en;

	return count;
}

static int fastdiscard_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", fastdiscard_en);

	return 0;
}

static int fastdiscard_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, fastdiscard_proc_show, NULL);
}

static const struct proc_ops fastdiscard_proc_ops = {
	.proc_open	= fastdiscard_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= fastdiscard_proc_write,
};

static int multi_discard_segments_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", dev_max_discard_segments);

	return 0;
}

static int multi_discard_segments_open(struct inode *inode, struct file *file)
{
	return single_open(file, multi_discard_segments_show, NULL);
}

static const struct proc_ops multi_discard_segments_proc_ops = {
	.proc_open	= multi_discard_segments_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int __init proc_oplus_ufs_fastdiscard_init(void)
{
	struct proc_dir_entry *oplus_ufs_feature, *oplus_fastdiscard,
		*oplus_multi_discard_segments;
	int ret = 0;

	oplus_ufs_feature = proc_mkdir(UFS_FEATURE_DIR, NULL);
	if (!oplus_ufs_feature) {
		pr_err(" Failed to create oplus_ufs_feature procfs\n");
		return -EFAULT;
	}

	oplus_fastdiscard = proc_create(FASTDISCARD_DIR, 0644,
		oplus_ufs_feature, &fastdiscard_proc_ops);
	if (!oplus_fastdiscard) {
		remove_proc_entry(UFS_FEATURE_DIR, NULL);
		pr_err(" Failed to create oplus_fastdiscard enable procfs\n");
		return -EFAULT;
	}

	oplus_multi_discard_segments = proc_create(MULTI_DISCARD_SEGMENTS_DIR, 0444,
		oplus_ufs_feature, &multi_discard_segments_proc_ops);
	if (!oplus_multi_discard_segments) {
		remove_proc_subtree(UFS_FEATURE_DIR, NULL);
		pr_err(" Failed to create oplus_multi_discard_segments procfs\n");
		return -EFAULT;
	}

	return ret;
}
static void sd_vh_setup_unmap_multi_segment(void *data,
	struct scsi_cmnd *cmd, char *buf)
{
	struct request *rq = scsi_cmd_to_rq(cmd);
	struct request_queue *q = rq->q;
	unsigned short segments = blk_rq_nr_discard_segments(rq);
	struct scsi_device *sdp = cmd->device;
	struct scsi_vpd *vpd;

	if (segments > 1) {
		unsigned int data_len = 8 + 16 * segments;
		struct bio *bio;
		unsigned int descriptor_offset = 8;
		u64 lba = sectors_to_logical(sdp, blk_rq_pos(rq));
		u32 nr_blocks = sectors_to_logical(sdp, blk_rq_sectors(rq));

		put_unaligned_be16(data_len, &cmd->cmnd[7]);

		/* update data len */
		rq->special_vec.bv_len = data_len;
		put_unaligned_be16(6 + 16 * segments, &buf[0]);
		put_unaligned_be16(16 * segments, &buf[2]);

		__rq_for_each_bio(bio, rq) {
			lba = sectors_to_logical(sdp, bio->bi_iter.bi_sector);
			nr_blocks = sectors_to_logical(sdp, bio_sectors(bio));

			put_unaligned_be64(lba, &buf[descriptor_offset]);
			put_unaligned_be32(nr_blocks, &buf[descriptor_offset + 8]);
			descriptor_offset += 16;
		}
	}

	if (fastdiscard_en == 0) {
		/* disable fastdiscard, it need to set max discard segments to 1 */
		if (queue_max_discard_segments(q) > 1)
			blk_queue_max_discard_segments(q, 1);
	} else if (fastdiscard_en == 1 && queue_max_discard_segments(q) == 1) {
		rcu_read_lock();
		vpd = rcu_dereference(sdp->vpd_pgb0);
		if (dev_max_discard_segments != get_unaligned_be32(&vpd->data[24])) {
			WARN_ON(1);
			dev_max_discard_segments = get_unaligned_be32(&vpd->data[24]);
		}
		rcu_read_unlock();
		if (dev_max_discard_segments != 1)
			blk_queue_max_discard_segments(q, dev_max_discard_segments);
	}
}

static void sd_vh_init_unmap_multi_segment(void *data,
	struct scsi_disk *sdkp, struct scsi_vpd *vpd)
{
	unsigned int max_unmap_block_desc_count =
		get_unaligned_be32(&vpd->data[24]);

	if (max_unmap_block_desc_count > 1)
		dev_max_discard_segments = max_unmap_block_desc_count;
}

static int __init oplus_ufs_fastdiscard_init(void)
{
	int ret = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	ret = proc_oplus_ufs_fastdiscard_init();
	if (ret) {
		pr_err("%s failed, ret = %d", __func__, ret);
		goto exit;
	}

	ret = register_trace_android_vh_sd_setup_unmap_multi_segment(
			sd_vh_setup_unmap_multi_segment, NULL);
	if (ret < 0) {
		pr_err("register_trace_android_vh_sd_setup_unmap_multi_segment failed, ret = %d",
			ret);
		goto exit;
	}

	ret = register_trace_android_vh_sd_init_unmap_multi_segment(
			sd_vh_init_unmap_multi_segment, NULL);
	if (ret < 0) {
		unregister_trace_android_vh_sd_setup_unmap_multi_segment(
			sd_vh_setup_unmap_multi_segment, NULL);
		pr_err("register_trace_android_vh_sd_init_unmap_multi_segment failed, ret = %d",
			ret);
		goto exit;
	}

	pr_info("oplus_ufs_fastdiscard init succeed\n");

exit:
#else
	pr_info("kernel version < 6.6, no need to init oplus_ufs_fastdiscard\n");
#endif
	return ret;
}

static void __exit oplus_ufs_fastdiscard_exit(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	remove_proc_subtree(UFS_FEATURE_DIR, NULL);
	unregister_trace_android_vh_sd_setup_unmap_multi_segment(
		sd_vh_setup_unmap_multi_segment, NULL);
	unregister_trace_android_vh_sd_init_unmap_multi_segment(
		sd_vh_init_unmap_multi_segment, NULL);

	pr_info("oplus_ufs_fastdiscard exit succeed\n");
#endif
}

module_init(oplus_ufs_fastdiscard_init);
module_exit(oplus_ufs_fastdiscard_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("liuderong");
MODULE_DESCRIPTION("Used to support ufs feature:FastDiscard");

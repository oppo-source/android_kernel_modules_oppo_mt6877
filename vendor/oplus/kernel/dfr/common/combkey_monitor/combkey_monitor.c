// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2030 Oplus. All rights reserved.
 * Description : combination key monitor, such as volup + pwrkey
 * Version : 1.0
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": %s: %d: " fmt, __func__, __LINE__

#include <linux/types.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/input.h>
#include <linux/ktime.h>
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_KEYEVENT_HANDLER)
#include "../../include/keyevent_handler.h"
#endif
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_THEIA)
#include "../../include/theia_send_event.h"
#include "../../include/theia_bright_black_check.h"
#endif

#define CREATE_TRACE_POINTS
#include "combkey_trace.h"
#include <linux/time.h>

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FEEDBACK)
#include <soc/oplus/dft/kernel_fb.h>
#endif

#include <linux/version.h>
#include <linux/blkdev.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/slab.h>


#define KEY_DOWN_VALUE 1
#define KEY_UP_VALUE 0

static struct delayed_work g_check_combkey_long_press_work;
static struct delayed_work g_check_pwrkey_long_press_work;
static struct delayed_work g_clear_combkey_long_press_flag_work;
#define CHECK_COMBKEY_LONG_PRESS_MS 6000
#define CHECK_PWRKEY_LONG_PRESS_MS 8000
#define CLEAR_COMBKEY_LONG_PRESS_FLAG_MS 1000

#define SYSTEM_ID 20120
#define COMBKEY_DCS_TAG      "CriticalLog"
#define COMBKEY_DCS_EVENTID  "Theia"
#define PWRKEY_LONG_PRESS    "TheiaPwkLongPress"
#define BUTTON_DEBOUNCE_TYPE "10001"

#define OPLUS_RESERVE1_ABNORMAL_REBOOT_UFS_START  (1303 * 4096)
#define OPLUS_RESERVE1_COMBKEY_LONG_PRESS_START (OPLUS_RESERVE1_ABNORMAL_REBOOT_UFS_START + 40)
#define TARGET_DEV_BLOCK "/dev/block/by-name/oplusreserve1"

static int g_combkey_long_press_flag = 0;
static bool is_pwrkey_down;
static bool is_volumup_down;
static bool is_volumup_pwrkey_down;
static u64 pwrkey_press_count;
static u64 volup_press_count;

struct key_press_time_data {
	u64 curr_down_time;
	u64 curr_up_time;
	u64 key_handle_interval;
};

static unsigned int combkey_monitor_events[] = {
	KEY_POWER,
	KEY_VOLUMEUP,
	KEY_VOLUMEDOWN,
	BTN_TRIGGER_HAPPY32,
};
static size_t combkey_monitor_events_nr = ARRAY_SIZE(combkey_monitor_events);


#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0)
static int blkdev_fsync(struct file *filp, loff_t start, loff_t end,
		int datasync)
{
	struct inode *bd_inode = filp->f_mapping->host;
	struct block_device *bdev = I_BDEV(bd_inode);
	int error;

	error = file_write_and_wait_range(filp, start, end);
	if (error)
		return error;

	/*
	 * There is no need to serialise calls to blkdev_issue_flush with
	 * i_mutex and doing so causes performance issues with concurrent
	 * O_SYNC writers to a block device.
	 */
	error = blkdev_issue_flush(bdev);
	if (error == -EOPNOTSUPP)
		error = 0;

	return error;
}
#endif

static int record_combkey_long_press_flag(unsigned int flag)
{
	unsigned int combkey_long_press = flag;
	int ret = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
	struct block_device *bdev = NULL;
#endif
	struct kiocb kiocb;
	struct iov_iter iter;
	struct kvec iov;
	const struct file_operations f_op = {.fsync = blkdev_fsync};
	struct file *dev_map_file = NULL;

	dev_map_file = (struct file *)kmalloc(sizeof(*dev_map_file), GFP_ATOMIC);
	if (NULL != dev_map_file) {
		memset(dev_map_file, 0, sizeof(struct file));
	} else {
		pr_err("Failed to kmalloc dev_map_file\n");
		return -1;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	dev_map_file = bdev_file_open_by_path(TARGET_DEV_BLOCK, FMODE_READ | FMODE_WRITE, NULL, NULL);
	if (IS_ERR(dev_map_file)) {
		pr_err("Failed to get dev_map_file\n");
		kfree(dev_map_file);
		return -1;
	}
#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
	bdev = blkdev_get_by_path(TARGET_DEV_BLOCK, FMODE_READ | FMODE_WRITE, NULL);
#else
	bdev = blkdev_get_by_path(TARGET_DEV_BLOCK, FMODE_READ | FMODE_WRITE, NULL, NULL);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
	if (IS_ERR(bdev)) {
		pr_err("Failed to get dev block\n");
		kfree(dev_map_file);
		return -1;
	}

	dev_map_file->f_mapping = bdev->bd_inode->i_mapping;
	dev_map_file->f_flags = O_DSYNC | __O_SYNC | O_NOATIME;
	dev_map_file->f_inode = bdev->bd_inode;
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 1, 0)
	dev_map_file->f_iocb_flags = IOCB_DSYNC;
#endif
#endif

	init_sync_kiocb(&kiocb, dev_map_file);
	kiocb.ki_pos = OPLUS_RESERVE1_COMBKEY_LONG_PRESS_START;
	iov.iov_base = &combkey_long_press;
	iov.iov_len = sizeof(combkey_long_press);
	iov_iter_kvec(&iter, WRITE, &iov, 1, sizeof(combkey_long_press));

	ret = generic_write_checks(&kiocb, &iter);
	if (likely(ret > 0)) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 1, 0)
		ret = generic_perform_write(&kiocb, &iter);
#else
		ret = generic_perform_write(dev_map_file, &iter, kiocb.ki_pos);
#endif
	} else {
		pr_err("generic_write_checks failed\n");
		kfree(dev_map_file);
		return 1;
	}

	if (likely(ret > 0)) {
		dev_map_file->f_op = &f_op;
		kiocb.ki_pos += ret;
		pr_err("Write back size %d\n", ret);
		ret = generic_write_sync(&kiocb, ret);
		if (ret < 0) {
			pr_err("Write sync failed\n");
			kfree(dev_map_file);
			return 1;
		}
	} else {
		pr_err("generic_perform_write failed\n");
		kfree(dev_map_file);
		return 1;
	}

	if (combkey_long_press == 1) {
		pr_info("record_combkey_long_press_flag success.\n");
		g_combkey_long_press_flag = 1;
	}

	kfree(dev_map_file);
	return 0;
}

static void combkey_long_press_callback(struct work_struct *work)
{
	pr_info("called. send pwr_resin_bark to theia.\n");
	theia_send_event(THEIA_EVENT_KPDPWR_RESIN_BARK, THEIA_LOGINFO_KERNEL_LOG | THEIA_LOGINFO_ANDROID_LOG,
		0, "kpdpwr_resin_bark happen");
	record_combkey_long_press_flag(1);
}

static void clear_combkey_long_press_flag_callback(struct work_struct *work)
{
	if (g_combkey_long_press_flag == 1) {
		pr_info("g_combkey_long_press_flag is 1, need clean\n");
		record_combkey_long_press_flag(0);
	} else {
		pr_info("g_combkey_long_press_flag is 0, do nothing\n");
	}
}

static long get_timestamp_ms(void)
{
	struct timespec64 now;
	ktime_get_real_ts64(&now);
	return timespec64_to_ns(&now) / NSEC_PER_MSEC;
}

static void pwrkey_long_press_callback(struct work_struct *work)
{
	pr_info("called. send long press pwrkey to theia.\n");
	/*
	theia_send_event(THEIA_EVENT_PWK_LONGPRESS, THEIA_LOGINFO_KERNEL_LOG
		 | THEIA_LOGINFO_ANDROID_LOG | THEIA_LOGINFO_DUMPSYS_SF | THEIA_LOGINFO_BINDER_INFO,
		0, "pwrkey long press happen");
	*/
	trace_combkey_monitor(get_timestamp_ms(), SYSTEM_ID, COMBKEY_DCS_TAG, COMBKEY_DCS_EVENTID, PWRKEY_LONG_PRESS);
}

static int combkey_monitor_notifier_call(struct notifier_block *nb, unsigned long type, void *data)
{
	struct keyevent_notifier_param *param = data;
	struct key_press_time_data pwr_tm_data = {
		.curr_down_time = 0,
		.curr_up_time = 0,
		.key_handle_interval = 0
	};
	struct key_press_time_data volup_tm_data = {
		.curr_down_time = 0,
		.curr_up_time = 0,
		.key_handle_interval = 0
	};
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FEEDBACK)
	char payload[1024] = {0x00};
#endif

	pr_info("called. event_code = %u, value = %d\n", param->keycode, param->down);

	if (param->keycode == KEY_POWER) {
		pr_info("pwrkey handle enter.\n");
		if (param->down == KEY_DOWN_VALUE) {
			is_pwrkey_down = true;
			set_pwk_flag(true);
			pr_info("pwrkey pressed, call pwrkey monitor checker.\n");

			pwr_tm_data.curr_down_time = ktime_to_ms(ktime_get());
			pr_info("pwrkey pressed, call pwrkey monitor checker. curr_down_time = %llu\n", pwr_tm_data.curr_down_time);
			black_screen_timer_restart();
			bright_screen_timer_restart();
		} else if (param->down == KEY_UP_VALUE) {
			is_pwrkey_down = false;
			pwr_tm_data.curr_up_time = ktime_to_ms(ktime_get());
			pwr_tm_data.key_handle_interval = pwr_tm_data.curr_up_time - pwr_tm_data.curr_down_time;
			pr_info("pwrkey key released, curr_up_time = %llu, key_handle_interval = %llu\n",
						pwr_tm_data.curr_up_time, pwr_tm_data.key_handle_interval);
			pwrkey_press_count++;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FEEDBACK)
			memset(payload, 0 , sizeof(payload));
			scnprintf(payload, sizeof(payload),
				"NULL$$EventField@@pwrkey$$FieldData@@cnt%llu$$detailData@@up_%llu,down_%llu,interval_%llu",
				pwrkey_press_count, pwr_tm_data.curr_up_time, pwr_tm_data.curr_down_time, pwr_tm_data.key_handle_interval);
			oplus_kevent_fb(FB_TRI_STATE_KEY, BUTTON_DEBOUNCE_TYPE, payload);
#endif
		}
	} else if (param->keycode == KEY_VOLUMEUP) {
		pr_info("volumup key handle enter\n");
		if (param->down == KEY_DOWN_VALUE) {
			is_volumup_down = true;
			volup_tm_data.curr_down_time = ktime_to_ms(ktime_get());
			pr_info("volumup key pressed, curr_down_time = %llu\n", volup_tm_data.curr_down_time);
		} else if (param->down == KEY_UP_VALUE) {
			is_volumup_down = false;
			volup_tm_data.curr_up_time = ktime_to_ms(ktime_get());
			volup_tm_data.key_handle_interval = volup_tm_data.curr_up_time - volup_tm_data.curr_down_time;
			pr_info("volumup key released, curr_up_time = %llu, key_handle_interval = %llu\n",
						volup_tm_data.curr_up_time, volup_tm_data.key_handle_interval);
			volup_press_count++;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FEEDBACK)
			memset(payload, 0 , sizeof(payload));
			scnprintf(payload, sizeof(payload),
				"NULL$$EventField@@volumeupkey$$FieldData@@cnt%llu$$detailData@@up_%llu,down_%llu,interval_%llu",
				volup_press_count, volup_tm_data.curr_up_time, volup_tm_data.curr_down_time, volup_tm_data.key_handle_interval);
			oplus_kevent_fb(FB_TRI_STATE_KEY, BUTTON_DEBOUNCE_TYPE, payload);
#endif
		}
	}

	/* combination key pressed, start to calculate duration */
	if (is_pwrkey_down && is_volumup_down) {
		is_volumup_pwrkey_down = true;
		pr_info("volup_pwrkey combination key pressed.\n");
		schedule_delayed_work(&g_check_combkey_long_press_work, msecs_to_jiffies(CHECK_COMBKEY_LONG_PRESS_MS));
	} else {
		if (is_volumup_pwrkey_down) {
			is_volumup_pwrkey_down = false;
			pr_info("volup_pwrkey combination key canceled.\n");
			cancel_delayed_work(&g_check_combkey_long_press_work);
		}
	}

	/* only power key pressed, start to calculate duration */
	if (is_pwrkey_down && !is_volumup_down) {
		pr_info("power key pressed.\n");
		schedule_delayed_work(&g_check_pwrkey_long_press_work, msecs_to_jiffies(CHECK_PWRKEY_LONG_PRESS_MS));
	} else {
		pr_info("power key canceled.\n");
		schedule_delayed_work(&g_clear_combkey_long_press_flag_work, msecs_to_jiffies(CLEAR_COMBKEY_LONG_PRESS_FLAG_MS));
		cancel_delayed_work(&g_check_pwrkey_long_press_work);
	}

	return NOTIFY_DONE;
}

static struct notifier_block combkey_monitor_notifier = {
	.notifier_call = combkey_monitor_notifier_call,
	.priority = 128,
};

static int __init combkey_monitor_init(void)
{
	pr_info("called.\n");
	keyevent_register_notifier(&combkey_monitor_notifier, combkey_monitor_events, combkey_monitor_events_nr);
	INIT_DELAYED_WORK(&g_check_combkey_long_press_work, combkey_long_press_callback);
	INIT_DELAYED_WORK(&g_check_pwrkey_long_press_work, pwrkey_long_press_callback);
	INIT_DELAYED_WORK(&g_clear_combkey_long_press_flag_work, clear_combkey_long_press_flag_callback);
	return 0;
}

static void __exit combkey_monitor_exit(void)
{
	pr_info("called.\n");
	cancel_delayed_work_sync(&g_check_combkey_long_press_work);
	cancel_delayed_work_sync(&g_check_pwrkey_long_press_work);
	keyevent_unregister_notifier(&combkey_monitor_notifier, combkey_monitor_events, combkey_monitor_events_nr);
}

module_init(combkey_monitor_init);
module_exit(combkey_monitor_exit);

MODULE_DESCRIPTION("oplus_combkey_monitor");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");

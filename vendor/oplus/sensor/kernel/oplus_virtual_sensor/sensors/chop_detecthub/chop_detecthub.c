#include "chop_detecthub.h"
#include "sensor_cmd.h"
#include "virtual_sensor.h"
#include <linux/notifier.h>
#include <linux/pm_wakeup.h>
#include <linux/version.h>

#define CHOP_DETECT_TAG					"[chop_detecthub] "
#define CHOP_DETECT_FUN(f)				pr_err(CHOP_DETECT_TAG"%s\n", __func__)
#define CHOP_DETECT_PR_ERR(fmt, args...)	pr_err(CHOP_DETECT_TAG"%s %d : "fmt, __func__, __LINE__, ##args)
#define CHOP_DETECT_LOG(fmt, args...)		pr_err(CHOP_DETECT_TAG fmt, ##args)

static struct virtual_sensor_init_info chop_detecthub_init_info;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static struct wakeup_source chop_wake_lock;
#else
static struct wakeup_source *chop_wake_lock = NULL;
#endif
static int chop_detect_open_report_data(int open)
{
	return 0;
}

static int chop_detect_enable_nodata(int en)
{
	CHOP_DETECT_LOG("chop_detect enable nodata, en = %d\n", en);

	return oplus_enable_to_hub(ID_CHOP_DETECT, en);
}

static int chop_detect_set_delay(u64 delay)
{
#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	unsigned int delayms = 0;

	delayms = delay / 1000 / 1000;
	return oplus_set_delay_to_hub(ID_CHOP_DETECT, delayms);
#elif defined CONFIG_NANOHUB
	return 0;
#else
	return 0;
#endif
}

static int chop_detect_batch(int flag, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	chop_detect_set_delay(samplingPeriodNs);
#endif

	CHOP_DETECT_LOG("chop_detect: samplingPeriodNs:%lld, maxBatchReportLatencyNs: %lld\n",
		samplingPeriodNs, maxBatchReportLatencyNs);

	return oplus_batch_to_hub(ID_CHOP_DETECT, flag, samplingPeriodNs, maxBatchReportLatencyNs);
}

static int chop_detect_flush(void)
{
	return oplus_flush_to_hub(ID_CHOP_DETECT);
}

static int chop_detect_data_report(struct data_unit_t *input_event)
{
	struct oplus_sensor_event event;

	memset(&event, 0, sizeof(struct oplus_sensor_event));

	event.handle = ID_CHOP_DETECT;
	event.flush_action = DATA_ACTION;
	event.time_stamp = (int64_t)input_event->time_stamp;
	event.word[0] = input_event->oplus_data_t.chop_detect_data_t.value;
	event.word[1] = input_event->oplus_data_t.chop_detect_data_t.report_count;
	return virtual_sensor_data_report(&event);
}

static int chop_detect_flush_report(void)
{
	return virtual_sensor_flush_report(ID_CHOP_DETECT);
}

static int chop_detect_recv_data(struct data_unit_t *event, void *reserved)
{
	int err = 0;

	CHOP_DETECT_LOG("chop_detect recv data, flush_action = %d, value = %d, report_count = %d, timestamp = %lld\n",
		event->flush_action,
		event->oplus_data_t.chop_detect_data_t.value,
		event->oplus_data_t.chop_detect_data_t.report_count,
		(int64_t)event->time_stamp);

	if (event->flush_action == DATA_ACTION) {
		/*hold 100 ms timeout wakelock*/
		#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		__pm_wakeup_event(&chop_wake_lock, msecs_to_jiffies(100));
		#else
		__pm_wakeup_event(chop_wake_lock, msecs_to_jiffies(100));
	   #endif
		chop_detect_data_report(event);
	} else if (event->flush_action == FLUSH_ACTION) {
		err = chop_detect_flush_report();
	}

	return err;
}

static int chop_detecthub_local_init(void)
{
	struct virtual_sensor_control_path ctl = {0};
	int err = 0;

	ctl.open_report_data = chop_detect_open_report_data;
	ctl.enable_nodata = chop_detect_enable_nodata;
	ctl.set_delay = chop_detect_set_delay;
	ctl.batch = chop_detect_batch;
	ctl.flush = chop_detect_flush;
	ctl.report_data = chop_detect_recv_data;

#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	ctl.is_report_input_direct = true;
	ctl.is_support_batch = false;
#ifdef OPLUS_FEATURE_SENSOR_ALGORITHM
	ctl.is_support_wake_lock = true;
#endif
#elif defined CONFIG_NANOHUB
	ctl.is_report_input_direct = true;
	ctl.is_support_batch = false;
#ifdef OPLUS_FEATURE_SENSOR_ALGORITHM
	ctl.is_support_wake_lock = true;
#endif
#else
#endif

	err = virtual_sensor_register_control_path(&ctl, ID_CHOP_DETECT);
	if (err) {
		CHOP_DETECT_PR_ERR("register chop_detect control path err\n");
		goto exit;
	}
#ifdef _OPLUS_SENSOR_HUB_VI

	err = scp_sensorHub_data_registration(ID_CHOP_DETECT, chop_detect_recv_data);
	if (err < 0) {
		CHOP_DETECT_PR_ERR("SCP_sensorHub_data_registration failed\n");
		goto exit;
	}
#endif
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	wakeup_source_init(&chop_wake_lock, "chop_wake_lock");
	#else
	chop_wake_lock = wakeup_source_register(NULL, "chop_wake_lock");
	#endif
	return 0;
exit:
	return -1;
}

static int chop_detecthub_local_uninit(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	if (chop_wake_lock) {
		wakeup_source_unregister(chop_wake_lock);
		chop_wake_lock = NULL;
	}
#endif
	return 0;
}

static struct virtual_sensor_init_info chop_detecthub_init_info = {
	.name = "chop_detect_hub",
	.init = chop_detecthub_local_init,
	.uninit = chop_detecthub_local_uninit,
};

static int __init chop_detecthub_init(void)
{
	pr_err("%s\n", __func__);
	virtual_sensor_driver_add(&chop_detecthub_init_info, ID_CHOP_DETECT);
	return 0;
}

static void __exit chop_detecthub_exit(void)
{
	CHOP_DETECT_FUN();
}

module_init(chop_detecthub_init);
module_exit(chop_detecthub_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ACTIVITYHUB driver");

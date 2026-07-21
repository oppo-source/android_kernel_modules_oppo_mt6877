#include "flat_detecthub.h"
#include "sensor_cmd.h"
#include "virtual_sensor.h"
#include <linux/notifier.h>
#include <linux/pm_wakeup.h>
#include <linux/version.h>

#define FLAT_DETECT_TAG					"[flat_detecthub] "
#define FLAT_DETECT_FUN(f)				pr_err(FLAT_DETECT_TAG"%s\n", __func__)
#define FLAT_DETECT_PR_ERR(fmt, args...)	pr_err(FLAT_DETECT_TAG"%s %d : "fmt, __func__, __LINE__, ##args)
#define FLAT_DETECT_LOG(fmt, args...)		pr_err(FLAT_DETECT_TAG fmt, ##args)

static struct virtual_sensor_init_info flat_detecthub_init_info;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static struct wakeup_source flat_wake_lock;
#else
static struct wakeup_source *flat_wake_lock = NULL;
#endif
static int flat_detect_open_report_data(int open)
{
	return 0;
}

static int flat_detect_enable_nodata(int en)
{
	FLAT_DETECT_LOG("flat_detect enable nodata, en = %d, = %d\n", en, ID_FLAT_DETECT);

	return oplus_enable_to_hub(ID_FLAT_DETECT, en);
}

static int flat_detect_set_delay(u64 delay)
{
#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	unsigned int delayms = 0;

	delayms = delay / 1000 / 1000;
	return oplus_set_delay_to_hub(ID_FLAT_DETECT, delayms);
#elif defined CONFIG_NANOHUB
	return 0;
#else
	return 0;
#endif
}

static int flat_detect_batch(int flag, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	flat_detect_set_delay(samplingPeriodNs);
#endif

	FLAT_DETECT_LOG("flat_detect: samplingPeriodNs:%lld, maxBatchReportLatencyNs: %lld\n",
		samplingPeriodNs, maxBatchReportLatencyNs);

	return oplus_batch_to_hub(ID_FLAT_DETECT, flag, samplingPeriodNs, maxBatchReportLatencyNs);
}

static int flat_detect_flush(void)
{
	return oplus_flush_to_hub(ID_FLAT_DETECT);
}

static int flat_detect_data_report(struct data_unit_t *input_event)
{
	struct oplus_sensor_event event;

	memset(&event, 0, sizeof(struct oplus_sensor_event));

	event.handle = ID_FLAT_DETECT;
	event.flush_action = DATA_ACTION;
	event.time_stamp = (int64_t)input_event->time_stamp;
	event.word[0] = input_event->oplus_data_t.flat_detect_data_t.flat_status;
	event.word[1] = input_event->oplus_data_t.flat_detect_data_t.value;
	event.word[2] = input_event->oplus_data_t.flat_detect_data_t.report_count;
	return virtual_sensor_data_report(&event);
}

static int flat_detect_flush_report(void)
{
	return virtual_sensor_flush_report(ID_FLAT_DETECT);
}

static int flat_detect_recv_data(struct data_unit_t *event, void *reserved)
{
	int err = 0;

	FLAT_DETECT_LOG("flat_detect recv data, flush_action = %d, value = %d, report_count = %d, timestamp = %lld\n",
		event->flush_action,
		event->oplus_data_t.flat_detect_data_t.value,
		event->oplus_data_t.flat_detect_data_t.report_count,
		(int64_t)event->time_stamp);

	if (event->flush_action == DATA_ACTION) {
		/*hold 100 ms timeout wakelock*/
		#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		__pm_wakeup_event(&flat_wake_lock, msecs_to_jiffies(100));
		#else
		__pm_wakeup_event(flat_wake_lock, msecs_to_jiffies(100));
	   #endif
		flat_detect_data_report(event);
	} else if (event->flush_action == FLUSH_ACTION) {
		err = flat_detect_flush_report();
	}

	return err;
}

static int flat_detecthub_local_init(void)
{
	struct virtual_sensor_control_path ctl = {0};
	int err = 0;

	ctl.open_report_data = flat_detect_open_report_data;
	ctl.enable_nodata = flat_detect_enable_nodata;
	ctl.set_delay = flat_detect_set_delay;
	ctl.batch = flat_detect_batch;
	ctl.flush = flat_detect_flush;
	ctl.report_data = flat_detect_recv_data;

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

	err = virtual_sensor_register_control_path(&ctl, ID_FLAT_DETECT);
	if (err) {
		FLAT_DETECT_PR_ERR("register flat_detect control path err\n");
		goto exit;
	}
#ifdef _OPLUS_SENSOR_HUB_VI

	err = scp_sensorHub_data_registration(ID_FLAT_DETECT, flat_detect_recv_data);
	if (err < 0) {
		FLAT_DETECT_PR_ERR("SCP_sensorHub_data_registration failed\n");
		goto exit;
	}
#endif
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	wakeup_source_init(&flat_wake_lock, "flat_wake_lock");
	#else
	flat_wake_lock = wakeup_source_register(NULL, "flat_wake_lock");
	#endif
	return 0;
exit:
	return -1;
}

static int flat_detecthub_local_uninit(void)
{
	return 0;
}

static struct virtual_sensor_init_info flat_detecthub_init_info = {
	.name = "flat_detect_hub",
	.init = flat_detecthub_local_init,
	.uninit = flat_detecthub_local_uninit,
};

static int __init flat_detecthub_init(void)
{
	pr_err("%s\n", __func__);
	virtual_sensor_driver_add(&flat_detecthub_init_info, ID_FLAT_DETECT);
	return 0;
}

static void __exit flat_detecthub_exit(void)
{
	FLAT_DETECT_FUN();
}

module_init(flat_detecthub_init);
module_exit(flat_detecthub_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ACTIVITYHUB driver");

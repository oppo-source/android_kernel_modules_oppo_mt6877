#include "oplus_flashlight_notify.h"
#define SENSOR_TYPE  SENSOR_TYPE_REAR_ALS

static BLOCKING_NOTIFIER_HEAD(flashlight_notifiers);
static struct hf_client * g_hf_client = NULL;

int register_flashlight_notifier(struct notifier_block *nb)
{
	pr_err("%s", __func__);
	return blocking_notifier_chain_register(&flashlight_notifiers, nb);
}

int unregister_flashlight_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&flashlight_notifiers, nb);
}

void flashlight_notify(unsigned long val, void *v)
{
	blocking_notifier_call_chain(&flashlight_notifiers, val, v);
}

static int flashlight_cs_event(struct notifier_block *this,
	unsigned long event, void *ptr)
{
	int data = -1;
	struct custom_cmd g_cmd;

	if (ptr != NULL) {
		data =  *(int*)ptr;
	} else {
		pr_err("%s ptr err", __func__);
		return NOTIFY_DONE;
	}

	switch (event) {
	case FLASHLIGHT_STATUS_TYPE:
		if (g_hf_client) {   // send custom cmd to colorsensor
			int cnt = 0;
			g_cmd.command = FLASHLIGHT_STATUS_TYPE;
			g_cmd.data[cnt++] = data;
			g_cmd.rx_len = 0;
			g_cmd.tx_len = cnt * sizeof(g_cmd.data[0]);

			hf_client_custom_cmd(g_hf_client, SENSOR_TYPE, &g_cmd);
			pr_info("%s event(%lu) data(%d)", __func__, event, data);
		}

		break;
	default:
		pr_err("%s error event(%lu) ", __func__, event);
		break;
	}

	return NOTIFY_DONE;
}
static struct notifier_block flashlight_cs_receiver = {
	.notifier_call = flashlight_cs_event,
};

int flashlight_cs_notify_init(void)
{
	int ret = -1;
	static bool initialized = false;

	if (!initialized) {
		g_hf_client = hf_client_create();
		if (g_hf_client) {
			ret = hf_client_find_sensor(g_hf_client, SENSOR_TYPE);
			if (ret < 0) {
				pr_err("%s hf_client_find_sensor %u fail\n", __func__, SENSOR_TYPE);
				hf_client_destroy(g_hf_client);
				g_hf_client = NULL;
			} else {
				ret = register_flashlight_notifier(&flashlight_cs_receiver);
				if (ret < 0) {
					hf_client_destroy(g_hf_client);
					g_hf_client = NULL;
				}
			}
		} else {
			ret = -1;
		}
		initialized = true;
	}

	return g_hf_client ? 0 : ret;
}

EXPORT_SYMBOL(flashlight_cs_notify_init);
EXPORT_SYMBOL(flashlight_notify);
EXPORT_SYMBOL(register_flashlight_notifier);
EXPORT_SYMBOL(unregister_flashlight_notifier);

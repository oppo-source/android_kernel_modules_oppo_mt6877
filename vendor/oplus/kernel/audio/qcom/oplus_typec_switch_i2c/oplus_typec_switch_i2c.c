// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/usb/typec.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_altmode.h>
#include "oplus_typec_switch_i2c.h"
#include <linux/qti-regmap-debugfs.h>
/* Add for typec_switch headset detection interrupt */
#include <linux/of_gpio.h>
#include <linux/gpio.h>
/*add WAS4783 support*/
#include "oplus_audio_switch.h"
/* Add for 3rd protocal stack notifier */
#if IS_ENABLED(CONFIG_TCPC_CLASS)
#include <tcpci.h>
#include <tcpm.h>
#include <tcpci_typec.h>
#endif

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_MM_FEEDBACK)
/* Add for switch mode err */
#include <soc/oplus/system/oplus_mm_kevent_fb.h>
#endif

#define TYPEC_SWITCH_I2C_NAME	"oplus-typec-witch-driver"

/* Add DIO4480 support */
#define HL5280_DEVICE_REG_VALUE 0x49
#define DIO4480_DEVICE_REG_VALUE 0xF1
#define DIO4483_DEVICE_REG_VALUE 0xF5
/*add WAS4783 support*/
#define WAS4783_DEVICE_REG_VALUE 0x31
#define INVALID_DEVICE_REG_VALUE 0x00

/* Optimize the pop sound when the headset plug in
 * 0x1~0xff == 100us~25500us
 */
#define DEFAULT_SWITCH_DELAY		0x12

/* Optimize headset buttons abnormal problem */
#define DIO4483_REG_DELAY_TIME 0x21
#define DIO4483_REG_0X2E 0x2E
#define DIO4483_REG_0X2F 0x2F
#define TYPEC_SWITCH_REG_MAX 0x30

/* Add for log */
#undef dev_dbg
#define dev_dbg dev_info

/* Add DIO4480 support */
enum switch_vendor {
    FSA4480 = 0,
    HL5280,
    DIO4480,
    WAS4783,
    DIO4483
};
#define MAX_RETRY  30

/* Add for 3rd protocal stack notifier */
#if IS_ENABLED(CONFIG_TCPC_CLASS)
static int probe_retry = 0;
#endif
static int chipid_read_retry = 0;

/*add WAS4783 support*/
#define GET_BIT(x, bit)                  ((x & (1 << bit)) >> bit)  /* Get bit x */
#define GET_BITS(_var, _index, _width)   (((_var) >> (_index)) & ((0x1 << (_width)) - 1))
#define CLEAR_BIT(x, bit)                (x &= ~(1 << bit))  /* Reset bit x */
#define SET_BIT(x, bit)                  (x |= (1 << bit))   /* Set bit x */

enum TYPEC_AUDIO_SWITCH_STATE {
	TYPEC_AUDIO_SWITCH_STATE_DPDM          = 0x0,
	TYPEC_AUDIO_SWITCH_STATE_FAST_CHG      = 0x1,
	TYPEC_AUDIO_SWITCH_STATE_AUDIO         = 0x1 << 1,
	TYPEC_AUDIO_SWITCH_STATE_UNKNOW        = 0x1 << 2,
	TYPEC_AUDIO_SWITCH_STATE_SUPPORT       = 0x1 << 4,
	TYPEC_AUDIO_SWITCH_STATE_NO_RAM,
	TYPEC_AUDIO_SWITCH_STATE_I2C_ERR       = 0x1 << 8,
	TYPEC_AUDIO_SWITCH_STATE_INVALID_PARAM = 0x1 << 9,
};

enum DNDP_STATE {
	DNL_OPEN_OR_DPR_OPEN      = 0x0, /* DN_L switch is open, or DN_R switch is open */
	DNL_DN_OR_DPR_DP          = 0x1, /* DN_L switch is connected to DN, or DN_R switch is connected to DP */
	DNL_L_OR_DPR_R            = 0x2, /* DN_L switch is connected to L, or DP_R switch is connected to R */
	DNL_DN2_OR_DPR_DP2        = 0x3, /* DN_L switch is connected to DN2, or DP_R switch is connected to DP2 */
};

struct typec_switch_priv {
	struct regmap *regmap;
	struct device *dev;
	struct notifier_block nb;
	atomic_t usbc_mode;
	struct work_struct usbc_analog_work;
	struct blocking_notifier_head typec_switch_notifier;
	struct mutex notification_lock;
/* Add for typec_switch headset detection interrupt */
	unsigned int hs_det_pin;
	struct typec_mux_dev *mux;

/* Add DIO4480 support */
	enum switch_vendor vendor;
	/* Add for 3rd usb protocal support */
	unsigned int usb_protocal;
	/*add WAS4783 support*/
	struct notifier_block chg_nb;
	struct mutex noti_lock;
	unsigned int switch_to_fast_charger_support;
	bool chg_registration;
	/*add for 3rd protocal stack notifer*/
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	struct tcpc_device *tcpc;
#endif
};

struct typec_switch_reg_val {
	u16 reg;
	u8 val;
};

static const struct regmap_config typec_switch_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TYPEC_SWITCH_REG_MAX,
};

static const struct typec_switch_reg_val switch_reg_i2c_defaults[] = {
/* Add for reset control reg */
	{DEFAULT_REG_SWITCH_CONTROL, 0x18},
	{DEFAULT_REG_SLOW_L, 0x00},
	{DEFAULT_REG_SLOW_R, 0x00},
	{DEFAULT_REG_SLOW_MIC, 0x00},
	{DEFAULT_REG_SLOW_SENSE, 0x00},
	{DEFAULT_REG_SLOW_GND, 0x00},
	{DEFAULT_REG_DELAY_L_R, 0x00},
/* Optimize the pop sound when the headset plug in */
	{DEFAULT_REG_DELAY_L_MIC, DEFAULT_SWITCH_DELAY},
	{DEFAULT_REG_DELAY_L_SENSE, 0x00},
	{DEFAULT_REG_DELAY_L_AGND, 0x09},
	{DEFAULT_REG_SWITCH_SETTINGS, 0x98},
	{DEFAULT_REG_FUN_EN, 0x08},
};

/* Add DIO4480 support */
int typec_switch_get_chip_vendor(struct device_node *node)
{
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct typec_switch_priv *switch_priv;

	if (!client)
		return -EINVAL;

	switch_priv = (struct typec_switch_priv *)i2c_get_clientdata(client);
	if (!switch_priv)
		return -EINVAL;


	return switch_priv->vendor;
}
EXPORT_SYMBOL(typec_switch_get_chip_vendor);

/* DIO4483: Optimize headset buttons abnormal problem */
static void typec_switch_usbc_update_settings_dio4483(
	struct typec_switch_priv *switch_priv,
	u32 switch_control, u32 switch_enable)
{
	u32 prev_control = 0;
	u32 prev_enable = 0;

	if (!switch_priv->regmap) {
		dev_err(switch_priv->dev, "%s: regmap invalid\n", __func__);
		return;
	}

	regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_CONTROL, &prev_control);
	regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_SETTINGS, &prev_enable);
	if (prev_control == switch_control && prev_enable == switch_enable) {
		dev_dbg(switch_priv->dev, "%s: settings unchanged\n", __func__);
		return;
	}

	regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_SETTINGS, 0x80);

	/* Add DIO4480 support */
	if ((switch_priv->vendor == DIO4480) || (switch_priv->vendor == DIO4483)) {
		regmap_write(switch_priv->regmap, DEFAULT_REG_RESET, 0x01);//reset DIO4480
		usleep_range(1000, 1005);
	}

	regmap_write(switch_priv->regmap, DEFAULT_REG_DELAY_L_SENSE, 0x00);
	regmap_write(switch_priv->regmap, DEFAULT_REG_DELAY_L_AGND, 0x00);
	regmap_write(switch_priv->regmap, DEFAULT_REG_DELAY_L_MIC, 0x0B);
	regmap_write(switch_priv->regmap, DEFAULT_REG_DELAY_L_R, 0x0F);
	regmap_write(switch_priv->regmap, DIO4483_REG_DELAY_TIME, 0x0F);
	regmap_write(switch_priv->regmap, DEFAULT_REG_FUN_EN, 0x08);

	regmap_write(switch_priv->regmap, DIO4483_REG_0X2E, 0x8F);
	regmap_write(switch_priv->regmap, DIO4483_REG_0X2F, 0x45);
	regmap_write(switch_priv->regmap, DIO4483_REG_0X2E, 0x72);

	regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_CONTROL, switch_control);
	/* DEFAULT_REG chip hardware requirement */
	usleep_range(50, 55);
	regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_SETTINGS, switch_enable);
	/* Optimize the pop sound when the headset plug in */
	usleep_range(DEFAULT_SWITCH_DELAY * 100, DEFAULT_SWITCH_DELAY * 100 + 50);
}

static void typec_switch_usbc_update_settings(struct typec_switch_priv *switch_priv,
		u32 switch_control, u32 switch_enable)
{
	u32 prev_control = 0;
	u32 prev_enable = 0;

	if (!switch_priv->regmap) {
		dev_err(switch_priv->dev, "%s: regmap invalid\n", __func__);
		return;
	}

	regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_CONTROL, &prev_control);
	regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_SETTINGS, &prev_enable);
	if (prev_control == switch_control && prev_enable == switch_enable) {
		dev_dbg(switch_priv->dev, "%s: settings unchanged\n", __func__);
		return;
	}

	regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_SETTINGS, 0x80);

/* Add DIO4480 support */
	if((switch_priv->vendor == DIO4480) || (switch_priv->vendor == DIO4483)) {
		regmap_write(switch_priv->regmap, DEFAULT_REG_RESET, 0x01);//reset DIO4480
		usleep_range(1000, 1005);
	}

	regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_CONTROL, switch_control);
	/* FSA4480 chip hardware requirement */
	usleep_range(50, 55);
	regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_SETTINGS, switch_enable);
/* Optimize the pop sound when the headset plug in */
	usleep_range(DEFAULT_SWITCH_DELAY*100, DEFAULT_SWITCH_DELAY*100+50);
}

static int typec_switch_usbc_event_changed(struct notifier_block *nb,
				      unsigned long evt, void *ptr)
{
	struct typec_switch_priv *switch_priv =
			container_of(nb, struct typec_switch_priv, nb);
	struct device *dev;

	enum typec_accessory acc = TYPEC_ACCESSORY_NONE;
#if IS_ENABLED(CONFIG_TCPC_CLASS)
/* Add for 3rd protocal stack notifier*/
	struct tcp_notify *noti = ptr;
	int old_state = TYPEC_UNATTACHED;
	int new_state = TYPEC_UNATTACHED;
#endif

	if (!switch_priv)
		return -EINVAL;

	dev = switch_priv->dev;
	if (!dev)
		return -EINVAL;

	if (switch_priv->usb_protocal == 1) {
#if IS_ENABLED(CONFIG_TCPC_CLASS)
		dev_info(dev, "%s: USB change event received, new_state:%d, old_state:%d\n",
				__func__, noti->typec_state.new_state, noti->typec_state.old_state);
#endif
	} else {
		dev_info(dev, "%s: USB change event received, supply mode %d, usbc mode %d, expected %d\n",
				__func__, acc, switch_priv->usbc_mode.counter, TYPEC_ACCESSORY_AUDIO);
	}

/* Add for 3rd protocal stack notifier */
	if (switch_priv->usb_protocal == 1) {
#if IS_ENABLED(CONFIG_TCPC_CLASS)
		switch (evt) {
		case TCP_NOTIFY_TYPEC_STATE:
			old_state = noti->typec_state.old_state;
			new_state = noti->typec_state.new_state;
			if (old_state == TYPEC_UNATTACHED &&
			    new_state == TYPEC_ATTACHED_AUDIO) {
				dev_info(dev, "Audio plug in\n");
				/* enable AudioAccessory connection */
				acc = TYPEC_ACCESSORY_AUDIO;
			} else if (old_state == TYPEC_ATTACHED_AUDIO &&
				   new_state == TYPEC_UNATTACHED) {
				dev_info(dev, "Audio plug out\n");
				/* disable AudioAccessory connection */
				acc = TYPEC_ACCESSORY_NONE;
			}
			break;
		default:
			return 0;
		}
#endif
	}

	switch (acc) {
	case TYPEC_ACCESSORY_AUDIO:
	case TYPEC_ACCESSORY_NONE:
		if (atomic_read(&(switch_priv->usbc_mode)) == acc)
			break; /* filter notifications received before */
		atomic_set(&(switch_priv->usbc_mode), acc);

		dev_dbg(dev, "%s: queueing usbc_analog_work\n",
			__func__);
		pm_stay_awake(switch_priv->dev);
		queue_work(system_freezable_wq, &switch_priv->usbc_analog_work);
		break;
	default:
		break;
	}

	return 0;
}

static int typec_switch_usbc_analog_setup_switches(struct typec_switch_priv *switch_priv)
{
	int rc = 0;
	int mode;
	struct device *dev;
/* Add for get status */
	unsigned int switch_status = 0;
	unsigned int jack_status = 0;

	if (!switch_priv)
		return -EINVAL;
	dev = switch_priv->dev;
	if (!dev)
		return -EINVAL;

	mutex_lock(&switch_priv->notification_lock);
	/* get latest mode again within locked context */
	mode = atomic_read(&(switch_priv->usbc_mode));

	dev_dbg(dev, "%s: setting GPIOs active = %d\n",
		__func__, mode != TYPEC_ACCESSORY_NONE);

	dev_info(dev, "%s: USB mode %d\n", __func__, mode);

	switch (mode) {
	/* add all modes FSA should notify for in here */
	case TYPEC_ACCESSORY_AUDIO:
		/* activate switches */
		/* DIO4483: Optimize headset buttons abnormal problem */
		if (switch_priv->vendor == DIO4483) {
			typec_switch_usbc_update_settings_dio4483(switch_priv, 0x00, 0x9F);
		} else {
			typec_switch_usbc_update_settings(switch_priv, 0x00, 0x9F);
		}
/* Add DIO4480 support */
		if ((switch_priv->vendor != DIO4480) && (switch_priv->vendor != DIO4483)) {
			/* Add for open auto mic DET */
			usleep_range(1000, 1005);
			regmap_write(switch_priv->regmap, DEFAULT_REG_FUN_EN, 0x45);
			usleep_range(4000, 4005);
			dev_info(dev, "%s: set reg[0x%x] done.\n", __func__, DEFAULT_REG_FUN_EN);

			regmap_read(switch_priv->regmap, DEFAULT_REG_JACK_STATUS, &jack_status);
			dev_info(dev, "%s: reg[0x%x]=0x%x.\n", __func__, DEFAULT_REG_JACK_STATUS, jack_status);
			if (jack_status & 0x2) {
				//for 3 pole, mic switch to SBU2
				dev_info(dev, "%s: set mic to sbu2 for 3 pole.\n", __func__);
				typec_switch_usbc_update_settings(switch_priv, 0x00, 0x9F);
				usleep_range(4000, 4005);
			}
		}

		/* DIO4483: Optimize headset buttons abnormal problem */
		if (switch_priv->vendor == DIO4483) {
			usleep_range(10000, 10005);
		}

		regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_STATUS0, &switch_status);
		dev_info(dev, "%s: reg[0x%x]=0x%x.\n", __func__, DEFAULT_REG_SWITCH_STATUS0, switch_status);
		regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_STATUS1, &switch_status);
		dev_info(dev, "%s: reg[0x%x]=0x%x.\n", __func__, DEFAULT_REG_SWITCH_STATUS1, switch_status);

		/* notify call chain on event */
		blocking_notifier_call_chain(&switch_priv->typec_switch_notifier,
					     mode, NULL);

/* Add for typec_switch headset detection interrupt */
		if (gpio_is_valid(switch_priv->hs_det_pin)) {
			dev_info(dev, "%s: set hs_det_pin to low.\n", __func__);
			gpio_direction_output(switch_priv->hs_det_pin, 0);
		}

		break;
	case TYPEC_ACCESSORY_NONE:
/* Add for typec_switch headset detection interrupt */
		if (gpio_is_valid(switch_priv->hs_det_pin)) {
			dev_info(dev, "%s: set hs_det_pin to high.\n", __func__);
			gpio_direction_output(switch_priv->hs_det_pin, 1);
		}

		/* notify call chain on event */
		blocking_notifier_call_chain(&switch_priv->typec_switch_notifier,
				TYPEC_ACCESSORY_NONE, NULL);

		/* deactivate switches */
		typec_switch_usbc_update_settings(switch_priv, 0x18, 0x98);
		break;
	default:
		/* ignore other usb connection modes */
		break;
	}

	mutex_unlock(&switch_priv->notification_lock);
	return rc;
}

static int typec_switch_to_fast_charger(struct typec_switch_priv *switch_priv, unsigned long to_fast_charger)
{
	struct device *dev;
	int ret = 0;
	unsigned int reg_val = 0, dn_l_status = 0, dp_r_status = 0, width = 2;

	if (!switch_priv) {
		pr_err("%s, switch_priv is NULL", __func__);
		return -EINVAL;
	}

	dev = switch_priv->dev;
	if (!dev) {
		pr_err("%s, switch_priv->dev is NULL", __func__);
		return -EINVAL;
	}

	if ((switch_priv->vendor != WAS4783) && (switch_priv->vendor != DIO4483)) {
		dev_err(dev, "%s, current chip 0x%02x, is not supported!", __func__, switch_priv->vendor);
		return -EINVAL;
	}

	mutex_lock(&switch_priv->noti_lock);
	regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_STATUS0, &reg_val);
	dn_l_status = GET_BITS(reg_val, DEFAULT_SWITCH_STATUS0_DN_L_SWITCH_STATUS_L, width);
	dp_r_status = GET_BITS(reg_val, DEFAULT_SWITCH_STATUS0_DP_R_SWITCH_STATUS_L, width);
	if (dn_l_status == 0x02 || dp_r_status == 0x02) {
		dev_info(dev,"%s, switching from headphone to charger is prohibited!\n", __func__);
		mutex_unlock(&switch_priv->noti_lock);
		return -EPERM;
	}

	dev_info(dev, "%s: to_fast_charger = %ld\n", __func__, to_fast_charger);
	if (to_fast_charger) {
		if (switch_priv->vendor == DIO4483)
			regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_CONTROL, 0x98);
		else
			regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_CONTROL, 0x80);

		regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_SETTINGS, 0x98);
		usleep_range(10000, 10005);
		dev_info(dev, "%s, charger plugin. set to switch mode", __func__);
	} else {
		regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_CONTROL, 0x18);
		regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_SETTINGS, 0x98);
		dev_info(dev, "%s, charger plugout. set to usb mode", __func__);
	}

	mutex_unlock(&switch_priv->noti_lock);
	return ret;
}

/*add WAS4783 support*/
/* bit3~0:
* 0000 DPDM, 0001 fast charge, 0010 Headphone 0100 unknown
*
* bit7~4:
* 0000 not support 1to3 switch
* 0001 support 1to3 switch

* bit11~bit8:
* 0000 noram
* 0001 iic err
* 0010 invalid param */
static int typec_switch_get_status(struct typec_switch_priv *switch_priv)
{
	struct device *dev = NULL;
	int rc = 0;
	unsigned int reg_status = 0, reg_val = 0, reg_l_shift = 0, reg_r_shift = 0;
	unsigned int width = 2, dn_l_status = 0, dp_r_status = 0;

	if (!switch_priv) {
		pr_err("%s, switch_priv is NULL", __func__);
		rc |= TYPEC_AUDIO_SWITCH_STATE_INVALID_PARAM;
		goto err_handler;
	}

	dev = switch_priv->dev;
	if (!dev) {
		pr_err("%s, switch_priv->dev is NULL", __func__);
		rc |= TYPEC_AUDIO_SWITCH_STATE_INVALID_PARAM;
		goto err_handler;
	}

	mutex_lock(&switch_priv->noti_lock);
	if ((switch_priv->vendor == WAS4783) || (switch_priv->vendor == DIO4483)) {
		rc |= TYPEC_AUDIO_SWITCH_STATE_SUPPORT;
	}

	reg_status = DEFAULT_REG_SWITCH_STATUS0;
	reg_l_shift = DEFAULT_SWITCH_STATUS0_DN_L_SWITCH_STATUS_L;
	reg_r_shift = DEFAULT_SWITCH_STATUS0_DP_R_SWITCH_STATUS_L;

	regmap_read(switch_priv->regmap, reg_status, &reg_val);
	dev_info(dev, "%s, reg[0x%02x] = 0x%02x", __func__, reg_status, reg_val);
	dn_l_status = GET_BITS(reg_val, reg_l_shift, width);
	dp_r_status = GET_BITS(reg_val, reg_r_shift, width);

	dev_info(dev, "%s, dn_l_status = 0x%02x, dp_r_status = 0x%02x", __func__, dn_l_status, dp_r_status);
	switch (dn_l_status) {
	case DNL_OPEN_OR_DPR_OPEN:
		dev_info(dev, "%s, DN_L Switch Open/Not Connected", __func__);
		break;
	case DNL_DN_OR_DPR_DP:
		dev_info(dev, "%s, DN_L connected to DN1", __func__);
		break;
	case DNL_L_OR_DPR_R: // Headphone
		dev_info(dev, "%s, DN_L connected to L", __func__);
		break;
	case DNL_DN2_OR_DPR_DP2: // Fast Charger
		dev_info(dev, "%s, DN_L connected to DN2", __func__);
		break;
	default:
		break;
	}

	switch (dp_r_status) {
	case DNL_OPEN_OR_DPR_OPEN:
		dev_info(dev, "%s, DP_R Switch Open/Not Connected", __func__);
		break;
	case DNL_DN_OR_DPR_DP:
		dev_info(dev, "%s, DP_R connected to DP1", __func__);
		break;
	case DNL_L_OR_DPR_R: // Headphone
		dev_info(dev, "%s, DP_R connected to R", __func__);
		break;
	case DNL_DN2_OR_DPR_DP2: // Fast Charger
		dev_info(dev, "%s, DP_R connected to DP2", __func__);
		break;
	default:
		break;
	}

	if ((dn_l_status == DNL_DN_OR_DPR_DP) && (dp_r_status == DNL_DN_OR_DPR_DP)) { // Charger
		rc |= TYPEC_AUDIO_SWITCH_STATE_DPDM;
	} else if ((dn_l_status == DNL_DN2_OR_DPR_DP2) && (dp_r_status == DNL_DN2_OR_DPR_DP2)) { // Fast Charger
		rc |= TYPEC_AUDIO_SWITCH_STATE_FAST_CHG;
	} else if ((dn_l_status == DNL_L_OR_DPR_R) && (dp_r_status == DNL_L_OR_DPR_R)) { // Headphone
		rc |= TYPEC_AUDIO_SWITCH_STATE_AUDIO;
	} else { // Unknown
		rc |= TYPEC_AUDIO_SWITCH_STATE_UNKNOW;
		pr_err("%s, Typec audio switch state is unknow", __func__);
	}
	mutex_unlock(&switch_priv->noti_lock);

err_handler:
	return rc;
}

static int typec_switch_chg_event_changed(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct typec_switch_priv *switch_priv =
				container_of(nb, struct typec_switch_priv, chg_nb);
	switch (event) {
	case TYPEC_AUDIO_SWITCH_STATE_DPDM:
	case TYPEC_AUDIO_SWITCH_STATE_FAST_CHG:
		typec_switch_to_fast_charger(switch_priv, event);
		break;
	case TYPEC_AUDIO_SWITCH_STATE_AUDIO:
		return typec_switch_get_status(switch_priv);
	default:
		break;
	}

	return NOTIFY_OK;
}

/* Add for dynamic check cross */
int typec_switch_check_cross_conn(struct device_node *node)
{
	int ret = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct typec_switch_priv *switch_priv;

	if (!client) {
		pr_err("%s: typec_switch client is NULL\n", __func__);
		return 0;
	}

	switch_priv = (struct typec_switch_priv *)i2c_get_clientdata(client);
	if (!switch_priv) {
		pr_err("%s: switch_priv is NULL\n", __func__);
		return 0;
	}

	dev_dbg(switch_priv->dev, "%s: registered vendor for %d\n",
		__func__, switch_priv->vendor);

	switch (switch_priv->vendor) {
	case FSA4480:
	case HL5280:
	case WAS4783:
	    ret = 0;
	    break;
	case DIO4480:
	case DIO4483:
	    ret = 1;
	    break;
	default:
		break;
	}

	return ret;
}
EXPORT_SYMBOL(typec_switch_check_cross_conn);

/*
 * oplus_typec_switch_reg_notifier - register notifier block with fsa driver
 *
 * @nb - notifier block of typec_switch
 * @node - phandle node to typec_switch device
 *
 * Returns 0 on success, or error code
 */
int typec_switch_reg_notifier(struct notifier_block *nb,
			 struct device_node *node)
{
	int rc = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct typec_switch_priv *switch_priv;

	if (!client)
		return -EINVAL;

	switch_priv = (struct typec_switch_priv *)i2c_get_clientdata(client);
	if (!switch_priv)
		return -EINVAL;

	rc = blocking_notifier_chain_register
				(&switch_priv->typec_switch_notifier, nb);

	dev_dbg(switch_priv->dev, "%s: registered notifier for %s\n",
		__func__, node->name);
	if (rc)
		return rc;

	/*
	 * as part of the init sequence check if there is a connected
	 * USB C analog adapter
	 */
	if (atomic_read(&(switch_priv->usbc_mode)) == TYPEC_ACCESSORY_AUDIO) {
		dev_dbg(switch_priv->dev, "%s: analog adapter already inserted\n",
			__func__);
		rc = typec_switch_usbc_analog_setup_switches(switch_priv);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(typec_switch_reg_notifier);

/*
 * oplus_typec_switch_reg_notifier - unregister notifier block with fsa driver
 *
 * @nb - notifier block of typec_switch
 * @node - phandle node to typec_switch device
 *
 * Returns 0 on pass, or error code
 */
int typec_switch_unreg_notifier(struct notifier_block *nb,
			     struct device_node *node)
{
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct typec_switch_priv *switch_priv;

	if (!client)
		return -EINVAL;

	switch_priv = (struct typec_switch_priv *)i2c_get_clientdata(client);
	if (!switch_priv)
		return -EINVAL;

	typec_switch_usbc_update_settings(switch_priv, 0x18, 0x98);
	return blocking_notifier_chain_unregister
					(&switch_priv->typec_switch_notifier, nb);
}
EXPORT_SYMBOL_GPL(typec_switch_unreg_notifier);

static int typec_switch_validate_display_port_settings(struct typec_switch_priv *switch_priv)
{
	u32 switch_status = 0;

	regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_STATUS1, &switch_status);

	if ((switch_status != 0x23) && (switch_status != 0x1C)) {
		pr_err("AUX SBU1/2 switch status is invalid = %u\n",
				switch_status);
		return -EIO;
	}

	return 0;
}
/*
 * typec_switch_switch_event - configure switch position based on event
 *
 * @node - phandle node to typec_switch device
 * @event - typec_switch_function enum
 *
 * Returns int on whether the switch happened or not
 */
int typec_switch_switch_event(struct device_node *node,
			 enum typec_switch_function event)
{
	int switch_control = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct typec_switch_priv *switch_priv;
/* Add record plugin status */
	unsigned int setting_reg_val = 0, control_reg_val = 0;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_MM_FEEDBACK)
	/* Add for switch mode err */
	char buf[MM_KEVENT_MAX_PAYLOAD_SIZE] = {0};
#endif /* CONFIG_OPLUS_FEATURE_MM_FEEDBACK */

	if (!client)
		return -EINVAL;

	switch_priv = (struct typec_switch_priv *)i2c_get_clientdata(client);
	if (!switch_priv)
		return -EINVAL;
	if (!switch_priv->regmap)
		return -EINVAL;

	pr_info("%s - switch event: %d\n", __func__, event);

	switch (event) {
	case TYPEC_SWITCH_MIC_GND_SWAP:
/* Add for status err */
		if (switch_priv->usbc_mode.counter != TYPEC_ACCESSORY_AUDIO) {
			regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_SETTINGS, &setting_reg_val);
			regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_CONTROL, &control_reg_val);
			pr_err("%s: error mode, reg[0x%x]=0x%x, reg[0x%x]=0x%x\n", __func__,
					DEFAULT_REG_SWITCH_SETTINGS, setting_reg_val, DEFAULT_REG_SWITCH_CONTROL, control_reg_val);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_MM_FEEDBACK)
			/* Add for switch mode err */
			scnprintf(buf, sizeof(buf) - 1, "func@@%s$$typec_mode@@%d$$regs@@0x%x,0x%x", \
					__func__, switch_priv->usbc_mode.counter, setting_reg_val, control_reg_val);
			upload_mm_fb_kevent_to_atlas_limit(OPLUS_AUDIO_EVENTID_HEADSET_DET, buf, MM_FB_KEY_RATELIMIT_5MIN);
#endif /* CONFIG_OPLUS_FEATURE_MM_FEEDBACK */
			break;
		}

		regmap_read(switch_priv->regmap, DEFAULT_REG_SWITCH_CONTROL,
				&switch_control);
		if ((switch_control & 0x07) == 0x07)
			switch_control = 0x0;
		else
			switch_control = 0x7;
		/* DIO4483: Optimize headset buttons abnormal problem */
		if (switch_priv->vendor == DIO4483) {
			typec_switch_usbc_update_settings_dio4483(switch_priv, switch_control, 0x9F);
		} else {
			typec_switch_usbc_update_settings(switch_priv, switch_control, 0x9F);
		}
		break;

/* Add DIO4480 support */
	case TYPEC_SWITCH_CONNECT_LR:
		usleep_range(50, 55);
		regmap_write(switch_priv->regmap, DEFAULT_REG_SWITCH_SETTINGS, 0x9F);
		pr_info("%s - panzhao connect LR  \n", __func__);
		break;

	case TYPEC_SWITCH_USBC_ORIENTATION_CC1:
		typec_switch_usbc_update_settings(switch_priv, 0x18, 0xF8);
		return typec_switch_validate_display_port_settings(switch_priv);
	case TYPEC_SWITCH_USBC_ORIENTATION_CC2:
		typec_switch_usbc_update_settings(switch_priv, 0x78, 0xF8);
		return typec_switch_validate_display_port_settings(switch_priv);
	case TYPEC_SWITCH_USBC_DISPLAYPORT_DISCONNECTED:
		typec_switch_usbc_update_settings(switch_priv, 0x18, 0x98);
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(typec_switch_switch_event);

/* Add for typec_switch headset detection interrupt */
static int typec_switch_parse_dt(struct typec_switch_priv *switch_priv,
	struct device *dev)
{
	struct device_node *dNode = dev->of_node;
	const char *usb_protocal_name = "typec_switch,use-3rd-usb-protocal";
	const char *switch_to_fast_charger_support = "typec_switch,switch-to-fast-charger-support";
	int ret = 0;
	int rc = 0;

	if (dNode == NULL)
		return -ENODEV;

	if (!switch_priv) {
		pr_err("%s: switch_priv is NULL\n", __func__);
		return -ENOMEM;
	}

	switch_priv->hs_det_pin = of_get_named_gpio(dNode,
	        "oplus,hs-det-gpio", 0);
	if (!gpio_is_valid(switch_priv->hs_det_pin)) {
	    pr_info("%s: hs-det-gpio in dt node is missing\n", __func__);
	}

	rc = of_property_read_u32(dNode, usb_protocal_name, &switch_priv->usb_protocal);
	if (rc) {
		pr_info("%s: %s in dt node is missing\n", __func__, usb_protocal_name);
		switch_priv->usb_protocal = 0;
	}

	rc = of_property_read_u32(dNode, switch_to_fast_charger_support, &switch_priv->switch_to_fast_charger_support);
	if (rc) {
		pr_info("%s: %s in dt node is missing\n", __func__, switch_to_fast_charger_support);
		switch_priv->switch_to_fast_charger_support = 1;
	}
	return ret;
}

static void typec_switch_usbc_analog_work_fn(struct work_struct *work)
{
	struct typec_switch_priv *switch_priv =
		container_of(work, struct typec_switch_priv, usbc_analog_work);

	if (!switch_priv) {
		pr_err("%s: fsa container invalid\n", __func__);
		return;
	}
	typec_switch_usbc_analog_setup_switches(switch_priv);
	pm_relax(switch_priv->dev);
}

static void typec_switch_update_reg_defaults(struct regmap *regmap)
{
	u8 i;

	for (i = 0; i < ARRAY_SIZE(switch_reg_i2c_defaults); i++)
		regmap_write(regmap, switch_reg_i2c_defaults[i].reg,
				   switch_reg_i2c_defaults[i].val);
}

static int typec_switch_mux_set(struct typec_mux_dev *mux, struct typec_mux_state *state)
{
	struct typec_switch_priv *switch_priv = typec_mux_get_drvdata(mux);
	enum typec_accessory acc = TYPEC_ACCESSORY_NONE;

	if (!switch_priv)
		return -EINVAL;

	if (!switch_priv->dev)
		return -EINVAL;

	if (state->mode == TYPEC_MODE_AUDIO) {
		acc = TYPEC_ACCESSORY_AUDIO;
	} else if (state->mode == TYPEC_MODE_DEBUG) {
		acc = TYPEC_ACCESSORY_DEBUG;
	} else {
		acc = TYPEC_ACCESSORY_NONE;
	}

	dev_info(switch_priv->dev, "%s: USB change event received, supply mode %d, usbc mode %d, expected %d\n",
			__func__, acc, switch_priv->usbc_mode.counter,
			TYPEC_ACCESSORY_AUDIO);

	switch (acc) {
	case TYPEC_ACCESSORY_AUDIO:
	case TYPEC_ACCESSORY_NONE:
		if (atomic_read(&(switch_priv->usbc_mode)) == acc)
			break; /* filter notifications received before */
		atomic_set(&(switch_priv->usbc_mode), acc);

		dev_dbg(switch_priv->dev, "%s: queueing usbc_analog_work\n",
			__func__);
		pm_stay_awake(switch_priv->dev);
		queue_work(system_freezable_wq, &switch_priv->usbc_analog_work);
		break;
	default:
		break;
	}

	return 0;
}

static int typec_switch_probe(struct i2c_client *i2c)
{
	struct typec_mux_desc mux_desc = { };
	struct typec_switch_priv *switch_priv;
	int rc = 0;
/* Add DIO4480 support */
	unsigned int reg_value = 0;
	/* Add for WAS4783 */
	int chg_retries = MAX_RETRY;

	pr_err("%s: enter\n", __func__);

	switch_priv = devm_kzalloc(&i2c->dev, sizeof(*switch_priv),
				GFP_KERNEL);
	if (!switch_priv)
		return -ENOMEM;

	switch_priv->dev = &i2c->dev;

/* Add for typec_switch headset detection interrupt */
	typec_switch_parse_dt(switch_priv, &i2c->dev);
	/* Add for WAS4783 */
	switch_priv->chg_registration = false;

	switch_priv->regmap = devm_regmap_init_i2c(i2c, &typec_switch_regmap_config);
	if (IS_ERR_OR_NULL(switch_priv->regmap)) {
		dev_err(switch_priv->dev, "%s: Failed to initialize regmap: %d\n",
			__func__, rc);
		if (!switch_priv->regmap) {
			rc = -EINVAL;
			goto err_data;
		}
		rc = PTR_ERR(switch_priv->regmap);
		goto err_data;
	}

/*add DIO4480/WAS4783 support*/
	regmap_read(switch_priv->regmap, DEFAULT_REG_DEVICE_ID, &reg_value);
	dev_info(switch_priv->dev, "%s: device id reg value: 0x%x\n", __func__, reg_value);
	if (reg_value == HL5280_DEVICE_REG_VALUE) {
		dev_info(switch_priv->dev, "%s: switch chip is HL5280\n", __func__);
		switch_priv->vendor = HL5280;
	} else if (reg_value == DIO4480_DEVICE_REG_VALUE) {
		dev_info(switch_priv->dev, "%s: switch chip is DIO4480\n", __func__);
		switch_priv->vendor = DIO4480;
	} else if (reg_value == DIO4483_DEVICE_REG_VALUE) {
		dev_info(switch_priv->dev, "%s: switch chip is DIO4483\n", __func__);
		switch_priv->vendor = DIO4483;
	} else if (reg_value == WAS4783_DEVICE_REG_VALUE) {
		dev_info(switch_priv->dev, "%s: switch chip is WAS4783\n", __func__);
		switch_priv->vendor = WAS4783;
	} else if (reg_value == INVALID_DEVICE_REG_VALUE && chipid_read_retry < 5) {
		dev_info(switch_priv->dev, "%s: incorrect chip ID [0x%x]\n", __func__, reg_value);
		chipid_read_retry++;
		usleep_range(1*1000, 1*1005);
		rc = -EPROBE_DEFER;
		goto err_data;
	} else {
		dev_info(switch_priv->dev, "%s: switch chip is FSA4480\n", __func__);
		switch_priv->vendor = FSA4480;
	}

	if ((switch_priv->vendor != DIO4480) && (switch_priv->vendor != DIO4483)) {
		typec_switch_update_reg_defaults(switch_priv->regmap);
		devm_regmap_qti_debugfs_register(switch_priv->dev, switch_priv->regmap);
	} else {
		regmap_write(switch_priv->regmap, DEFAULT_REG_RESET, 0x01);//reset DIO4480
		usleep_range(1*1000, 1*1005);
	}

	switch_priv->nb.notifier_call = typec_switch_usbc_event_changed;
	switch_priv->nb.priority = 0;
	if (switch_priv->usb_protocal != 1) {
		mux_desc.drvdata = switch_priv;
		mux_desc.fwnode = dev_fwnode(switch_priv->dev);
		mux_desc.set = typec_switch_mux_set;
		switch_priv->mux = typec_mux_register(switch_priv->dev, &mux_desc);
		if (IS_ERR(switch_priv->mux)) {
			dev_err(switch_priv->dev, "failed to register typec mux: %ld\n", PTR_ERR(switch_priv->mux));
			goto err_data;
		}
	} else {
#if IS_ENABLED(CONFIG_TCPC_CLASS)
		dev_info(switch_priv->dev, "%s: start register 3rd protocal stack notifier\n", __func__);
		switch_priv->tcpc = tcpc_dev_get_by_name("type_c_port0");
		if (!switch_priv->tcpc) {
			if (probe_retry > 30) {
				dev_err(switch_priv->dev, "%s: get tcpc failed, jump tcp register\n", __func__);
				rc = 0;
				goto tcp_register_finish;
			} else {
				probe_retry++;
				dev_err(switch_priv->dev, "%s: get tcpc failed, retry:%d \n", __func__, probe_retry);
				usleep_range(1*1000, 1*1005);
				rc = -EPROBE_DEFER;
				goto err_data;
			}
		}
		rc = register_tcp_dev_notifier(switch_priv->tcpc, &switch_priv->nb, TCP_NOTIFY_TYPE_USB);
		if (rc) {
			dev_err(switch_priv->dev, "%s: ucsi glink notifier registration failed: %d\n",
				__func__, rc);
			goto err_data;
		}
#endif
	}

#if IS_ENABLED(CONFIG_TCPC_CLASS)
tcp_register_finish:
#endif
/* add WAS4783 support */
	mutex_init(&switch_priv->noti_lock);

	if (((switch_priv->vendor == WAS4783) || (switch_priv->vendor == DIO4483)) && switch_priv->switch_to_fast_charger_support == 1) {
		switch_priv->chg_nb.notifier_call = typec_switch_chg_event_changed;
		switch_priv->chg_nb.priority = 0;
		do {
			rc = register_chg_glink_notifier(&switch_priv->chg_nb);
			if (rc) {
				dev_err(switch_priv->dev, "%s: Failed to register charge glink notifier, will retry for %d times\n",
					__func__, (chg_retries - 1));
				usleep_range(1*1000, 1*1005);
				rc = 0;
			} else {
				switch_priv->chg_registration = true;
				dev_info(switch_priv->dev, "%s: register charge glink notifier success\n", __func__);
				break;
			}
			chg_retries--;
		} while (chg_retries > 0);

		if (!switch_priv->chg_registration) {
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_MM_FEEDBACK)
			mm_fb_audio_fatal_delay(OPLUS_AUDIO_EVENTID_HEADSET_DET, MM_FB_KEY_RATELIMIT_30MIN, \
				FEEDBACK_DELAY_60S, "charge glink notifier registration failed");
#endif /* CONFIG_OPLUS_FEATURE_MM_FEEDBACK */
		}
	}

	mutex_init(&switch_priv->notification_lock);
	i2c_set_clientdata(i2c, switch_priv);

	INIT_WORK(&switch_priv->usbc_analog_work,
		  typec_switch_usbc_analog_work_fn);

	BLOCKING_INIT_NOTIFIER_HEAD(&switch_priv->typec_switch_notifier);
	pr_info("%s: finished\n", __func__);

	return 0;

err_data:
	pr_err("%s: finished since err\n", __func__);

/* Add for typec_switch headset detection interrupt */
	if (gpio_is_valid(switch_priv->hs_det_pin)) {
		gpio_free(switch_priv->hs_det_pin);
	}
	devm_kfree(&i2c->dev, switch_priv);
	return rc;
}

static void typec_switch_remove(struct i2c_client *i2c)
{
/*add for 3rd protocal stack notifier*/
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	int ret = 0;
#endif
	struct typec_switch_priv *switch_priv =
			(struct typec_switch_priv *)i2c_get_clientdata(i2c);

	if (!switch_priv)
		return;

/*add for 3rd protocal stack notifier*/
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	if (switch_priv->tcpc) {
		ret = unregister_tcp_dev_notifier(switch_priv->tcpc, &switch_priv->nb, TCP_NOTIFY_TYPE_ALL);
		if (ret < 0) {
			pr_err("%s unregister tcpc notifier fail\n", __func__);
		}
	}
#endif
	typec_mux_unregister(switch_priv->mux);
	typec_switch_usbc_update_settings(switch_priv, 0x18, 0x98);
	cancel_work_sync(&switch_priv->usbc_analog_work);
	pm_relax(switch_priv->dev);
	mutex_destroy(&switch_priv->notification_lock);
/* free gpio and switch_priv */
	if (gpio_is_valid(switch_priv->hs_det_pin)) {
		gpio_free(switch_priv->hs_det_pin);
	}
	/*add WAS4783 support*/
	if (switch_priv->vendor == WAS4783 && switch_priv->chg_registration) {
		unregister_chg_glink_notifier(&switch_priv->chg_nb);
	}
	if (switch_priv->vendor == DIO4483) {
		unregister_chg_glink_notifier(&switch_priv->chg_nb);
	}
	mutex_destroy(&switch_priv->noti_lock);
	devm_kfree(&i2c->dev, switch_priv);
	dev_set_drvdata(&i2c->dev, NULL);
}

/* Add for reset codec */
static void typec_switch_shutdown(struct i2c_client *i2c) {
	struct typec_switch_priv *switch_priv =
		(struct typec_switch_priv *)i2c_get_clientdata(i2c);

	if (!switch_priv) {
		return;
	}

	pr_info("%s: recover all register while shutdown\n", __func__);

	/* reset DIO4480 */
	if ((switch_priv->vendor == DIO4480) || (switch_priv->vendor == DIO4483)) {
		regmap_write(switch_priv->regmap, DEFAULT_REG_RESET, 0x01);
		return;
	}

	typec_switch_update_reg_defaults(switch_priv->regmap);

	return;
}

static const struct of_device_id typec_switch_i2c_dt_match[] = {
	{
		.compatible = "oplus,typec-switch-i2c",
	},
	{
		.compatible = "oplus,fsa4480-i2c",
	},
	{
		.compatible = "oplus,dio4480-i2c",
	},
	{
		.compatible = "oplus,was4783-i2c",
	},
	{}
};

static struct i2c_driver typec_switch_i2c_driver = {
	.driver = {
		.name = TYPEC_SWITCH_I2C_NAME,
		.of_match_table = typec_switch_i2c_dt_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = typec_switch_probe,
	.remove = typec_switch_remove,
/* Add for reset codec */
	.shutdown = typec_switch_shutdown,
};

static int __init typec_switch_init(void)
{
	int rc;

	pr_info("%s(): enter\n", __func__);
	rc = i2c_add_driver(&typec_switch_i2c_driver);
	if (rc)
		pr_err("typec_switch: Failed to register I2C driver: %d\n", rc);

	return rc;
}
module_init(typec_switch_init);

static void __exit typec_switch_exit(void)
{
	i2c_del_driver(&typec_switch_i2c_driver);
}
module_exit(typec_switch_exit);

MODULE_DESCRIPTION("TypeC Switch I2C driver");
MODULE_LICENSE("GPL");

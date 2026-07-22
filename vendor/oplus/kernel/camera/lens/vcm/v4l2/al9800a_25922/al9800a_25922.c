// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 MediaTek Inc.

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#define FPX "al9800a_25922 - "

#define AL9800A_NAME					"al9800a_25922"
#define AL9800A_MAX_FOCUS_POS			1023
#define AL9800A_ORIGIN_FOCUS_POS		512
#define AL9800A_MOVE_STEPS			  	50
#define AL9800A_LOCK_FOCUS_POS			675
/*
 * This sets the minimum granularity for the focus positions.
 * A value of 1 gives maximum accuracy for a desired focus position
 */
#define AL9800A_FOCUS_STEPS				1
#define AL9800A_CONTROL_REG				0x02
#define AL9800A_SET_POSITION_ADDR		0x03
#define AL9800A_STATUS_REG				0x05

#define AL9800A_CONTROL_POWER_DOWN		0x01
#define AL9800A_AAC_MODE_EN				BIT(1)

#define AL9800A_CMD_DELAY				0xff
#define AL9800A_CTRL_DELAY_US			5000
/*
 * This acts as the minimum granularity of lens movement.
 * Keep this value power of 2, so the control steps can be
 * uniformly adjusted for gradual lens movement, with desired
 * number of control steps.
 */

#define AL9800A_MOVE_DELAY_US      1000
#define AL9800A_INIT_DELAY_US      1000
#define AL9800A_SET_VOLTAGE        2800000

#define VCM_IOC_POWER_ON         _IO('V', BASE_VIDIOC_PRIVATE + 4)
#define VCM_IOC_POWER_OFF        _IO('V', BASE_VIDIOC_PRIVATE + 5)

#define VIDIOC_MTK_SET_LOCK      _IOWR('V', BASE_VIDIOC_PRIVATE + 6, int)

/* AL9800A device structure */
struct AL9800A_device {
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_subdev sd;
	struct v4l2_ctrl *focus;
	struct regulator *vin;
	struct regulator *vdd;
	struct pinctrl *vcamaf_pinctrl;
	struct pinctrl_state *vcamaf_on;
	struct pinctrl_state *vcamaf_off;
	/* active or standby mode */
	bool active;
};

static inline struct AL9800A_device *to_AL9800A_vcm(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct AL9800A_device, ctrls);
}

static inline struct AL9800A_device *sd_to_AL9800A_vcm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct AL9800A_device, sd);
}

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
	unsigned char delay;
};

static struct regval_list AL9800A_init_regs[] = {
    {0x02, 0x10, 0},
    {0x10, 0x00, 0},
    {0x02, 0x01, 0},
    {0x02, 0x00, 1},
    {0x02, 0x02, 0},
    {0x06, 0x40, 0},
    {0x07, 0x04, 1},
};

static int AL9800A_write_smbus(struct AL9800A_device *AL9800A, unsigned char reg,
			      unsigned char value)
{
	struct i2c_client *client = v4l2_get_subdevdata(&AL9800A->sd);
	int ret = 0;

	if (reg == AL9800A_CMD_DELAY  && value == AL9800A_CMD_DELAY)
		usleep_range(AL9800A_CTRL_DELAY_US,
			     AL9800A_CTRL_DELAY_US + 100);
	else
		ret = i2c_smbus_write_byte_data(client, reg, value);
	return ret;
}

static int AL9800A_write_array(struct AL9800A_device *AL9800A,
			      struct regval_list *vals, u32 len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		pr_info(FPX "Init write [%d, %d]", vals[i].reg_num, vals[i].value);
		ret = AL9800A_write_smbus(AL9800A, vals[i].reg_num,
					 vals[i].value);
		if (ret < 0)
			return ret;

		if(i == 3 || i == 6){
			usleep_range(vals[i].delay * AL9800A_INIT_DELAY_US,
				vals[i].delay * AL9800A_INIT_DELAY_US + 1000);
		}
	}
	return 0;
}

static int AL9800A_set_position(struct AL9800A_device *AL9800A, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&AL9800A->sd);

	if (!AL9800A->active) {
		pr_info(FPX "AL9800A set position is %d, active is %d", val, AL9800A->active);
		return 0;
	}
	pr_info(FPX "AL9800A set position is %d", val);

	return i2c_smbus_write_word_data(client, AL9800A_SET_POSITION_ADDR,
					 swab16(val*2));
}

static int AL9800A_release(struct AL9800A_device *AL9800A)
{
	int ret, val;
	int diff_dac = 0;
	int nStep_count = 0;
	int i = 0;
	struct i2c_client *client = v4l2_get_subdevdata(&AL9800A->sd);

	if (!AL9800A->active) {
		return 0;
	}
	pr_info(FPX "%s +\n", __func__);
	if (AL9800A->focus->val <= 0) {
		AL9800A->focus->val = 512;
	}
	diff_dac = AL9800A_ORIGIN_FOCUS_POS - AL9800A->focus->val;

	nStep_count = (diff_dac < 0 ? (diff_dac*(-1)) : diff_dac) /
		AL9800A_MOVE_STEPS;

	val = AL9800A->focus->val;

	for (i = 0; i < nStep_count; ++i) {
		val += (diff_dac < 0 ? (AL9800A_MOVE_STEPS*(-1)) : AL9800A_MOVE_STEPS);
		ret = AL9800A_set_position(AL9800A, val);
		if (ret) {
			pr_info(FPX "%s I2C failure: %d",
				__func__, ret);
			return ret;
		}
		usleep_range(AL9800A_MOVE_DELAY_US,
			     AL9800A_MOVE_DELAY_US + 1000);
	}

	// last step to origin
	ret = AL9800A_set_position(AL9800A, AL9800A_ORIGIN_FOCUS_POS);
	if (ret) {
		pr_info(FPX "%s I2C failure: %d",
				__func__, ret);
		return ret;
	}

    ret = i2c_smbus_write_byte_data(client, AL9800A_CONTROL_REG,
                    AL9800A_CONTROL_POWER_DOWN);
    if (ret) {
        return ret;
    }
    AL9800A->active = false;
    pr_info(FPX "%s -\n", __func__);
    return 0;
}

static int AL9800A_init(struct AL9800A_device *AL9800A)
{
    int ret, val;
    if (AL9800A->active) {
        return 0;
    }
    pr_info(FPX "%s +\n", __func__);

    ret = AL9800A_write_array(AL9800A, AL9800A_init_regs,
                    ARRAY_SIZE(AL9800A_init_regs));
	if (ret)
		return ret;
	AL9800A->active = true;
	val = AL9800A_ORIGIN_FOCUS_POS;
    ret = AL9800A_set_position(AL9800A, val);
	if (ret) {
		return ret;
	}
	pr_info(FPX "%s -\n", __func__);
    return 0;
}

/* Power handling */
static int AL9800A_power_off(struct AL9800A_device *AL9800A)
{
	int ret;

	pr_info(FPX "%s\n", __func__);

	ret = AL9800A_release(AL9800A);
	if (ret)
		pr_info(FPX "AL9800A release failed!\n");

	ret = regulator_disable(AL9800A->vin);
	if (ret)
		return ret;

	ret = regulator_disable(AL9800A->vdd);
	if (ret)
		return ret;

	if (AL9800A->vcamaf_pinctrl && AL9800A->vcamaf_off)
		ret = pinctrl_select_state(AL9800A->vcamaf_pinctrl,
					AL9800A->vcamaf_off);

	return ret;
}

static int AL9800A_power_on(struct AL9800A_device *AL9800A)
{
	int ret;

	pr_info(FPX "%s\n", __func__);

	regulator_set_voltage(AL9800A->vin, AL9800A_SET_VOLTAGE, AL9800A_SET_VOLTAGE);
	ret = regulator_enable(AL9800A->vin);
	if (ret < 0)
		return ret;

	if (AL9800A->vcamaf_pinctrl && AL9800A->vcamaf_on)
		ret = pinctrl_select_state(AL9800A->vcamaf_pinctrl,
					AL9800A->vcamaf_on);

	if (ret < 0)
		return ret;

	/*
	 * TODO(b/139784289): Confirm hardware requirements and adjust/remove
	 * the delay.
	 */
	usleep_range(AL9800A_CTRL_DELAY_US, AL9800A_CTRL_DELAY_US + 100);

	ret = AL9800A_init(AL9800A);
	if (ret < 0)
		goto fail;

	return 0;

fail:
	regulator_disable(AL9800A->vin);
	regulator_disable(AL9800A->vdd);
	if (AL9800A->vcamaf_pinctrl && AL9800A->vcamaf_off) {
		pinctrl_select_state(AL9800A->vcamaf_pinctrl,
				AL9800A->vcamaf_off);
	}

	return ret;
}

static int AL9800A_set_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	int loop_time = 0, status = 0;
	struct AL9800A_device *AL9800A = to_AL9800A_vcm(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		/*wait for I2C bus idle*/
		while (loop_time < 20)
		{
			status = i2c_smbus_read_byte_data(v4l2_get_subdevdata(&AL9800A->sd), AL9800A_STATUS_REG);
			status = status & 0x01;//get reg 05 status
			pr_info(FPX "AL9800A 0x05 status:%x", status);
			if(status == 0){
				break;
			}
			loop_time++;
			usleep_range(AL9800A_CTRL_DELAY_US, AL9800A_CTRL_DELAY_US + 100);
		}
		pr_info(FPX "pos(%d)\n", ctrl->val);
		ret = AL9800A_set_position(AL9800A, ctrl->val);
		if (ret) {
			pr_info(FPX "%s I2C failure: %d",
				__func__, ret);
			return ret;
		}
	}
	return 0;
}

static const struct v4l2_ctrl_ops AL9800A_vcm_ctrl_ops = {
	.s_ctrl = AL9800A_set_ctrl,
};

static int AL9800A_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int ret;
	struct AL9800A_device *AL9800A = sd_to_AL9800A_vcm(sd);

	pr_info(FPX "%s\n", __func__);

	ret = AL9800A_power_on(AL9800A);
	if (ret < 0) {
		pr_info(FPX "%s power on fail, ret = %d",
			__func__, ret);
		return ret;
	}

	return 0;
}

static int AL9800A_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct AL9800A_device *AL9800A = sd_to_AL9800A_vcm(sd);

	pr_info(FPX "%s\n", __func__);

	AL9800A_power_off(AL9800A);

	return 0;
}

static int AL9800A_set_lock(struct AL9800A_device *AL9800A, void *arg)
{
	int *val = (int *)arg;
	int ret = 0;
	pr_info(FPX " setvibration lock in val:%d", *val);
	if (*val == 1) {
		ret = AL9800A_init(AL9800A);
		if (ret) {
			pr_info(FPX " %s init failure: %d", __func__, ret);
			return ret;
		}
		AL9800A_set_position(AL9800A, AL9800A_LOCK_FOCUS_POS - 100);
		usleep_range(AL9800A_MOVE_DELAY_US,
			     AL9800A_MOVE_DELAY_US + 1000);
		AL9800A_set_position(AL9800A, AL9800A_LOCK_FOCUS_POS - 50);
		usleep_range(AL9800A_MOVE_DELAY_US,
			     AL9800A_MOVE_DELAY_US + 1000);
		ret = AL9800A_set_position(AL9800A, AL9800A_LOCK_FOCUS_POS);
		if (ret) {
			pr_info(FPX " %s I2C failure: %d", __func__, ret);
			return ret;
		}
        AL9800A->focus->val = AL9800A_LOCK_FOCUS_POS;
	} else if (*val == 0){
		pr_info(FPX " setvibration unlock");
	} else {
		pr_info(FPX " setvibration error");
	}
	return 0;
}

static int AL9800A_vcm_resume(struct AL9800A_device *AL9800A)
{
    int ret = 0;
    if (AL9800A->active) {
        return 0;
    }
    ret = AL9800A_init(AL9800A);
    if (ret) {
        pr_info(FPX "%s init failure: %d", __func__, ret);
        return ret;
    }
    ret = AL9800A_set_position(AL9800A, AL9800A->focus->val);
    if (ret) {
        pr_info(FPX "%s I2C failure: %d", __func__, ret);
        return ret;
    }
    pr_info(FPX "%s exit stand by mode, active:%d\n", __func__, AL9800A->active);
    return ret;
}

static int AL9800A_vcm_suspend(struct AL9800A_device *AL9800A)
{
    int ret = 0;
    ret = AL9800A_release(AL9800A);
    pr_info(FPX "%s entry stand by mode, active:%d\n", __func__, AL9800A->active);
    return ret;
}

static long AL9800A_ops_core_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
    int ret = 0;
    struct AL9800A_device *AL9800A = sd_to_AL9800A_vcm(sd);
    pr_info(FPX "%s +\n", __func__);
    switch (cmd) {
    case VCM_IOC_POWER_ON:
        ret = AL9800A_vcm_resume(AL9800A);
        pr_info(FPX "%s VCM_IOC_POWER_ON, cmd:%d, ret: %d\n", __func__, cmd, ret);
        break;
    case VCM_IOC_POWER_OFF:
        ret = AL9800A_vcm_suspend(AL9800A);
        pr_info(FPX "%s VCM_IOC_POWER_OFF, cmd:%d, ret:%d\n", __func__, cmd, ret);
        break;
    case VIDIOC_MTK_SET_LOCK:
        AL9800A_set_lock(AL9800A, arg);
        break;
    default:
        ret = -ENOIOCTLCMD;
        break;
    }
    pr_info(FPX "%s -\n", __func__);
    return ret;
}

static const struct v4l2_subdev_internal_ops AL9800A_int_ops = {
	.open = AL9800A_open,
	.close = AL9800A_close,
};

static struct v4l2_subdev_core_ops AL9800A_ops_core = {
    .ioctl = AL9800A_ops_core_ioctl,
};
static const struct v4l2_subdev_ops AL9800A_ops = {
    .core = &AL9800A_ops_core,
};

static void AL9800A_subdev_cleanup(struct AL9800A_device *AL9800A)
{
	v4l2_async_unregister_subdev(&AL9800A->sd);
	v4l2_ctrl_handler_free(&AL9800A->ctrls);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&AL9800A->sd.entity);
#endif
}

static int AL9800A_init_controls(struct AL9800A_device *AL9800A)
{
	struct v4l2_ctrl_handler *hdl = &AL9800A->ctrls;
	const struct v4l2_ctrl_ops *ops = &AL9800A_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	AL9800A->focus = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
			  0, AL9800A_MAX_FOCUS_POS, AL9800A_FOCUS_STEPS, 0);

	if (hdl->error)
		return hdl->error;

	AL9800A->sd.ctrl_handler = hdl;

	return 0;
}

static int AL9800A_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct AL9800A_device *AL9800A;
	int ret;

	pr_info(FPX "%s\n", __func__);

	AL9800A = devm_kzalloc(dev, sizeof(*AL9800A), GFP_KERNEL);
	if (!AL9800A)
		return -ENOMEM;

	AL9800A->vin = devm_regulator_get(dev, "vin");
	if (IS_ERR(AL9800A->vin)) {
		ret = PTR_ERR(AL9800A->vin);
		if (ret != -EPROBE_DEFER)
			pr_info(FPX "cannot get vin regulator\n");
		return ret;
	}

	AL9800A->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(AL9800A->vdd)) {
		ret = PTR_ERR(AL9800A->vdd);
		if (ret != -EPROBE_DEFER)
			pr_info(FPX "cannot get vdd regulator\n");
		return ret;
	}

	AL9800A->vcamaf_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(AL9800A->vcamaf_pinctrl)) {
		ret = PTR_ERR(AL9800A->vcamaf_pinctrl);
		AL9800A->vcamaf_pinctrl = NULL;
		pr_info(FPX "cannot get pinctrl\n");
	} else {
		AL9800A->vcamaf_on = pinctrl_lookup_state(
			AL9800A->vcamaf_pinctrl, "vcamaf_on");

		if (IS_ERR(AL9800A->vcamaf_on)) {
			ret = PTR_ERR(AL9800A->vcamaf_on);
			AL9800A->vcamaf_on = NULL;
			pr_info(FPX "cannot get vcamaf_on pinctrl\n");
		}

		AL9800A->vcamaf_off = pinctrl_lookup_state(
			AL9800A->vcamaf_pinctrl, "vcamaf_off");

		if (IS_ERR(AL9800A->vcamaf_off)) {
			ret = PTR_ERR(AL9800A->vcamaf_off);
			AL9800A->vcamaf_off = NULL;
			pr_info(FPX "cannot get vcamaf_off pinctrl\n");
		}
	}

	v4l2_i2c_subdev_init(&AL9800A->sd, client, &AL9800A_ops);
	AL9800A->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	AL9800A->sd.internal_ops = &AL9800A_int_ops;

	ret = AL9800A_init_controls(AL9800A);
	if (ret)
		goto err_cleanup;

#if defined(CONFIG_MEDIA_CONTROLLER)
	ret = media_entity_pads_init(&AL9800A->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_cleanup;

	AL9800A->sd.entity.function = MEDIA_ENT_F_LENS;
#endif

	ret = v4l2_async_register_subdev(&AL9800A->sd);
	if (ret < 0)
		goto err_cleanup;

	ret = i2c_smbus_write_byte_data(client, AL9800A_CONTROL_REG,
                    AL9800A_CONTROL_POWER_DOWN);
	if (ret) {
		pr_info(FPX "AL9800A_CONTROL_POWER_DOWN fail\n");
	}

	return 0;

err_cleanup:
	AL9800A_subdev_cleanup(AL9800A);
	return ret;
}

static void AL9800A_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct AL9800A_device *AL9800A = sd_to_AL9800A_vcm(sd);

	pr_info(FPX "%s\n", __func__);

	AL9800A_subdev_cleanup(AL9800A);

	// return 0;
}

static const struct i2c_device_id AL9800A_id_table[] = {
	{ AL9800A_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, AL9800A_id_table);

static const struct of_device_id AL9800A_of_table[] = {
	{ .compatible = "oplus,al9800a_25922" },
	{ },
};
MODULE_DEVICE_TABLE(of, AL9800A_of_table);

static struct i2c_driver AL9800A_i2c_driver = {
	.driver = {
		.name = AL9800A_NAME,
		.of_match_table = AL9800A_of_table,
	},
	.probe  = AL9800A_probe,
	.remove = AL9800A_remove,
	.id_table = AL9800A_id_table,
};

module_i2c_driver(AL9800A_i2c_driver);

MODULE_AUTHOR("Dongchun Zhu <dongchun.zhu@mediatek.com>");
MODULE_DESCRIPTION("AL9800A VCM driver");
MODULE_LICENSE("GPL v2");

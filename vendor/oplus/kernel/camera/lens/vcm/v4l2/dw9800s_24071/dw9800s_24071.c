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
#include <linux/mutex.h>

#define DW9800S_NAME				"dw9800s_24071"
#define DW9800S_MAX_FOCUS_POS			1023
#define DW9800S_ORIGIN_FOCUS_POS			512
#define DW9800S_MOVE_STEPS			  	50
/*
 * This sets the minimum granularity for the focus positions.
 * A value of 1 gives maximum accuracy for a desired focus position
 */
#define DW9800S_FOCUS_STEPS			1
#define DW9800S_CONTROL_REG			0x02
#define DW9800S_SET_POSITION_ADDR		0x03
#define DW9800S_STATUS_REG				0x05

#define DW9800S_CONTROL_POWER_DOWN		BIT(0)
#define DW9800S_AAC_MODE_EN			BIT(1)

#define DW9800S_CMD_DELAY			0xff
#define DW9800S_CTRL_DELAY_US			5000
/*
 * This acts as the minimum granularity of lens movement.
 * Keep this value power of 2, so the control steps can be
 * uniformly adjusted for gradual lens movement, with desired
 * number of control steps.
 */

#define DW9800S_MOVE_DELAY_US      1000
#define DW9800S_INIT_DELAY_US      1000
#define DW9800S_SET_VOLTAGE        2800000
#define DW9800S_CONTROL_REG_MODE_DC  0x00
#define DW9800S_CONTROL_REG_MODE_SAC 0x02

#define VCM_IOC_POWER_ON         _IO('V', BASE_VIDIOC_PRIVATE + 4)
#define VCM_IOC_POWER_OFF        _IO('V', BASE_VIDIOC_PRIVATE + 5)

struct mode_info {
    int32_t mode;
    int32_t flag;
};
#define VCM_IOC_SET_MODE         _IOWR('V', BASE_VIDIOC_PRIVATE + 7, struct mode_info)

/* dw9800s device structure */
struct dw9800s_device {
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_subdev sd;
	struct v4l2_ctrl *focus;
	struct regulator *vin;
	struct regulator *vdd;
	struct pinctrl *vcamaf_pinctrl;
	struct pinctrl_state *vcamaf_on;
	struct pinctrl_state *vcamaf_off;
	struct mutex af_lock;
	/* active or standby mode */
	bool active;
};

static inline struct dw9800s_device *to_dw9800s_vcm(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct dw9800s_device, ctrls);
}

static inline struct dw9800s_device *sd_to_dw9800s_vcm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct dw9800s_device, sd);
}

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
	unsigned char delay;
};

static struct regval_list dw9800s_init_regs[] = {
    {0x02, 0x01, 0},
    {0x02, 0x00, 1},
    {0x02, 0x02, 0},
    {0x06, 0x40, 0},
    {0x07, 0x08, 1},
};

static int dw9800s_write_smbus(struct dw9800s_device *dw9800s, unsigned char reg,
			      unsigned char value)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dw9800s->sd);
	int ret = 0;

	if (reg == DW9800S_CMD_DELAY  && value == DW9800S_CMD_DELAY)
		usleep_range(DW9800S_CTRL_DELAY_US,
			     DW9800S_CTRL_DELAY_US + 100);
	else
		ret = i2c_smbus_write_byte_data(client, reg, value);
	return ret;
}

static int dw9800s_write_array(struct dw9800s_device *dw9800s,
			      struct regval_list *vals, u32 len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		pr_info("Init write [%d, %d]", vals[i].reg_num, vals[i].value);
		ret = dw9800s_write_smbus(dw9800s, vals[i].reg_num,
					 vals[i].value);
		if (ret < 0)
			return ret;

		usleep_range(vals[i].delay * DW9800S_INIT_DELAY_US,
						vals[i].delay * DW9800S_INIT_DELAY_US + 1000);
	}
	return 0;
}

static int dw9800s_set_position(struct dw9800s_device *dw9800s, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dw9800s->sd);
	int ret = 0;
	if (!dw9800s->active) {
		pr_info("dw9800s set position is %d, active is %d", val, dw9800s->active);
		return 0;
	}
	if(mutex_trylock(&(dw9800s->af_lock))) {
		ret = i2c_smbus_write_word_data(client, DW9800S_SET_POSITION_ADDR, swab16(val));
		mutex_unlock(&(dw9800s->af_lock));
		pr_info("dw9800s set position is %d", val);
	} else {
		pr_info("%s af_lock is locked, dw9800s set position is  %d",__func__, val);
	}
	return ret;
}

static int dw9800s_release(struct dw9800s_device *dw9800s)
{
	int ret, val;
	int diff_dac = 0;
	int nStep_count = 0;
	int i = 0;
	struct i2c_client *client = v4l2_get_subdevdata(&dw9800s->sd);

	if (!dw9800s->active) {
		return 0;
	}
	pr_info("%s +\n", __func__);
	diff_dac = DW9800S_ORIGIN_FOCUS_POS - dw9800s->focus->val;

	nStep_count = (diff_dac < 0 ? (diff_dac*(-1)) : diff_dac) /
		DW9800S_MOVE_STEPS;

	val = dw9800s->focus->val;

	for (i = 0; i < nStep_count; ++i) {
		val += (diff_dac < 0 ? (DW9800S_MOVE_STEPS*(-1)) : DW9800S_MOVE_STEPS);
		ret = dw9800s_set_position(dw9800s, val);
		if (ret) {
			pr_info("%s I2C failure: %d",
				__func__, ret);
			return ret;
		}
		usleep_range(DW9800S_MOVE_DELAY_US,
			     DW9800S_MOVE_DELAY_US + 1000);
	}

	// last step to origin
	ret = dw9800s_set_position(dw9800s, DW9800S_ORIGIN_FOCUS_POS);
	if (ret) {
		pr_info("%s I2C failure: %d",
				__func__, ret);
		return ret;
	}

    ret = i2c_smbus_write_byte_data(client, DW9800S_CONTROL_REG,
                    DW9800S_CONTROL_POWER_DOWN);
    if (ret) {
        return ret;
    }
    dw9800s->active = false;
    pr_info("%s -\n", __func__);
    return 0;
}

static int dw9800s_init(struct dw9800s_device *dw9800s)
{
    int ret, val;
    if (dw9800s->active) {
        return 0;
    }
    pr_info("%s +\n", __func__);

    ret = dw9800s_write_array(dw9800s, dw9800s_init_regs,
                    ARRAY_SIZE(dw9800s_init_regs));
	if (ret)
		return ret;
	dw9800s->active = true;
	val = DW9800S_ORIGIN_FOCUS_POS;
    ret = dw9800s_set_position(dw9800s, val);
	if (ret) {
		return ret;
	}
	pr_info("%s -\n", __func__);
    return 0;
}

/* Power handling */
static int dw9800s_power_off(struct dw9800s_device *dw9800s)
{
	int ret;

	pr_info("%s\n", __func__);

	ret = dw9800s_release(dw9800s);
	if (ret)
		pr_info("dw9800s release failed!\n");

	ret = regulator_disable(dw9800s->vin);
	if (ret)
		return ret;

	ret = regulator_disable(dw9800s->vdd);
	if (ret)
		return ret;

	if (dw9800s->vcamaf_pinctrl && dw9800s->vcamaf_off)
		ret = pinctrl_select_state(dw9800s->vcamaf_pinctrl,
					dw9800s->vcamaf_off);

	return ret;
}

static int dw9800s_power_on(struct dw9800s_device *dw9800s)
{
	int ret;

	pr_info("%s\n", __func__);

	regulator_set_voltage(dw9800s->vin, DW9800S_SET_VOLTAGE, DW9800S_SET_VOLTAGE);
	ret = regulator_enable(dw9800s->vin);
	if (ret < 0)
		return ret;

	if (dw9800s->vcamaf_pinctrl && dw9800s->vcamaf_on)
		ret = pinctrl_select_state(dw9800s->vcamaf_pinctrl,
					dw9800s->vcamaf_on);

	if (ret < 0)
		return ret;

	/*
	 * TODO(b/139784289): Confirm hardware requirements and adjust/remove
	 * the delay.
	 */
	usleep_range(DW9800S_CTRL_DELAY_US, DW9800S_CTRL_DELAY_US + 100);

	ret = dw9800s_init(dw9800s);
	if (ret < 0)
		goto fail;

	return 0;

fail:
	regulator_disable(dw9800s->vin);
	regulator_disable(dw9800s->vdd);
	if (dw9800s->vcamaf_pinctrl && dw9800s->vcamaf_off) {
		pinctrl_select_state(dw9800s->vcamaf_pinctrl,
				dw9800s->vcamaf_off);
	}

	return ret;
}

static int dw9800s_set_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct dw9800s_device *dw9800s = to_dw9800s_vcm(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		ret = dw9800s_set_position(dw9800s, ctrl->val);
		if (ret) {
			pr_info("%s I2C failure: %d",
				__func__, ret);
			return ret;
		}
	}
	return 0;
}

static const struct v4l2_ctrl_ops dw9800s_vcm_ctrl_ops = {
	.s_ctrl = dw9800s_set_ctrl,
};

static int dw9800s_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int ret;
	struct dw9800s_device *dw9800s = sd_to_dw9800s_vcm(sd);
	pr_info("%s\n", __func__);

	ret = dw9800s_power_on(dw9800s);
	if (ret < 0) {
		pr_info("%s power on fail, ret = %d",
			__func__, ret);
		return ret;
	}

	return 0;
}

static int dw9800s_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct dw9800s_device *dw9800s = sd_to_dw9800s_vcm(sd);

	pr_info("%s\n", __func__);

	dw9800s_power_off(dw9800s);

	return 0;
}

static int dw9800s_vcm_resume(struct dw9800s_device *dw9800s)
{
    int ret = 0;
    if (dw9800s->active) {
        return 0;
    }
    ret = dw9800s_init(dw9800s);
    if (ret) {
        pr_info("%s init failure: %d", __func__, ret);
        return ret;
    }
    ret = dw9800s_set_position(dw9800s, dw9800s->focus->val);
    if (ret) {
        pr_info("%s I2C failure: %d", __func__, ret);
        return ret;
    }
    pr_info("%s exit stand by mode, active:%d\n", __func__, dw9800s->active);
    return ret;
}

static int dw9800s_vcm_suspend(struct dw9800s_device *dw9800s)
{
    int ret = 0;
    ret = dw9800s_release(dw9800s);
    pr_info("%s entry stand by mode, active:%d\n", __func__, dw9800s->active);
    return ret;
}
enum ACTUATOR_MODE {
    ACTUATOR_MODE_INVALID = -1,
    ACTUATOR_MODE_DIRECT = 0,
    ACTUATOR_MODE_LSC = 1,
    ACTUATOR_MODE_SAC2 = 2,
    ACTUATOR_MODE_SAC3 = 3,
    ACTUATOR_MODE_SAC4 = 4,
    ACTUATOR_MODE_SAC5 = 5,
    ACTUATOR_MODE_MAX,
};
static bool dw9800s_is_busy(struct dw9800s_device *dw9800s)
{
    unsigned char busy_reg;
    bool busy_status = false;
    struct i2c_client *client = v4l2_get_subdevdata(&dw9800s->sd);
    busy_reg = i2c_smbus_read_byte_data(client, 0x05);
    pr_info("%s, busy:0x%02x", __func__, busy_reg);
    if (busy_reg < 0)
       return false;
    busy_status = (busy_reg & 0x01) ? true : false;
    return busy_status;
}
static int dw9800s_actuator_mode(struct dw9800s_device *dw9800s,  unsigned char mode)
{
    int32_t flag = false;
    int ret = 0;
    struct i2c_client *client = v4l2_get_subdevdata(&dw9800s->sd);
    mutex_lock(&(dw9800s->af_lock));
    if(!dw9800s_is_busy(dw9800s)) {
        ret = i2c_smbus_write_byte_data(client, DW9800S_CONTROL_REG, mode);
        if(ret < 0) {
            pr_info("%s,Failed to write  DW9800S_CONTROL_REG", __func__);
            flag = false;
        } else {
            flag = true;
        }
    } else {
        flag = false;
        pr_info("%s, busy status", __func__);
    }
    mutex_unlock(&(dw9800s->af_lock));
    pr_info("%s, mode:%d", __func__, mode);
    return flag;
}
static int dw9800s_vcm_set_mode(struct dw9800s_device *dw9800s, void *arg)
{
    struct mode_info *config = (struct mode_info*)arg;
    switch(config->mode) {
        case ACTUATOR_MODE_DIRECT:
            config->flag = dw9800s_actuator_mode(dw9800s, DW9800S_CONTROL_REG_MODE_DC);
            break;
        case ACTUATOR_MODE_SAC3:
            config->flag = dw9800s_actuator_mode(dw9800s, DW9800S_CONTROL_REG_MODE_SAC);
            break;
        default:
            config->flag = 0;
            pr_info("%s Invalid motor mode\n", __func__);
            break;
    }
    pr_info("%s actuator_Mode:%d, updata_flag:%d", __func__, config->mode, config->flag);
    return 0;
}

static long dw9800s_ops_core_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
    int ret = 0;
    struct dw9800s_device *dw9800s = sd_to_dw9800s_vcm(sd);
    pr_info("%s +\n", __func__);
    switch (cmd) {
    case VCM_IOC_POWER_ON:
        ret = dw9800s_vcm_resume(dw9800s);
        pr_info("%s VCM_IOC_POWER_ON, cmd:%d, ret: %d\n", __func__, cmd, ret);
        break;
    case VCM_IOC_POWER_OFF:
        ret = dw9800s_vcm_suspend(dw9800s);
        pr_info("%s VCM_IOC_POWER_OFF, cmd:%d, ret:%d\n", __func__, cmd, ret);
        break;
    case VCM_IOC_SET_MODE:
        ret = dw9800s_vcm_set_mode(dw9800s, arg);
        pr_info("%s VCM_IOC_SET_MODE, cmd:%d, ret:%d \n", __func__, cmd, ret);
        break;
    default:
        ret = -ENOIOCTLCMD;
        break;
    }
    pr_info("%s -\n", __func__);
    return ret;
}

static const struct v4l2_subdev_internal_ops dw9800s_int_ops = {
	.open = dw9800s_open,
	.close = dw9800s_close,
};

static struct v4l2_subdev_core_ops dw9800s_ops_core = {
    .ioctl = dw9800s_ops_core_ioctl,
};
static const struct v4l2_subdev_ops dw9800s_ops = {
    .core = &dw9800s_ops_core,
};

static void dw9800s_subdev_cleanup(struct dw9800s_device *dw9800s)
{
	v4l2_async_unregister_subdev(&dw9800s->sd);
	v4l2_ctrl_handler_free(&dw9800s->ctrls);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&dw9800s->sd.entity);
#endif
}

static int dw9800s_init_controls(struct dw9800s_device *dw9800s)
{
	struct v4l2_ctrl_handler *hdl = &dw9800s->ctrls;
	const struct v4l2_ctrl_ops *ops = &dw9800s_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	dw9800s->focus = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
			  0, DW9800S_MAX_FOCUS_POS, DW9800S_FOCUS_STEPS, 0);

	if (hdl->error)
		return hdl->error;

	dw9800s->sd.ctrl_handler = hdl;

	return 0;
}

static int dw9800s_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct dw9800s_device *dw9800s;
	int ret;
	pr_info("%s\n", __func__);

	dw9800s = devm_kzalloc(dev, sizeof(*dw9800s), GFP_KERNEL);
	if (!dw9800s)
		return -ENOMEM;
	mutex_init(&(dw9800s->af_lock));
	dw9800s->vin = devm_regulator_get(dev, "vin");
	if (IS_ERR(dw9800s->vin)) {
		ret = PTR_ERR(dw9800s->vin);
		if (ret != -EPROBE_DEFER)
			pr_info("cannot get vin regulator\n");
		return ret;
	}

	dw9800s->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(dw9800s->vdd)) {
		ret = PTR_ERR(dw9800s->vdd);
		if (ret != -EPROBE_DEFER)
			pr_info("cannot get vdd regulator\n");
		return ret;
	}

	dw9800s->vcamaf_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(dw9800s->vcamaf_pinctrl)) {
		ret = PTR_ERR(dw9800s->vcamaf_pinctrl);
		dw9800s->vcamaf_pinctrl = NULL;
		pr_info("cannot get pinctrl\n");
	} else {
		dw9800s->vcamaf_on = pinctrl_lookup_state(
			dw9800s->vcamaf_pinctrl, "vcamaf_on");

		if (IS_ERR(dw9800s->vcamaf_on)) {
			ret = PTR_ERR(dw9800s->vcamaf_on);
			dw9800s->vcamaf_on = NULL;
			pr_info("cannot get vcamaf_on pinctrl\n");
		}

		dw9800s->vcamaf_off = pinctrl_lookup_state(
			dw9800s->vcamaf_pinctrl, "vcamaf_off");

		if (IS_ERR(dw9800s->vcamaf_off)) {
			ret = PTR_ERR(dw9800s->vcamaf_off);
			dw9800s->vcamaf_off = NULL;
			pr_info("cannot get vcamaf_off pinctrl\n");
		}
	}

	v4l2_i2c_subdev_init(&dw9800s->sd, client, &dw9800s_ops);
	dw9800s->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dw9800s->sd.internal_ops = &dw9800s_int_ops;

	ret = dw9800s_init_controls(dw9800s);
	if (ret)
		goto err_cleanup;

#if defined(CONFIG_MEDIA_CONTROLLER)
	ret = media_entity_pads_init(&dw9800s->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_cleanup;

	dw9800s->sd.entity.function = MEDIA_ENT_F_LENS;
#endif

	ret = v4l2_async_register_subdev(&dw9800s->sd);
	if (ret < 0)
		goto err_cleanup;

	return 0;

err_cleanup:
	dw9800s_subdev_cleanup(dw9800s);
	return ret;
}

static void dw9800s_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9800s_device *dw9800s = sd_to_dw9800s_vcm(sd);
	pr_info("%s\n", __func__);

	dw9800s_subdev_cleanup(dw9800s);

	// return 0;
}

static const struct i2c_device_id dw9800s_id_table[] = {
	{ DW9800S_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, dw9800s_id_table);

static const struct of_device_id dw9800s_of_table[] = {
	{ .compatible = "oplus,dw9800s_24071" },
	{ },
};
MODULE_DEVICE_TABLE(of, dw9800s_of_table);

static struct i2c_driver dw9800s_i2c_driver = {
	.driver = {
		.name = DW9800S_NAME,
		.of_match_table = dw9800s_of_table,
	},
	.probe  = dw9800s_probe,
	.remove = dw9800s_remove,
	.id_table = dw9800s_id_table,
};

module_i2c_driver(dw9800s_i2c_driver);

MODULE_AUTHOR("Dongchun Zhu <dongchun.zhu@mediatek.com>");
MODULE_DESCRIPTION("DW9800S VCM driver");
MODULE_LICENSE("GPL v2");

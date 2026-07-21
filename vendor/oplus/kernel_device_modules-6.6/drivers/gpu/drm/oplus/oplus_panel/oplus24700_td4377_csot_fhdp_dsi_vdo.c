// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <linux/delay.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <soc/oplus/device_info.h>
#include <soc/oplus/system/boot_mode.h>
#include <mtk_boot_common.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif
#if IS_ENABLED(CONFIG_DRM_OPLUS_PANEL_NOTIFY)
#include <linux/msm_drm_notify.h>
#elif IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY)
#include <linux/soc/qcom/panel_event_notifier.h>
#include <linux/msm_drm_notify.h>
#include <drm/drm_panel.h>
#elif IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
#include <linux/msm_drm_notify.h>
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
#include <linux/mtk_panel_ext.h>
#include <linux/mtk_disp_notify.h>
#endif
//#include "../oplus/oplus_display_mtk_debug.h"

#if defined(CONFIG_RT4831A_I2C)
#include "../../../misc/mediatek/gate_ic/gate_i2c.h"
#endif
#include "../oplus_display_onscreenfingerprint.h"
//#include "ktz8866.h"
#define CHANGE_FPS_EN 1

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
#ifndef CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY
extern enum boot_mode_t get_boot_mode(void);
#else
extern int get_boot_mode(void);
#endif
#else
/* extern unsigned int silence_mode; */
extern int get_boot_mode(void);
#endif
#if IS_ENABLED(CONFIG_TOUCHPANEL_NOTIFY)
extern int (*tp_gesture_enable_notifier)(unsigned int tp_index);
#endif
#ifdef LCD_LOAD_TP_FW
extern void lcd_queue_load_tp_fw(void);
#endif

#define MAX_NORMAL_BRIGHTNESS 3210
#define MULTIPLE_BRIGHTNESS   1070
#define MTK_DISP_EVENT_FOR_TOUCH		0x10
static int esd_brightness;
unsigned long esd_flag = 0;
unsigned int g_shutdown_flag = 0;
//static int current_esd_fps;

/* enable this to check panel self -bist pattern */
/* #define PANEL_BIST_PATTERN */
/****************TPS65132***********/
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
//#include "lcm_i2c.h"
//#include "../bias/ocp2130_drv.h"

static char bl_tb0[] = { 0x51, 0x0F, 0xFF};
//static char bl_open[] = { 0x53, 0x2C };
/*esd check*/
//static char fps_open[] = 	{ 0xB9, 0x83, 0x10, 0x21, 0x55, 0x00 };
//static char fps_BD[] =     	{ 0xBD, 0x00 };
//static char fps120_e2[] =   { 0xE2, 0x00 };
//static char fps60_e2[] =    { 0xE2, 0x50 };
//static char fps40_e2[] =    { 0xE2, 0x60 };
//static char fps_close[] =    { 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00 };
//TO DO: You have to do that remove macro BYPASSI2C and solve build error
//otherwise voltage will be unstable
#ifdef HIMAX_WAKE_UP
extern uint8_t wake_flag_drm;
#endif
extern unsigned int oplus_max_normal_brightness;

static int first_set_bl;
static int cabc_status = 3;
static int esd_last_level;

#define HFP (165)
#define HSA (4)
#define HBP (40)
#define VSA (4)
#define VBP (40)
#define VAC (2400)
#define HAC (1080)
#define VFP_45hz (2480)
#define VFP_48hz (2180)
#define VFP_50hz (2000)
#define VFP_60hz (1260)
#define VFP_90hz (80)

#define PLL_CLOCK (380)
#define DATA_RATE (760)

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos;
	struct gpio_desc *bias_neg;
	bool prepared;
	bool enabled;

	unsigned int gate_ic;

	int error;
};


#define lcm_dcs_write_seq(ctx, seq...)                                         \
	({                                                                     \
		const u8 d[] = { seq };                                        \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

#define lcm_dcs_write_seq_static(ctx, seq...)                                  \
	({                                                                     \
		static const u8 d[] = { seq };                                 \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

#ifdef PANEL_SUPPORT_READBACK
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret,
			 cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = { 0 };
	static int ret;

	pr_info("%s+\n", __func__);

	if (ret == 0) {
		ret = lcm_dcs_read(ctx, 0x0A, buffer, 1);
		pr_info("%s  0x%08x\n", __func__, buffer[0] | (buffer[1] << 8));
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

static void lcm_panel_init(struct lcm *ctx)
{
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(5 * 1000, 5001);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(5 * 1000, 5001);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(5 * 1000, 5001);
	gpiod_set_value(ctx->reset_gpio, 1);

#ifdef LCD_LOAD_TP_FW
	lcd_queue_load_tp_fw();
#endif
	usleep_range(15*1000, 15001);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	first_set_bl = 1;

    lcm_dcs_write_seq_static(ctx, 0xB0, 0x00);
    lcm_dcs_write_seq_static(ctx, 0xF5, 0xE6, 0x5a);
    usleep_range(200, 210);
    lcm_dcs_write_seq_static(ctx, 0xE6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x00, 0x00, 0x00, 0x00);
    usleep_range(200, 210);
    lcm_dcs_write_seq_static(ctx, 0xB0, 0x03);
    lcm_dcs_write_seq_static(ctx, 0xB0, 0x04);
    lcm_dcs_write_seq_static(ctx, 0xD6, 0x00);
    lcm_dcs_write_seq_static(ctx, 0x35, 0x01);
    lcm_dcs_write_seq_static(ctx, 0x53, 0x2c);
    lcm_dcs_write_seq_static(ctx, 0x53, 0x2c);
    lcm_dcs_write_seq_static(ctx, 0x51, 0x00, 0x00);
    usleep_range(200, 210);
    lcm_dcs_write_seq_static(ctx, 0x55, 0x00);
    lcm_dcs_write_seq_static(ctx, 0xB0, 0x03);


    lcm_dcs_write_seq_static(ctx, 0x11);
    usleep_range(105000, 105100);
    lcm_dcs_write_seq_static(ctx, 0x29);
    usleep_range(5000, 5100);
    pr_info("%s-\n", __func__);
}

static void lcm_init_set_cabc(struct lcm *ctx) {

  	if (cabc_status == 1) {
  		lcm_dcs_write_seq_static(ctx, 0x55, 0x01);
  	} else if (cabc_status == 2) {
  		lcm_dcs_write_seq_static(ctx, 0x55, 0x02);
  	} else if (cabc_status == 3) {
  		lcm_dcs_write_seq_static(ctx, 0x55, 0x03);
  	} else if (cabc_status == 0) {
  		lcm_dcs_write_seq_static(ctx, 0x55, 0x00);
  	}

	pr_info("%s- cabc_init_mode=%d\n", __func__,cabc_status);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	pr_info("%s line = %d\n", __func__,__LINE__);
	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int mode = 0;
	//int blank = 0;
	int flag_poweroff = 1;

	pr_info("%s+\n", __func__);
	if (!ctx->prepared)
		return 0;

	mode = get_boot_mode();
	if ((mode != MSM_BOOT_MODE__FACTORY) && (mode != MSM_BOOT_MODE__RF) && (mode != MSM_BOOT_MODE__WLAN)) {
		if(tp_gesture_enable_notifier && tp_gesture_enable_notifier(0) && (g_shutdown_flag == 0) && (esd_flag == 0)) {
			flag_poweroff = 0;
		} else {
			flag_poweroff = 1;
		}
	}

	msleep(8);
	lcm_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(20);

	lcm_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(90);
	pr_err("[TP]flag_poweroff = %d\n",flag_poweroff);
	if (flag_poweroff == 1) {
		/*ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);*/

		ctx->bias_neg =
			devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->bias_neg, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);

		usleep_range(5000, 5001);

		ctx->bias_pos =
			devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->bias_pos, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);
	}
	ctx->error = 0;
	ctx->prepared = false;
	pr_info("%s-\n", __func__);
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;
	//int blank = 0;

	pr_info("%s+\n", __func__);
	if (ctx->prepared)
		return 0;

	// lcd reset H -> L -> L
	//ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	//gpiod_set_value(ctx->reset_gpio, 1);
	//usleep_range(10000, 10001);
	//gpiod_set_value(ctx->reset_gpio, 0);
	//msleep(20);
	//gpiod_set_value(ctx->reset_gpio, 1);
	//devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	// end
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	usleep_range(2*1000, 2*1000+100);// 2ms

	//_bias_ic_i2c_panel_bias_enable(1);

	ctx->bias_pos =
		devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->bias_pos, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);

	usleep_range(5000, 5010);// 5ms
	ctx->bias_neg =
		devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->bias_neg, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
	usleep_range(2 * 1000, 2 * 1000+100);// 2ms

	/*#define LCD_CTL_TP_LOAD_FW 0x10
	#define LCD_CTL_CS_ON  0x19
	blank = LCD_CTL_CS_ON;
	mtk_disp_notifier_call_chain(MTK_DISP_EVENT_FOR_TOUCH, &blank);
	usleep_range(5000, 5100);
	blank = LCD_CTL_TP_LOAD_FW;
	mtk_disp_notifier_call_chain(MTK_DISP_EVENT_FOR_TOUCH, &blank);
	usleep_range(5000, 5100);*/
#ifdef HIMAX_WAKE_UP
	//if (wake_flag_drm == 0)
		//lcd_set_bl_bias_reg(ctx->dev, 1);
#else
		//lcd_set_bl_bias_reg(ctx->dev, 1);
#endif
//	_lcm_i2c_write_bytes(0x0, 0xf);
//	_lcm_i2c_write_bytes(0x1, 0xf);
	lcm_panel_init(ctx);
	lcm_init_set_cabc(ctx);
	ret = ctx->error;
	if (ret < 0) {
		lcm_unprepare(panel);
		pr_info("%s11111-\n", __func__);
	}
	ctx->prepared = true;
#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif
/*
#ifdef LCD_LOAD_TP_FW
	lcd_queue_load_tp_fw();
#endif
*/
	pr_info("%s-\n", __func__);
	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	pr_info("%s line = %d\n", __func__,__LINE__);
	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

static const struct drm_display_mode performance_mode_45hz = {
    .clock = 285616,
    .hdisplay = HAC,
    .hsync_start = HAC + HFP, //HFP
    .hsync_end = HAC + HFP + HSA, //HSA
    .htotal = HAC + HFP + HSA + HBP, //HBP
    .vdisplay = VAC,
    .vsync_start = VAC + VFP_45hz, //24700 45fps
    .vsync_end = VAC + VFP_45hz + VSA, //VSA
    .vtotal = VAC + VFP_45hz + VSA + VBP, //VBP
};

static const struct drm_display_mode performance_mode_48hz = {
    .clock = 286096,
    .hdisplay = HAC,
    .hsync_start = HAC + HFP, //HFP
    .hsync_end = HAC + HFP + HSA, //HSA
    .htotal = HAC + HFP + HSA + HBP, //HBP
    .vdisplay = VAC,
    .vsync_start = VAC + VFP_48hz, //24700 48fps
    .vsync_end = VAC + VFP_48hz + VSA, //VSA
    .vtotal = VAC + VFP_48hz + VSA + VBP, //VBP
};

static const struct drm_display_mode performance_mode_50hz = {
    .clock = 286415,
    .hdisplay = HAC,
    .hsync_start = HAC + HFP, //HFP
    .hsync_end = HAC + HFP + HSA, //HSA
    .htotal = HAC + HFP + HSA + HBP, //HBP
    .vdisplay = VAC,
    .vsync_start = VAC + VFP_50hz, //24700 50fps
    .vsync_end = VAC + VFP_50hz + VSA, //VSA
    .vtotal = VAC + VFP_50hz + VSA + VBP, //VBP
};

static const struct drm_display_mode performance_mode_60hz = {
    .clock = 286467,
    .hdisplay = HAC,
    .hsync_start = HAC + HFP, //HFP
    .hsync_end = HAC + HFP + HSA, //HSA
    .htotal = HAC + HFP + HSA + HBP, //HBP
    .vdisplay = VAC,
    .vsync_start = VAC + VFP_60hz, //24700 60fps
    .vsync_end = VAC + VFP_60hz + VSA, //VSA
    .vtotal = VAC + VFP_60hz + VSA + VBP, //VBP
};

static const struct drm_display_mode default_mode_90hz = {
    .clock = 292809,
    .hdisplay = HAC,
    .hsync_start = HAC + HFP, //HFP
    .hsync_end = HAC + HFP + HSA, //HSA
    .htotal = HAC + HFP + HSA + HBP, //HBP
    .vdisplay = VAC,
    .vsync_start = VAC + VFP_90hz, //24700 90fps
    .vsync_end = VAC + VFP_90hz + VSA, //VSA
    .vtotal = VAC + VFP_90hz + VSA + VBP, //VBP
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params_45hz = {
    .vendor = "td4377_oris_c",
    .manufacture = "24700_csot_td4377",
	//.change_fps_by_vfp_send_cmd = 1,
	//.vfp_low_power = 148,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.physical_width_um = 70000,
	.physical_height_um = 150000,
    .dsc_params = {
                    .enable = 0,
                    .bdg_dsc_enable = 1,
                    .ver = 17,
                    .slice_mode = 1,
                    .rgb_swap = 0,
                    .dsc_cfg = 34,
                    .rct_on = 1,
                    .bit_per_channel = 8,
                    .dsc_line_buf_depth = 9,
                    .bp_enable = 1,
                    .bit_per_pixel = 128,
                    .pic_height = 2400,
                    .pic_width = 1080,
                    .slice_height = 12,
                    .slice_width = 540,
                    .chunk_size = 540,
                    .xmit_delay = 512,
                    .dec_delay = 526,
                    .scale_value = 32,
                    .increment_interval = 287,
                    .decrement_interval = 7,
                    .line_bpg_offset = 12,
                    .nfl_bpg_offset = 2235,
                    .slice_bpg_offset = 2170,
                    .initial_offset = 6144,
                    .final_offset = 4336,
                    .flatness_minqp = 3,
                    .flatness_maxqp = 12,
                    .rc_model_size = 8192,
                    .rc_edge_factor = 6,
                    .rc_quant_incr_limit0 = 11,
                    .rc_quant_incr_limit1 = 11,
                    .rc_tgt_offset_hi = 3,
                    .rc_tgt_offset_lo = 3,
                    },
	.data_rate = DATA_RATE, /* 943 */
	.pll_clk = PLL_CLOCK,
	.bdg_ssc_enable = 0,
	.ssc_enable = 0,
	//.data_rate_khz = 1030000, /* 943307 */

	.oplus_display_global_dre = 1,
};

static struct mtk_panel_params ext_params_48hz = {
    .vendor = "td4377_oris_c",
    .manufacture = "24700_csot_td4377",
	//.change_fps_by_vfp_send_cmd = 1,
	//.vfp_low_power = 148,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.physical_width_um = 70000,
	.physical_height_um = 150000,
    .dsc_params = {
                    .enable = 0,
                    .bdg_dsc_enable = 1,
                    .ver = 17,
                    .slice_mode = 1,
                    .rgb_swap = 0,
                    .dsc_cfg = 34,
                    .rct_on = 1,
                    .bit_per_channel = 8,
                    .dsc_line_buf_depth = 9,
                    .bp_enable = 1,
                    .bit_per_pixel = 128,
                    .pic_height = 2400,
                    .pic_width = 1080,
                    .slice_height = 12,
                    .slice_width = 540,
                    .chunk_size = 540,
                    .xmit_delay = 512,
                    .dec_delay = 526,
                    .scale_value = 32,
                    .increment_interval = 287,
                    .decrement_interval = 7,
                    .line_bpg_offset = 12,
                    .nfl_bpg_offset = 2235,
                    .slice_bpg_offset = 2170,
                    .initial_offset = 6144,
                    .final_offset = 4336,
                    .flatness_minqp = 3,
                    .flatness_maxqp = 12,
                    .rc_model_size = 8192,
                    .rc_edge_factor = 6,
                    .rc_quant_incr_limit0 = 11,
                    .rc_quant_incr_limit1 = 11,
                    .rc_tgt_offset_hi = 3,
                    .rc_tgt_offset_lo = 3,
                    },
	.data_rate = DATA_RATE, /* 943 */
	.pll_clk = PLL_CLOCK,
	.bdg_ssc_enable = 0,
	.ssc_enable = 0,
	//.data_rate_khz = 1030000, /* 943307 */

	.oplus_display_global_dre = 1,
};

static struct mtk_panel_params ext_params_50hz = {
    .vendor = "td4377_oris_c",
    .manufacture = "24700_csot_td4377",
	//.change_fps_by_vfp_send_cmd = 1,
	//.vfp_low_power = 148,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.physical_width_um = 70000,
	.physical_height_um = 150000,
    .dsc_params = {
                    .enable = 0,
                    .bdg_dsc_enable = 1,
                    .ver = 17,
                    .slice_mode = 1,
                    .rgb_swap = 0,
                    .dsc_cfg = 34,
                    .rct_on = 1,
                    .bit_per_channel = 8,
                    .dsc_line_buf_depth = 9,
                    .bp_enable = 1,
                    .bit_per_pixel = 128,
                    .pic_height = 2400,
                    .pic_width = 1080,
                    .slice_height = 12,
                    .slice_width = 540,
                    .chunk_size = 540,
                    .xmit_delay = 512,
                    .dec_delay = 526,
                    .scale_value = 32,
                    .increment_interval = 287,
                    .decrement_interval = 7,
                    .line_bpg_offset = 12,
                    .nfl_bpg_offset = 2235,
                    .slice_bpg_offset = 2170,
                    .initial_offset = 6144,
                    .final_offset = 4336,
                    .flatness_minqp = 3,
                    .flatness_maxqp = 12,
                    .rc_model_size = 8192,
                    .rc_edge_factor = 6,
                    .rc_quant_incr_limit0 = 11,
                    .rc_quant_incr_limit1 = 11,
                    .rc_tgt_offset_hi = 3,
                    .rc_tgt_offset_lo = 3,
                    },
	.data_rate = DATA_RATE, /* 943 */
	.pll_clk = PLL_CLOCK,
	.bdg_ssc_enable = 0,
	.ssc_enable = 0,
	//.data_rate_khz = 1030000, /* 943307 */

	.oplus_display_global_dre = 1,
};

static struct mtk_panel_params ext_params_60hz = {
    .vendor = "td4377_oris_c",
    .manufacture = "24700_csot_td4377",
	//.change_fps_by_vfp_send_cmd = 1,
	//.vfp_low_power = 148,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.physical_width_um = 70000,
	.physical_height_um = 150000,
    .dsc_params = {
                    .enable = 0,
                    .bdg_dsc_enable = 1,
                    .ver = 17,
                    .slice_mode = 1,
                    .rgb_swap = 0,
                    .dsc_cfg = 34,
                    .rct_on = 1,
                    .bit_per_channel = 8,
                    .dsc_line_buf_depth = 9,
                    .bp_enable = 1,
                    .bit_per_pixel = 128,
                    .pic_height = 2400,
                    .pic_width = 1080,
                    .slice_height = 12,
                    .slice_width = 540,
                    .chunk_size = 540,
                    .xmit_delay = 512,
                    .dec_delay = 526,
                    .scale_value = 32,
                    .increment_interval = 287,
                    .decrement_interval = 7,
                    .line_bpg_offset = 12,
                    .nfl_bpg_offset = 2235,
                    .slice_bpg_offset = 2170,
                    .initial_offset = 6144,
                    .final_offset = 4336,
                    .flatness_minqp = 3,
                    .flatness_maxqp = 12,
                    .rc_model_size = 8192,
                    .rc_edge_factor = 6,
                    .rc_quant_incr_limit0 = 11,
                    .rc_quant_incr_limit1 = 11,
                    .rc_tgt_offset_hi = 3,
                    .rc_tgt_offset_lo = 3,
                    },
	.data_rate = DATA_RATE, /* 943 */
	.pll_clk = PLL_CLOCK,
	.bdg_ssc_enable = 0,
	.ssc_enable = 0,
	//.data_rate_khz = 1030000, /* 943307 */

	.oplus_display_global_dre = 1,
};

static struct mtk_panel_params ext_params_90hz = {
    .vendor = "td4377_oris_c",
    .manufacture = "24700_csot_td4377",
	//.change_fps_by_vfp_send_cmd = 1,
	//.vfp_low_power = 148,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x1C,
	},
	.physical_width_um = 70000,
	.physical_height_um = 150000,
    .dsc_params = {
                    .enable = 0,
                    .bdg_dsc_enable = 1,
                    .ver = 17,
                    .slice_mode = 1,
                    .rgb_swap = 0,
                    .dsc_cfg = 34,
                    .rct_on = 1,
                    .bit_per_channel = 8,
                    .dsc_line_buf_depth = 9,
                    .bp_enable = 1,
                    .bit_per_pixel = 128,
                    .pic_height = 2400,
                    .pic_width = 1080,
                    .slice_height = 12,
                    .slice_width = 540,
                    .chunk_size = 540,
                    .xmit_delay = 512,
                    .dec_delay = 526,
                    .scale_value = 32,
                    .increment_interval = 287,
                    .decrement_interval = 7,
                    .line_bpg_offset = 12,
                    .nfl_bpg_offset = 2235,
                    .slice_bpg_offset = 2170,
                    .initial_offset = 6144,
                    .final_offset = 4336,
                    .flatness_minqp = 3,
                    .flatness_maxqp = 12,
                    .rc_model_size = 8192,
                    .rc_edge_factor = 6,
                    .rc_quant_incr_limit0 = 11,
                    .rc_quant_incr_limit1 = 11,
                    .rc_tgt_offset_hi = 3,
                    .rc_tgt_offset_lo = 3,
                    },
	.data_rate = DATA_RATE, /* 943 */
	.pll_clk = PLL_CLOCK,
	.bdg_ssc_enable = 0,
	.ssc_enable = 0,
	//.data_rate_khz = 1030000, /* 943307 */

	.oplus_display_global_dre = 1,
};

static void cabc_switch(void *dsi, dcs_write_gce cb,void *handle, unsigned int cabc_mode)
{
    char bl_tb1[] = {0x55, 0x03}; /* no cabc ui pictures videoes*/

    pr_err("%s cabc = %d\n", __func__, cabc_mode);

    if (cabc_mode == 1) {
        bl_tb1[1] = 0x01;
        cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));
    } else if (cabc_mode == 2) {
        bl_tb1[1] = 0x02;
        cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));
    } else if (cabc_mode == 3) {
        if(cabc_status == 0){
            bl_tb1[1] = 0x01;
            usleep_range(3000, 4000);
            cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));

            bl_tb1[1] = 0x02;
            usleep_range(3000, 4000);
            cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));

            bl_tb1[1] = 0x03;
            usleep_range(3000, 4000);
            cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));
        }else{
            bl_tb1[1] = 0x03;
            cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));
        }
    } else if (cabc_mode == 0) {
        if(cabc_status == 3){

            bl_tb1[1] = 0x02;
            usleep_range(3000, 4000);
            cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));

            bl_tb1[1] = 0x01;
            usleep_range(3000, 4000);
            cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));

            bl_tb1[1] = 0x00; /* cabc off */
            usleep_range(3000, 4000);
            cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));
        }else{
            bl_tb1[1] = 0x00; /* cabc off */
            cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));
        }
    }else {
        bl_tb1[1] = 0x03; /* default */
        cb(dsi, handle, bl_tb1, ARRAY_SIZE(bl_tb1));
    }
	cabc_status = cabc_mode;
    /* cabc_lastlevel = cabc_mode; */
}


static int panel_ata_check(struct drm_panel *panel)
{
	/* Customer test by own ATA tool */
	return 1;
}

static int map_exp[4096] = {0};

static void init_global_exp_backlight(void)
{
        int lut_index[41] = {0, 64, 144, 154, 187, 227, 264, 300, 334, 366, 397, 427, 456, 484, 511, 537, 563, 587, 611, 635, 658, 680, 702,
                                                 723, 744, 764, 784, 804, 823, 842, 861, 879, 897, 915, 933, 950, 967, 984, 1000, 1016, 1070};
        int lut_value1[41] = {0, 3,  11,  13,  21,  32,  45, 59,  76,  92, 111, 130,  150, 172, 194, 219, 244, 268, 293, 320, 347, 373, 402,
                                                430, 459, 487, 517, 549, 579, 611, 642, 675, 707, 740, 775, 808, 843, 878, 911, 947, 1070};
        int index_start = 0, index_end = 0;
        int value1_start = 0, value1_end = 0;
        int i,j;
        int index_len = sizeof(lut_index) / sizeof(int);
        int value_len = sizeof(lut_value1) / sizeof(int);
        if (index_len == value_len) {
                for (i = 0; i < index_len - 1; i++) {
                        index_start = lut_index[i] * oplus_max_normal_brightness / MULTIPLE_BRIGHTNESS;
                        index_end = lut_index[i+1] * oplus_max_normal_brightness / MULTIPLE_BRIGHTNESS;
                        value1_start = lut_value1[i] * oplus_max_normal_brightness / MULTIPLE_BRIGHTNESS;
                        value1_end = lut_value1[i+1] * oplus_max_normal_brightness / MULTIPLE_BRIGHTNESS;
                        for (j = index_start; j <= index_end; j++) {
                                map_exp[j] = value1_start + (value1_end - value1_start) * (j - index_start) / (index_end - index_start);
                        }
                }
        }
}


static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	unsigned int bl_level = level;
	/* bl_level = map_exp[level]; */
	unsigned int mode;

	mode = get_boot_mode();
	/*
	if (mode == KERNEL_POWER_OFF_CHARGING_BOOT && level > 0)
		level = 2047;
		if (silence_mode == 1)
			level = 0; */

	/*if ((level > 0) && (level < oplus_max_normal_brightness)) {
		bl_level = map_exp[level];
	}
	*/
	if (mode == KERNEL_POWER_OFF_CHARGING_BOOT && level > 0)
		bl_level = 2047;
	else
		bl_level = level;

	esd_last_level = level;

	//pr_info("%s bl_level: %d,%d mode = %d %d \n", __func__, bl_level,level,mode);

	//lcd cabc backlight
	if (bl_level > 4095)
		bl_level = 4095;
	bl_tb0[1] = (bl_level >> 8)& 0x0f;
	bl_tb0[2] = bl_level & 0xFF;
	esd_brightness = level;

	if (first_set_bl) {
		msleep(12);
		first_set_bl = 0;
	}

	pr_info("%s level = %d,backlight = %d,bl_tb0[1] = 0x%x,bl_tb0[2] = 0x%x\n",
		__func__, level, bl_level, bl_tb0[1], bl_tb0[2]);

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}

static int oplus_esd_backlight_recovery(void *dsi, dcs_write_gce cb,
		void *handle)
{
	char bl_tb0[] = {0x51, 0x0F, 0xFF};

	pr_err("%s esd_backlight = %d\n", __func__, esd_brightness);
	bl_tb0[1] =  (esd_brightness >> 8) & 0x0f;
	bl_tb0[2] = esd_brightness & 0xff;
	if (first_set_bl) {
		msleep(12);
		first_set_bl = 0;
	}
	if (!cb)
		return -1;


	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 1;
}

struct drm_display_mode *get_mode_by_id_hfp(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}

static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id_hfp(connector, mode);

	pr_info("drm_mode_vrefresh(m) =%d", drm_mode_vrefresh(m));

	if (ext && m && drm_mode_vrefresh(m) == 90){
		ext->params = &ext_params_90hz;
	}else if (ext && m && drm_mode_vrefresh(m) == 45){
		ext->params = &ext_params_45hz;
	} else if (ext && m && drm_mode_vrefresh(m) == 48){
		ext->params = &ext_params_48hz;
	} else if (ext && m && drm_mode_vrefresh(m) == 50){
		ext->params = &ext_params_50hz;
	} else if (ext && m && drm_mode_vrefresh(m) == 60){
		ext->params = &ext_params_60hz;
	}else{
		ret = 1;
		pr_err("[ %s : %d ] : No mode to set fps = %d \n", __func__ ,  __LINE__ , drm_mode_vrefresh(m));
	}
	return ret;
}

static int mtk_panel_ext_param_get(struct drm_panel *panel,
                struct drm_connector *connector,
                struct mtk_panel_params **ext_param, unsigned int id)
{
        int ret = 0;

	if (id == 0) {
		 *ext_param = &ext_params_90hz;
	}else if (id == 1){
		 *ext_param = &ext_params_45hz;
	}else if (id == 2){
		 *ext_param = &ext_params_48hz;
	}else if (id == 3){
		 *ext_param = &ext_params_50hz;
	}else if (id == 4){
		 *ext_param = &ext_params_60hz;
	}else{
			ret = 1;
	}
        return ret;
}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.esd_backlight_recovery = oplus_esd_backlight_recovery,
	.ext_param_set = mtk_panel_ext_param_set,
	.ext_param_get = mtk_panel_ext_param_get,
	//.mode_switch = mode_switch,
	.ata_check = panel_ata_check,
	.cabc_switch = cabc_switch,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	/**
	 * @prepare: the time (in milliseconds) that it takes for the panel to
	 *	   become ready and start receiving video data
	 * @enable: the time (in milliseconds) that it takes for the panel to
	 *	  display the first valid frame after starting to receive
	 *	  video data
	 * @disable: the time (in milliseconds) that it takes for the panel to
	 *	   turn the display off (no content is visible)
	 * @unprepare: the time (in milliseconds) that it takes for the panel
	 *		 to power itself down completely
	 */
	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int lcm_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode2;
	struct drm_display_mode *mode3;
	struct drm_display_mode *mode4;
	struct drm_display_mode *mode5;

	mode = drm_mode_duplicate(connector->dev, &default_mode_90hz);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 default_mode_90hz.hdisplay, default_mode_90hz.vdisplay,
			 drm_mode_vrefresh(&default_mode_90hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	printk("lcm_pack_modes:mode->name[%s] mode->type[%u] htotal=%u vtotal =%u\n",
                mode->name, mode->type, mode->htotal, mode->vtotal);
	drm_mode_probed_add(connector, mode);

	mode2 = drm_mode_duplicate(connector->dev, &performance_mode_45hz);
	if (!mode2) {
		dev_info(connector->dev->dev, "failed to add mode2 %ux%ux@%u\n",
			 performance_mode_45hz.hdisplay, performance_mode_45hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_45hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	printk("lcm_pack_modes:mode2->name[%s] mode2->type[%u] htotal=%u vtotal =%u\n",
                mode2->name, mode2->type, mode2->htotal, mode2->vtotal);
	drm_mode_probed_add(connector, mode2);

	mode3 = drm_mode_duplicate(connector->dev, &performance_mode_48hz);
	if (!mode3) {
		dev_info(connector->dev->dev, "failed to add mode3 %ux%ux@%u\n",
			 performance_mode_48hz.hdisplay, performance_mode_48hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_48hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode3);
	mode3->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	printk("lcm_pack_modes:mode3->name[%s] mode3->type[%u] htotal=%u vtotal =%u\n",
                mode3->name, mode3->type, mode3->htotal, mode3->vtotal);
	drm_mode_probed_add(connector, mode3);

	mode4 = drm_mode_duplicate(connector->dev, &performance_mode_50hz);
	if (!mode4) {
		dev_info(connector->dev->dev, "failed to add mode4 %ux%ux@%u\n",
			performance_mode_50hz.hdisplay, performance_mode_50hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_50hz));
		return -ENOMEM;
	}
	
	drm_mode_set_name(mode4);
	mode4->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	printk("lcm_pack_modes:mode4->name[%s] mode4->type[%u] htotal=%u vtotal =%u\n",
                mode4->name, mode4->type, mode4->htotal, mode4->vtotal);
	drm_mode_probed_add(connector, mode4);

	mode5 = drm_mode_duplicate(connector->dev, &performance_mode_60hz);
	if (!mode5) {
		dev_info(connector->dev->dev, "failed to add mode5 %ux%ux@%u\n",
			 performance_mode_60hz.hdisplay, performance_mode_60hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_60hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode5);
	mode5->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	printk("lcm_pack_modes:mode5->name[%s] mode5->type[%u] htotal=%u vtotal =%u\n",
                mode5->name, mode5->type, mode5->htotal, mode5->vtotal);
	drm_mode_probed_add(connector, mode5);

	connector->display_info.width_mm = 70;
	connector->display_info.height_mm = 156;

	printk("lcm_pack_modes: end--width_mm:%d height_mm =%d\n",
                connector->display_info.width_mm, connector->display_info.height_mm);

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static void check_is_bdg_support(struct device *dev)
{
       unsigned int ret = 0;
       unsigned int bdg_support = 0;

       ret = of_property_read_u32(dev->of_node, "bdg-support", &bdg_support);
       if (!ret && bdg_support == 1) {
               pr_info("%s, bdg support 1", __func__);
       } else {
               pr_info("%s, bdg support 0", __func__);
               bdg_support = 0;
       }
}

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct lcm *ctx;
	struct device_node *backlight;
	unsigned int value;
	int ret;

	pr_info("%s+ lcm ,td4377_csot\n", __func__);

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	ret = of_property_read_u32(dev->of_node, "gate-ic", &value);
	if (ret < 0)
		value = 0;
	else
		ctx->gate_ic = value;

	//pr_info(" %d  %s,ctx->ctx->gate_ic = %d \n", __LINE__, __func__,ctx->gate_ic);

	check_is_bdg_support(dev);
	value = 0;
	ret = of_property_read_u32(dev->of_node, "rc-enable", &value);
	if (ret < 0)
		value = 0;
	else {
		ext_params_90hz.round_corner_en = value;
	}

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	//pr_info(" %d  %s,ctx->reset_gpio = %d \n", __LINE__, __func__,ctx->reset_gpio);

	devm_gpiod_put(dev, ctx->reset_gpio);
	ctx->bias_pos = devm_gpiod_get_index(dev, "bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_info(dev, "cannot get bias-gpios 0 %ld\n",
			 PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	//pr_info(" %d  %s,ctx->bias_pos = %x \n", __LINE__, __func__,ctx->bias_pos);
	devm_gpiod_put(dev, ctx->bias_pos);

	ctx->bias_neg = devm_gpiod_get_index(dev, "bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_info(dev, "cannot get bias-gpios 1 %ld\n",
			 PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	//pr_info(" %d  %s,ctx->bias_neg = %x \n", __LINE__, __func__,ctx->bias_neg);
	devm_gpiod_put(dev, ctx->bias_neg);


	ctx->prepared = true;
	ctx->enabled = true;
	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params_90hz, &ext_funcs, &ctx->panel);
	pr_info(" %d  %s\n", __LINE__, __func__);
	if (ret < 0)
		return ret;

#endif

	oplus_max_normal_brightness = MAX_NORMAL_BRIGHTNESS;
	init_global_exp_backlight();

	/* wanhang */
	register_device_proc("lcd", "td4377", "csot");
	oplus_ofp_init(dev->of_node);
	pr_info("%s- lcm,td4377_csot,vdo,90hz\n", __func__);

	return ret;
}

static void lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif

}

static const struct of_device_id lcm_of_match[] = {
	{
	    .compatible = "oplus24700_td4377_csot_fhdp_dsi_vdo",
	},
	{}
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "oplus24700_td4377_csot_fhdp_dsi_vdo",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("zhangxian <xian.zhang@tinno.com>");
MODULE_DESCRIPTION("ICETRON lcm HX83102J VDO 120HZ LCD Panel Driver");
MODULE_LICENSE("GPL v2");


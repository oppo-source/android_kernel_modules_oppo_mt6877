// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 MediaTek Inc.

/*****************************************************************************
 *
 * Filename:
 * ---------
 *	 himalayanmainmipiraw_Sensor.c
 *
 * Project:
 * --------
 *	 ALPS
 *
 * Description:
 * ------------
 *	 Source code of Sensor driver
 *
 *
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by CC/CQ. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/
#include "himalayanmainmipiraw_Sensor.h"

#define PFX "himalayanmain_camera_sensor"
#define LOG_INF(format, args...) pr_err(PFX "[%s] " format, __func__, ##args)
#define OTP_SIZE    0x8000
#define MODULEINFO_SIZE	17
#define AFINFO_SIZE		9
#define QRCODE_SIZE		24
#define HIMALAYANMAIN_EEPROM_READ_ID       0xA1
#define HIMALAYANMAIN_EEPROM_WRITE_ID      0xA0
#define OPLUS_CAMERA_COMMON_DATA_LENGTH     40
#define HIMALAYANMAIN_SENSOR_GAIN_BASE                1024
#define HIMALAYANMAIN_SENSOR_GAIN_MAX                 32768 /* (32 * HIMALAYANMAIN_SENSOR_GAIN_BASE) */
#define HIMALAYANMAIN_SENSOR_GAIN_MAX_VALID_INDEX     6

// static kal_uint8 otp_data_checksum[OTP_SIZE] = {0};
static u16 get_gain2reg(u32 gain);
static void himalayanmain_sensor_init(struct subdrv_ctx *ctx);
static int  himalayanmain_set_gain(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int himalayanmain_set_test_pattern(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int  himalayanmain_set_shutter(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int himalayanmain_check_sensor_id(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int get_eeprom_common_data(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int himalayanmain_get_otp_checksum_data(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int get_imgsensor_id(struct subdrv_ctx *ctx, u32 *sensor_id);
static int open(struct subdrv_ctx *ctx);
static int init_ctx(struct subdrv_ctx *ctx,	struct i2c_client *i2c_client, u8 i2c_write_id);
static int vsync_notify(struct subdrv_ctx *ctx,	unsigned int sof_cnt);
static int himalayanmain_streaming_suspend(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int himalayanmain_streaming_resume(struct subdrv_ctx *ctx, u8 *para, u32 *len);

/* STRUCT */

static struct subdrv_feature_control feature_control_list[] = {
	{SENSOR_FEATURE_SET_TEST_PATTERN, himalayanmain_set_test_pattern},
	{SENSOR_FEATURE_SET_ESHUTTER, himalayanmain_set_shutter},
	{SENSOR_FEATURE_SET_GAIN, himalayanmain_set_gain},
	{SENSOR_FEATURE_CHECK_SENSOR_ID, himalayanmain_check_sensor_id},
	{SENSOR_FEATURE_GET_EEPROM_COMDATA, get_eeprom_common_data},
	{SENSOR_FEATURE_GET_SENSOR_OTP_ALL, himalayanmain_get_otp_checksum_data},
	{SENSOR_FEATURE_SET_STREAMING_SUSPEND, himalayanmain_streaming_suspend},
	{SENSOR_FEATURE_SET_STREAMING_RESUME, himalayanmain_streaming_resume},
};

static struct mtk_mbus_frame_desc_entry frame_desc_prev[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 3264,
			.vsize = 2448,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cap[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 3264,
			.vsize = 2448,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_vid[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 3264,
			.vsize = 1836,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_hs_vid[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 3264,
			.vsize = 1836,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_slim_vid[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 3264,
			.vsize = 1836,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cust1[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 3264,
			.vsize = 2448,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};
static struct mtk_mbus_frame_desc_entry frame_desc_cust2[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 3264,
			.vsize = 2448,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};
static struct mtk_mbus_frame_desc_entry frame_desc_cust3[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 3264,
			.vsize = 2448,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};

static struct subdrv_mode_struct mode_struct[] = {
	{
		.frame_desc = frame_desc_prev,
		.num_entries = ARRAY_SIZE(frame_desc_prev),
		.mode_setting_table = himalayanmain_preview_setting,
		.mode_setting_len = ARRAY_SIZE(himalayanmain_preview_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.pclk = 132000000,
		.linelength = 1760,
		.framelength = 2500,
		.max_framerate = 300,
		.mipi_pixel_rate = 264000000,
		.readout_length = 0,
		.read_margin = 4,
		.framelength_step = 2,
		.coarse_integ_step = 1,
		.min_exposure_line = 2,
		.imgsensor_winsize_info = {
			.full_w = 3264,
			.full_h = 2448,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 3264,
			.h0_size = 2448,
			.scale_w = 3264,
			.scale_h = 2448,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 3264,
			.h1_size = 2448,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 3264,
			.h2_tg_size = 2448,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1,//cc
		.fine_integ_line = 0,
		.delay_frame = 3,
		.csi_param = {
			.dphy_trail = 0x47,
		},

	},
	{
		.frame_desc = frame_desc_cap,
		.num_entries = ARRAY_SIZE(frame_desc_cap),
		.mode_setting_table = himalayanmain_capture_setting,
		.mode_setting_len = ARRAY_SIZE(himalayanmain_capture_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.pclk = 132000000,
		.linelength = 1760,
		.framelength = 2500,
		.max_framerate = 300,
		.mipi_pixel_rate = 264000000,
		.readout_length = 0,
		.read_margin = 4,
		.framelength_step = 2,
		.coarse_integ_step = 1,
		.min_exposure_line = 2,
		.imgsensor_winsize_info = {
			.full_w = 3264,
			.full_h = 2448,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 3264,
			.h0_size = 2448,
			.scale_w = 3264,
			.scale_h = 2448,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 3264,
			.h1_size = 2448,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 3264,
			.h2_tg_size = 2448,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1,//cc
		.fine_integ_line = 0,
		.delay_frame = 3,
		.csi_param = {
			.dphy_trail = 0x47,
		},

	},
	{
		.frame_desc = frame_desc_vid,
		.num_entries = ARRAY_SIZE(frame_desc_vid),
		.mode_setting_table = himalayanmain_normal_video_setting,
		.mode_setting_len = ARRAY_SIZE(himalayanmain_normal_video_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.pclk = 132000000,
		.linelength = 1760,
		.framelength = 2500,
		.max_framerate = 300,
		.mipi_pixel_rate = 264000000,
		.readout_length = 0,
		.read_margin = 4,
		.framelength_step = 2,
		.coarse_integ_step = 1,
		.min_exposure_line = 2,
		.imgsensor_winsize_info = {
			.full_w = 3264,
			.full_h = 2448,
			.x0_offset = 0,
			.y0_offset = 306,
			.w0_size = 3264,
			.h0_size = 1836,
			.scale_w = 3264,
			.scale_h = 1836,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 3264,
			.h1_size = 1836,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 3264,
			.h2_tg_size = 1836,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1,
		.fine_integ_line = 0,
		.delay_frame = 3,
		.csi_param = {
			.dphy_trail = 0x47,
		},

	},
	{
		.frame_desc = frame_desc_hs_vid,
		.num_entries = ARRAY_SIZE(frame_desc_hs_vid),
		.mode_setting_table = himalayanmain_hs_video_setting,
		.mode_setting_len = ARRAY_SIZE(himalayanmain_hs_video_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.pclk = 132000000,
		.linelength = 1760,
		.framelength = 2500,
		.max_framerate = 300,
		.mipi_pixel_rate = 264000000,
		.readout_length = 0,
		.read_margin = 4,
		.framelength_step = 2,
		.coarse_integ_step = 1,
		.min_exposure_line = 2,
		.imgsensor_winsize_info = {
			.full_w = 3264,
			.full_h = 2448,
			.x0_offset = 0,
			.y0_offset = 306,
			.w0_size = 3264,
			.h0_size = 1836,
			.scale_w = 3264,
			.scale_h = 1836,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 3264,
			.h1_size = 1836,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 3264,
			.h2_tg_size = 1836,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1,
		.fine_integ_line = 0,
		.delay_frame = 3,
		.csi_param = {
			.dphy_trail = 0x47,
		},

	},
	{
		.frame_desc = frame_desc_slim_vid,
		.num_entries = ARRAY_SIZE(frame_desc_slim_vid),
		.mode_setting_table = himalayanmain_slim_video_setting,
		.mode_setting_len = ARRAY_SIZE(himalayanmain_slim_video_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.pclk = 132000000,
		.linelength = 1760,
		.framelength = 2500,
		.max_framerate = 300,
		.mipi_pixel_rate = 264000000,
		.readout_length = 0,
		.read_margin = 4,
		.framelength_step = 2,
		.coarse_integ_step = 1,
		.min_exposure_line = 2,
		.imgsensor_winsize_info = {
			.full_w = 3264,
			.full_h = 2448,
			.x0_offset = 0,
			.y0_offset = 306,
			.w0_size = 3264,
			.h0_size = 1836,
			.scale_w = 3264,
			.scale_h = 1836,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 3264,
			.h1_size = 1836,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 3264,
			.h2_tg_size = 1836,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1,
		.fine_integ_line = 0,
		.delay_frame = 3,
		.csi_param = {
			.dphy_trail = 0x47,
		},

	},
	{
		.frame_desc = frame_desc_cust1,
		.num_entries = ARRAY_SIZE(frame_desc_cust1),
		.mode_setting_table = himalayanmain_custom1_setting,
		.mode_setting_len = ARRAY_SIZE(himalayanmain_custom1_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.pclk = 132000000,
		.linelength = 1760,
		.framelength = 2500,
		.max_framerate = 300,
		.mipi_pixel_rate = 264000000,
		.readout_length = 0,
		.read_margin = 4,
		.framelength_step = 2,
		.coarse_integ_step = 1,
		.min_exposure_line = 2,
		.imgsensor_winsize_info = {
			.full_w = 3264,
			.full_h = 2448,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 3264,
			.h0_size = 2448,
			.scale_w = 3264,
			.scale_h = 2448,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 3264,
			.h1_size = 2448,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 3264,
			.h2_tg_size = 2448,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1,
		.fine_integ_line = 0,
		.delay_frame = 3,
		.csi_param = {
			.dphy_trail = 0x47,
		},

	},
	{
		.frame_desc = frame_desc_cust2,
		.num_entries = ARRAY_SIZE(frame_desc_cust2),
		.mode_setting_table = himalayanmain_custom2_setting,
		.mode_setting_len = ARRAY_SIZE(himalayanmain_custom2_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.pclk = 132000000,
		.linelength = 1760,
		.framelength = 2500,
		.max_framerate = 300,
		.mipi_pixel_rate = 264000000,
		.readout_length = 0,
		.read_margin = 4,
		.framelength_step = 2,
		.coarse_integ_step = 1,
		.min_exposure_line = 2,
		.imgsensor_winsize_info = {
			.full_w = 3264,
			.full_h = 2448,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 3264,
			.h0_size = 2448,
			.scale_w = 3264,
			.scale_h = 2448,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 3264,
			.h1_size = 2448,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 3264,
			.h2_tg_size = 2448,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1,
		.fine_integ_line = 0,
		.delay_frame = 3,
		.csi_param = {
			.dphy_trail = 0x47,
		},

	},
	{
		.frame_desc = frame_desc_cust3,
		.num_entries = ARRAY_SIZE(frame_desc_cust3),
		.mode_setting_table = himalayanmain_custom3_setting,
		.mode_setting_len = ARRAY_SIZE(himalayanmain_custom3_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.pclk = 132000000,
		.linelength = 1760,
		.framelength = 2500,
		.max_framerate = 300,
		.mipi_pixel_rate = 264000000,
		.readout_length = 0,
		.read_margin = 4,
		.framelength_step = 2,
		.coarse_integ_step = 1,
		.min_exposure_line = 2,
		.imgsensor_winsize_info = {
			.full_w = 3264,
			.full_h = 2448,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 3264,
			.h0_size = 2448,
			.scale_w = 3264,
			.scale_h = 2448,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 3264,
			.h1_size = 2448,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 3264,
			.h2_tg_size = 2448,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1,
		.fine_integ_line = 0,
		.delay_frame = 3,
		.csi_param = {
			.dphy_trail = 0x47,
		},

	},
};

static struct subdrv_static_ctx static_ctx = {
	.sensor_id = HIMALAYANMAIN_SENSOR_ID,
	.reg_addr_sensor_id = {0x3107, 0x3108},
	.i2c_addr_table = {0x20, 0xFF}, // TBD
	.i2c_burst_write_support = FALSE,
	.i2c_transfer_data_type = I2C_DT_ADDR_16_DATA_8,
	.eeprom_info = 0,
	.eeprom_num = 0,
	.resolution = {3264, 2448},
	.mirror = IMAGE_HV_MIRROR, // TBD

	.mclk = 24,
	.isp_driving_current = ISP_DRIVING_4MA,
	.sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,
	.mipi_sensor_type = MIPI_OPHY_NCSI2,
	.mipi_lane_num = SENSOR_MIPI_4_LANE,
	.ob_pedestal = 0x41,

	.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_B,
	.ana_gain_def = BASEGAIN * 1,
	.ana_gain_min = BASEGAIN * 1,
	.ana_gain_max = BASEGAIN * 32,
	.ana_gain_type = 5, //Sony:type 0; OV:type 1; Samsung:type 2; Hinyx:type 3; GC:type 4
	.ana_gain_step = 32,
	.ana_gain_table = himalayanmain_ana_gain_table,
	.ana_gain_table_size = sizeof(himalayanmain_ana_gain_table),
	.tuning_iso_base = 100,
	.exposure_def = 0x3D0,
	.exposure_min = 4,
	.exposure_max = 0xFFFF - 8,
	.exposure_step = 1,
	.exposure_margin = 8,

	.frame_length_max = 0x7FF0,
	.ae_effective_frame = 3,
	.frame_time_delay_frame = 2,
	.start_exposure_offset = 500000,

	.pdaf_type = PDAF_SUPPORT_NA,
	.hdr_type = HDR_SUPPORT_NA,
	.seamless_switch_support = FALSE,
	.temperature_support = FALSE,

	.g_temp = PARAM_UNDEFINED,
	.g_gain2reg = get_gain2reg,
	.s_gph = PARAM_UNDEFINED,

	.reg_addr_stream = 0x0100,
	.reg_addr_mirror_flip = PARAM_UNDEFINED, // TBD
	.reg_addr_exposure = {
		{0x3e00, 0x3e01, 0x3e02}
	},
	.long_exposure_support = FALSE,
	.reg_addr_exposure_lshift = PARAM_UNDEFINED,
	.reg_addr_ana_gain = {
		{0x3e08,0x3e07}
	},
	.reg_addr_frame_length = {0x320e, 0x320f},
	.reg_addr_temp_en = PARAM_UNDEFINED,
	.reg_addr_temp_read = PARAM_UNDEFINED,
	.reg_addr_auto_extend = PARAM_UNDEFINED,
	.reg_addr_frame_count = PARAM_UNDEFINED,

	.init_setting_table = himalayanmain_init_setting,
	.init_setting_len = ARRAY_SIZE(himalayanmain_init_setting),
	.mode = mode_struct,
	.sensor_mode_num = ARRAY_SIZE(mode_struct),
	.list = feature_control_list,
	.list_len = ARRAY_SIZE(feature_control_list),
	.chk_s_off_sta = 0,
	.chk_s_off_end = 0,
	.checksum_value = 0xf10e5980,
};

static struct subdrv_ops ops = {
	.get_id = get_imgsensor_id,
	.init_ctx = init_ctx,
	.open = open,
	.get_info = common_get_info,
	.get_resolution = common_get_resolution,
	.control = common_control,
	.feature_control = common_feature_control,
	.close = common_close,
	.get_frame_desc = common_get_frame_desc,
	.get_temp = common_get_temp,
	.get_csi_param = common_get_csi_param,
	.vsync_notify = vsync_notify,
	.update_sof_cnt = common_update_sof_cnt,
};

static struct subdrv_pw_seq_entry pw_seq[] = {
	{HW_ID_RST,    {0},		1000},
	{HW_ID_MCLK,   {24},		1000},
	{HW_ID_MCLK_DRIVING_CURRENT, {4}, 1000},
	{HW_ID_DOVDD,  {1800000, 1800000}, 5000},
	{HW_ID_DVDD,   {1200000, 1200000}, 9000},
	{HW_ID_AVDD,   {2800000, 2800000}, 5000},
	{HW_ID_RST,    {1},		10000},
};

const struct subdrv_entry himalayanmain_mipi_raw_entry = {
	.name = "himalayanmain_mipi_raw",
	.id = HIMALAYANMAIN_SENSOR_ID,
	.pw_seq = pw_seq,
	.pw_seq_cnt = ARRAY_SIZE(pw_seq),
	.ops = &ops,
};

/* static struct eeprom_addr_table_struct oplus_eeprom_addr_table = {
	.i2c_read_id = 0xA1,
	.i2c_write_id = 0xA0,

	.addr_modinfo = 0x0000,
	.addr_sensorid = 0x0006,
	.addr_lens = 0x0008,
	.addr_vcm = 0x000A,
	.addr_modinfoflag = 0x0010,

	.addr_af = 0x002A,
	.addr_afmacro = 0x002C,
	.addr_afinf = 0x002E,
	.addr_afflag = 0x0032,

	.addr_qrcode = 0x0034,
	.addr_qrcodeflag = 0x004B,
}; */

static struct oplus_eeprom_info_struct oplus_eeprom_info = {0};

static void himalayanmain_read_eeprom_data(struct subdrv_ctx *ctx)
{
	oplus_eeprom_info.sensorid_offset = 0x06;
	oplus_eeprom_info.lens_offset = 0x08;
	oplus_eeprom_info.vcm_offset = 0x0A;
	oplus_eeprom_info.macPos_offset = 0x02;
	oplus_eeprom_info.infPos_offset = 0x04;

	oplus_eeprom_info.moduleInfo_size = 17;
	oplus_eeprom_info.af_size = 9;
	oplus_eeprom_info.qrcode_size = 24;

	memcpy(&oplus_eeprom_info.moduleInfo[0], &himalayanmain_txd_main_otp.module_info[0], MODULEINFO_SIZE);
	memcpy(&oplus_eeprom_info.afInfo[0], &himalayanmain_txd_main_otp.af_data[0], AFINFO_SIZE);
	memcpy(&oplus_eeprom_info.qrcodeInfo[0], &himalayanmain_txd_main_otp.sn_data[0], QRCODE_SIZE);
}

static int get_eeprom_common_data(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	if (himalayanmain_txd_main_otp.ModuleFlag == 0) {
		himalayanmain_sensor_otp_read_all_data(ctx->i2c_client);
	}
	LOG_INF("ModuleFlag = 0x%02x\n", himalayanmain_txd_main_otp.ModuleFlag);

	himalayanmain_read_eeprom_data(ctx);
	memcpy(para, (u8*)(&oplus_eeprom_info), sizeof(oplus_eeprom_info));
	*len = sizeof(oplus_eeprom_info);
	LOG_INF("get_eeprom_common_data");
	return 0;
}

static int himalayanmain_get_otp_checksum_data(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	LOG_INF("get otp data");
	memcpy(para, &himalayanmain_txd_main_otp, sizeof(himalayanmain_txd_main_otp));
	*len = sizeof(himalayanmain_txd_main_otp);
	return 0;
}

static int himalayanmain_check_sensor_id(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	get_imgsensor_id(ctx, (u32 *)para);
	return 0;
}

static int get_imgsensor_id(struct subdrv_ctx *ctx, u32 *sensor_id)
{
	u8 i = 0;
	u8 retry = 2;
	u32 addr_h = ctx->s_ctx.reg_addr_sensor_id.addr[0];
	u32 addr_l = ctx->s_ctx.reg_addr_sensor_id.addr[1];
	u32 addr_ll = ctx->s_ctx.reg_addr_sensor_id.addr[2];
	LOG_INF("get_imgsensor_id enter");

	while (ctx->s_ctx.i2c_addr_table[i] != 0xFF) {
		ctx->i2c_write_id = ctx->s_ctx.i2c_addr_table[i];
		do {
			*sensor_id = (subdrv_i2c_rd_u8(ctx, addr_h) << 8) | subdrv_i2c_rd_u8(ctx, addr_l);
			if (addr_ll)
				*sensor_id = ((*sensor_id) << 8) | subdrv_i2c_rd_u8(ctx, addr_ll);
			LOG_INF("i2c_write_id(0x%x) sensor_id(0x%x/0x%x)\n", ctx->i2c_write_id, *sensor_id, ctx->s_ctx.sensor_id);
			if (*sensor_id == 0xd154) {
				*sensor_id = ctx->s_ctx.sensor_id;
				return ERROR_NONE;
			}
			LOG_INF("Read sensor id fail. i2c_write_id: 0x%x\n", ctx->i2c_write_id);
			LOG_INF("sensor_id = 0x%x, ctx->s_ctx.sensor_id = 0x%x\n",
				*sensor_id, ctx->s_ctx.sensor_id);
			retry--;
		} while (retry > 0);
		i++;
		retry = 2;
	}
	if (*sensor_id != ctx->s_ctx.sensor_id) {
		*sensor_id = 0xFFFFFFFF;
		return ERROR_SENSOR_CONNECT_FAIL;
	}
	return ERROR_NONE;
}

static void himalayanmain_sensor_init(struct subdrv_ctx *ctx)
{
	subdrv_i2c_wr_u8(ctx, 0x0103,0x01);
	subdrv_i2c_wr_u8(ctx, 0x36e9,0x80);
	subdrv_i2c_wr_u8(ctx, 0x37f9,0x80);
	subdrv_i2c_wr_u8(ctx, 0x36e9,0x24);
	subdrv_i2c_wr_u8(ctx, 0x37f9,0x24);
	subdrv_i2c_wr_u8(ctx, 0x301f,0x0e);
	subdrv_i2c_wr_u8(ctx, 0x3205,0xc7);
	subdrv_i2c_wr_u8(ctx, 0x3211,0x04);
	subdrv_i2c_wr_u8(ctx, 0x3270,0x00);
	subdrv_i2c_wr_u8(ctx, 0x3271,0x00);
	subdrv_i2c_wr_u8(ctx, 0x3272,0x00);
	subdrv_i2c_wr_u8(ctx, 0x3273,0x03);
	subdrv_i2c_wr_u8(ctx, 0x3301,0x08);
	subdrv_i2c_wr_u8(ctx, 0x3303,0x10);
	subdrv_i2c_wr_u8(ctx, 0x3306,0x84);
	subdrv_i2c_wr_u8(ctx, 0x3307,0x04);
	subdrv_i2c_wr_u8(ctx, 0x3309,0x88);
	subdrv_i2c_wr_u8(ctx, 0x330a,0x01);
	subdrv_i2c_wr_u8(ctx, 0x330b,0x0c);
	subdrv_i2c_wr_u8(ctx, 0x330c,0x10);
	subdrv_i2c_wr_u8(ctx, 0x330d,0x18);
	subdrv_i2c_wr_u8(ctx, 0x330e,0x30);
	subdrv_i2c_wr_u8(ctx, 0x330f,0x04);
	subdrv_i2c_wr_u8(ctx, 0x3310,0x02);
	subdrv_i2c_wr_u8(ctx, 0x3314,0x15);
	subdrv_i2c_wr_u8(ctx, 0x3317,0x04);
	subdrv_i2c_wr_u8(ctx, 0x331f,0x79);
	subdrv_i2c_wr_u8(ctx, 0x3326,0x0e);
	subdrv_i2c_wr_u8(ctx, 0x3327,0x0a);
	subdrv_i2c_wr_u8(ctx, 0x3329,0x0b);
	subdrv_i2c_wr_u8(ctx, 0x3333,0x10);
	subdrv_i2c_wr_u8(ctx, 0x3334,0x40);
	subdrv_i2c_wr_u8(ctx, 0x3347,0x0f);
	subdrv_i2c_wr_u8(ctx, 0x335d,0x60);
	subdrv_i2c_wr_u8(ctx, 0x3364,0x56);
	subdrv_i2c_wr_u8(ctx, 0x336c,0xce);
	subdrv_i2c_wr_u8(ctx, 0x3390,0x08);
	subdrv_i2c_wr_u8(ctx, 0x3391,0x09);
	subdrv_i2c_wr_u8(ctx, 0x3392,0x0f);
	subdrv_i2c_wr_u8(ctx, 0x3393,0x10);
	subdrv_i2c_wr_u8(ctx, 0x3394,0x20);
	subdrv_i2c_wr_u8(ctx, 0x3395,0x28);
	subdrv_i2c_wr_u8(ctx, 0x33ad,0x3c);
	subdrv_i2c_wr_u8(ctx, 0x33af,0x70);
	subdrv_i2c_wr_u8(ctx, 0x33b2,0x70);
	subdrv_i2c_wr_u8(ctx, 0x33b3,0x40);
	subdrv_i2c_wr_u8(ctx, 0x349f,0x1e);
	subdrv_i2c_wr_u8(ctx, 0x34a6,0x09);
	subdrv_i2c_wr_u8(ctx, 0x34a7,0x0f);
	subdrv_i2c_wr_u8(ctx, 0x34a8,0x30);
	subdrv_i2c_wr_u8(ctx, 0x34a9,0x20);
	subdrv_i2c_wr_u8(ctx, 0x34f8,0x1f);
	subdrv_i2c_wr_u8(ctx, 0x34f9,0x08);
	subdrv_i2c_wr_u8(ctx, 0x3637,0x43);
	subdrv_i2c_wr_u8(ctx, 0x363c,0x8d);
	subdrv_i2c_wr_u8(ctx, 0x3670,0x4a);
	subdrv_i2c_wr_u8(ctx, 0x3674,0xf6);
	subdrv_i2c_wr_u8(ctx, 0x3675,0xdc);
	subdrv_i2c_wr_u8(ctx, 0x3676,0xcc);
	subdrv_i2c_wr_u8(ctx, 0x367c,0x09);
	subdrv_i2c_wr_u8(ctx, 0x367d,0x0f);
	subdrv_i2c_wr_u8(ctx, 0x3690,0x34);
	subdrv_i2c_wr_u8(ctx, 0x3691,0x44);
	subdrv_i2c_wr_u8(ctx, 0x3692,0x55);
	subdrv_i2c_wr_u8(ctx, 0x3698,0x86);
	subdrv_i2c_wr_u8(ctx, 0x3699,0x8d);
	subdrv_i2c_wr_u8(ctx, 0x369a,0x99);
	subdrv_i2c_wr_u8(ctx, 0x369b,0xb0);
	subdrv_i2c_wr_u8(ctx, 0x369c,0x0f);
	subdrv_i2c_wr_u8(ctx, 0x369d,0x1f);
	subdrv_i2c_wr_u8(ctx, 0x36a2,0x09);
	subdrv_i2c_wr_u8(ctx, 0x36a3,0x0b);
	subdrv_i2c_wr_u8(ctx, 0x36a4,0x0f);
	subdrv_i2c_wr_u8(ctx, 0x36b0,0x48);
	subdrv_i2c_wr_u8(ctx, 0x36b1,0x38);
	subdrv_i2c_wr_u8(ctx, 0x36b2,0x41);
	subdrv_i2c_wr_u8(ctx, 0x370f,0x01);
	subdrv_i2c_wr_u8(ctx, 0x3724,0xc1);
	subdrv_i2c_wr_u8(ctx, 0x3771,0x07);
	subdrv_i2c_wr_u8(ctx, 0x3772,0x03);
	subdrv_i2c_wr_u8(ctx, 0x3773,0x63);
	subdrv_i2c_wr_u8(ctx, 0x377a,0x08);
	subdrv_i2c_wr_u8(ctx, 0x377b,0x0f);
	subdrv_i2c_wr_u8(ctx, 0x3901,0x04);
	subdrv_i2c_wr_u8(ctx, 0x3903,0xa0);
	subdrv_i2c_wr_u8(ctx, 0x3905,0x8d);
	subdrv_i2c_wr_u8(ctx, 0x391d,0x01);
	subdrv_i2c_wr_u8(ctx, 0x3926,0x23);
	subdrv_i2c_wr_u8(ctx, 0x393f,0x80);
	subdrv_i2c_wr_u8(ctx, 0x3940,0x00);
	subdrv_i2c_wr_u8(ctx, 0x3941,0x00);
	subdrv_i2c_wr_u8(ctx, 0x3942,0x00);
	subdrv_i2c_wr_u8(ctx, 0x3943,0x63);
	subdrv_i2c_wr_u8(ctx, 0x3944,0x5f);
	subdrv_i2c_wr_u8(ctx, 0x3c04,0x01);
	subdrv_i2c_wr_u8(ctx, 0x3e00,0x01);
	subdrv_i2c_wr_u8(ctx, 0x3e01,0x38);
	subdrv_i2c_wr_u8(ctx, 0x3e02,0x00);
	subdrv_i2c_wr_u8(ctx, 0x4401,0x13);
	subdrv_i2c_wr_u8(ctx, 0x4402,0x03);
	subdrv_i2c_wr_u8(ctx, 0x4403,0x0e);
	subdrv_i2c_wr_u8(ctx, 0x4404,0x28);
	subdrv_i2c_wr_u8(ctx, 0x4405,0x34);
	subdrv_i2c_wr_u8(ctx, 0x4407,0x0e);
	subdrv_i2c_wr_u8(ctx, 0x440c,0x42);
	subdrv_i2c_wr_u8(ctx, 0x440d,0x42);
	subdrv_i2c_wr_u8(ctx, 0x440e,0x32);
	subdrv_i2c_wr_u8(ctx, 0x440f,0x53);
	subdrv_i2c_wr_u8(ctx, 0x4412,0x01);
	subdrv_i2c_wr_u8(ctx, 0x4424,0x01);
	subdrv_i2c_wr_u8(ctx, 0x442d,0x00);
	subdrv_i2c_wr_u8(ctx, 0x442e,0x00);
	subdrv_i2c_wr_u8(ctx, 0x4509,0x28);
	subdrv_i2c_wr_u8(ctx, 0x450d,0x18);
	subdrv_i2c_wr_u8(ctx, 0x451d,0xc8);
	subdrv_i2c_wr_u8(ctx, 0x4526,0x09);
	subdrv_i2c_wr_u8(ctx, 0x5000,0x0e);
	subdrv_i2c_wr_u8(ctx, 0x550e,0x00);
	subdrv_i2c_wr_u8(ctx, 0x550f,0xBC);
	subdrv_i2c_wr_u8(ctx, 0x5780,0x66);
	subdrv_i2c_wr_u8(ctx, 0x578d,0x40);
	subdrv_i2c_wr_u8(ctx, 0x57aa,0xeb);
	subdrv_i2c_wr_u8(ctx, 0x3221,0x66);
}

static int open(struct subdrv_ctx *ctx)
{
	u32 sensor_id = 0;
	u32 scenario_id = 0;
	LOG_INF("open enter");

	/* get sensor id */
	if (get_imgsensor_id(ctx, &sensor_id) != ERROR_NONE)
		return ERROR_SENSOR_CONNECT_FAIL;

	/* initail setting */
	himalayanmain_sensor_init(ctx);

	memset(ctx->exposure, 0, sizeof(ctx->exposure));
	memset(ctx->ana_gain, 0, sizeof(ctx->gain));
	ctx->exposure[0] = ctx->s_ctx.exposure_def;
	ctx->ana_gain[0] = ctx->s_ctx.ana_gain_def;
	ctx->current_scenario_id = scenario_id;
	ctx->pclk = ctx->s_ctx.mode[scenario_id].pclk;
	ctx->line_length = ctx->s_ctx.mode[scenario_id].linelength;
	ctx->frame_length = ctx->s_ctx.mode[scenario_id].framelength;
	ctx->current_fps = 10 * ctx->pclk / ctx->line_length / ctx->frame_length;
	ctx->readout_length = ctx->s_ctx.mode[scenario_id].readout_length;
	ctx->read_margin = ctx->s_ctx.mode[scenario_id].read_margin;
	ctx->min_frame_length = ctx->frame_length;
	ctx->autoflicker_en = FALSE;
	ctx->test_pattern = 0;
	ctx->ihdr_mode = 0;
	ctx->pdaf_mode = 0;
	ctx->hdr_mode = 0;
	ctx->extend_frame_length_en = 0;
	ctx->is_seamless = 0;
	ctx->fast_mode_on = 0;
	ctx->sof_cnt = 0;
	ctx->ref_sof_cnt = 0;
	ctx->is_streaming = 0;

	return ERROR_NONE;
}

static u16 get_gain2reg(u32 gain)
{
	return (u16)gain;
}
static void himalayanmain_set_dummy(struct subdrv_ctx *ctx)
{
	DRV_LOG(ctx, "frame length = %d\n", ctx->frame_length);
	subdrv_i2c_wr_u8(ctx, 0x320e, ctx->frame_length >> 8);
	subdrv_i2c_wr_u8(ctx, 0x320f, ctx->frame_length & 0xFF);
}

static void himalayanmain_set_max_framerate(struct subdrv_ctx *ctx, UINT16 framerate, kal_bool min_framelength_en)
{
	kal_uint32 frame_length = ctx->frame_length;

	DRV_LOG(ctx, "framerate = %d, min framelength should enable %d? \n", framerate,min_framelength_en);
	frame_length = ctx->pclk / framerate * 10 / ctx->line_length;
	if (frame_length >= ctx->min_frame_length)
		ctx->frame_length = frame_length;
	else
		ctx->frame_length = ctx->min_frame_length;

	ctx->dummy_line =
		ctx->frame_length - ctx->min_frame_length;

	if (ctx->frame_length > ctx->max_frame_length) {
		ctx->frame_length = ctx->max_frame_length;

		ctx->dummy_line =
			ctx->frame_length - ctx->min_frame_length;
	}
	if (min_framelength_en)
		ctx->min_frame_length = ctx->frame_length;
	himalayanmain_set_dummy(ctx);
}
static int himalayanmain_set_gain(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u64* feature_data = (u64*)para;
	u32 gain = *feature_data;
	uint32_t rg_gain = 0;
	int16_t gain_index = 0;
	uint32_t temp_gain = 0;
	uint32_t HIMALAYANMAIN_AGC_Param[HIMALAYANMAIN_SENSOR_GAIN_MAX_VALID_INDEX][2] = {
		{  1024,  0x00 },
		{  2048,  0x08 },
		{  4096,  0x09 },
		{  8192,  0x0b },
		{ 16384,  0x0f },
		{ 32768,  0x1f },
	};
	rg_gain = gain;
	if (rg_gain < HIMALAYANMAIN_SENSOR_GAIN_BASE) {
        rg_gain = HIMALAYANMAIN_SENSOR_GAIN_BASE;
	} else if (rg_gain > HIMALAYANMAIN_SENSOR_GAIN_MAX) {
        rg_gain = HIMALAYANMAIN_SENSOR_GAIN_MAX;
	}
	DRV_LOG(ctx, "Gain_Debug pass_gain= 0x%x\n", gain);
	/* mapping of gain to register value */
	if (ctx->s_ctx.g_gain2reg != NULL)
		rg_gain = ctx->s_ctx.g_gain2reg(rg_gain);
	else
		rg_gain = gain2reg(rg_gain);
	/* restore gain */
	memset(ctx->ana_gain, 0, sizeof(ctx->ana_gain));
	ctx->ana_gain[0] = rg_gain;
	for (gain_index = HIMALAYANMAIN_SENSOR_GAIN_MAX_VALID_INDEX - 1; gain_index >= 0; gain_index--)
		if (rg_gain >= HIMALAYANMAIN_AGC_Param[gain_index][0])
			break;
	/* write gain */
	subdrv_i2c_wr_u8(ctx, 0x3e08, HIMALAYANMAIN_AGC_Param[gain_index][1]);
	temp_gain = rg_gain * HIMALAYANMAIN_SENSOR_GAIN_BASE / HIMALAYANMAIN_AGC_Param[gain_index][0];
	subdrv_i2c_wr_u8(ctx, 0x3e07, (temp_gain >> 3) & 0xff);
	DRV_LOG(ctx, "gain[0x%x], %x, %x\n", rg_gain, HIMALAYANMAIN_AGC_Param[gain_index][1], (temp_gain >> 3) & 0xff);

	return 0;
}

static int himalayanmain_set_test_pattern(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u32 mode = *((u32 *)para);

	if (mode != ctx->test_pattern)
		LOG_INF("mode(%u->%u)\n", ctx->test_pattern, mode);
	if (mode) {
		subdrv_i2c_wr_u8(ctx, 0x3337, 0x30);
		subdrv_i2c_wr_u8(ctx, 0x391f, 0xc8);
		subdrv_i2c_wr_u8(ctx, 0x3e08, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x3e07, 0x80);
		subdrv_i2c_wr_u8(ctx, 0x3e06, 0x00);
	}else{
		subdrv_i2c_wr_u8(ctx, 0x3337, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x391f, 0xc9);
	}
	ctx->test_pattern = mode;
	return ERROR_NONE;
}

static int init_ctx(struct subdrv_ctx *ctx,	struct i2c_client *i2c_client, u8 i2c_write_id)
{
	LOG_INF("init_ctx enter");
	memcpy(&(ctx->s_ctx), &static_ctx, sizeof(struct subdrv_static_ctx));
	subdrv_ctx_init(ctx);
	ctx->i2c_client = i2c_client;
	ctx->i2c_write_id = i2c_write_id;
	return 0;
}

static int vsync_notify(struct subdrv_ctx *ctx,	unsigned int sof_cnt)
{
	LOG_INF("sof_cnt(%u) ctx->ref_sof_cnt(%u) ctx->fast_mode_on(%d)",
		sof_cnt, ctx->ref_sof_cnt, ctx->fast_mode_on);
	ctx->sof_cnt = sof_cnt;
	if (ctx->fast_mode_on && (sof_cnt > ctx->ref_sof_cnt)) {
		ctx->fast_mode_on = FALSE;
		ctx->ref_sof_cnt = 0;
		LOG_INF("seamless_switch disabled.");
		set_i2c_buffer(ctx, 0x3010, 0x00);
		set_i2c_buffer(ctx, 0x3036, 0x00);
		commit_i2c_buffer(ctx);
	}
	return 0;
}


static int himalayanmain_set_shutter(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	int fine_integ_line = 0;
	u64 *feature_data = (u64 *)para;
	u32 shutter = *feature_data;
	u32 frame_length = *(feature_data + 1);
	kal_uint16 realtime_fps = 0;
	bool gph = !ctx->is_seamless && (ctx->s_ctx.s_gph != NULL);
	ctx->frame_length = frame_length ? frame_length : ctx->min_frame_length;
	check_current_scenario_id_bound(ctx);
	/* check boundary of shutter */
	fine_integ_line = ctx->s_ctx.mode[ctx->current_scenario_id].fine_integ_line;
	shutter = FINE_INTEG_CONVERT(shutter, fine_integ_line);
	shutter = max_t(u64, shutter,
		(u64)ctx->s_ctx.mode[ctx->current_scenario_id].multi_exposure_shutter_range[0].min);
	shutter = min_t(u64, shutter,
		(u64)ctx->s_ctx.mode[ctx->current_scenario_id].multi_exposure_shutter_range[0].max);
	/* check boundary of framelength */
	ctx->frame_length = max((u32)shutter + ctx->s_ctx.exposure_margin, ctx->min_frame_length);
	ctx->frame_length = min(ctx->frame_length, ctx->s_ctx.frame_length_max);
	/* restore shutter */
	memset(ctx->exposure, 0, sizeof(ctx->exposure));
	ctx->exposure[0] = (u32) shutter;
	/* group hold start */
	if (gph)
		ctx->s_ctx.s_gph((void *)ctx, 1);
	/* enable auto extend */
	if (ctx->s_ctx.reg_addr_auto_extend)
		subdrv_i2c_wr_u8(ctx, ctx->s_ctx.reg_addr_auto_extend, 0x01);

	if (ctx->autoflicker_en && ctx->line_length && ctx->frame_length) {
		realtime_fps = ctx->pclk / ctx->line_length * 10 / ctx->frame_length;
		DRV_LOG(ctx, "realtime_fps : %d", realtime_fps);
		if (realtime_fps >= 297 && realtime_fps <= 305) {
			himalayanmain_set_max_framerate(ctx, 296, 0);
		} else if (realtime_fps >= 147 && realtime_fps <= 150) {
			himalayanmain_set_max_framerate(ctx, 146, 0);
		} else {
			subdrv_i2c_wr_u8(ctx,	ctx->s_ctx.reg_addr_frame_length.addr[0],(ctx->frame_length >> 8) & 0xFF);
			subdrv_i2c_wr_u8(ctx,	ctx->s_ctx.reg_addr_frame_length.addr[1],(ctx->frame_length) & 0xFF);
		}
	} else {
		subdrv_i2c_wr_u8(ctx,	ctx->s_ctx.reg_addr_frame_length.addr[0],(ctx->frame_length >> 8) & 0xFF);
		subdrv_i2c_wr_u8(ctx,	ctx->s_ctx.reg_addr_frame_length.addr[1],(ctx->frame_length) & 0xFF);
	}
	/* write shutter */

	ctx->exposure[0] = ctx->exposure[0] *2;

	subdrv_i2c_wr_u8(ctx,	ctx->s_ctx.reg_addr_exposure[0].addr[0],(ctx->exposure[0] >> 12) & 0x0F);
	subdrv_i2c_wr_u8(ctx,	ctx->s_ctx.reg_addr_exposure[0].addr[1],(ctx->exposure[0] >> 4)  & 0xFF);
	subdrv_i2c_wr_u8(ctx,	ctx->s_ctx.reg_addr_exposure[0].addr[2],(ctx->exposure[0] << 4)  & 0xF0);

	DRV_LOG(ctx, "exp[0x%x], fll(input/output):%u/%u, flick_en:%d\n",
		ctx->exposure[0], frame_length, ctx->frame_length, ctx->autoflicker_en);
	if (!ctx->ae_ctrl_gph_en) {
		if (gph)
			ctx->s_ctx.s_gph((void *)ctx, 0);
		commit_i2c_buffer(ctx);
	}
	/* group hold end */

	return ERROR_NONE;
}

static void streaming_ctrl(struct subdrv_ctx *ctx, bool enable)
{
	check_current_scenario_id_bound(ctx);
	if (ctx->s_ctx.mode[ctx->current_scenario_id].aov_mode) {
		DRV_LOG(ctx, "AOV mode set stream in SCP side! (sid:%u)\n",
			ctx->current_scenario_id);
		return;
	}

	if (enable) {
		if (ctx->s_ctx.chk_s_off_sta) {
			DRV_LOG(ctx, "check_stream_off before stream on");
			check_stream_off(ctx);
		}
		subdrv_i2c_wr_u8(ctx, ctx->s_ctx.reg_addr_stream, 0x01);
	} else {
		check_stream_on(ctx);  // check_stream_on before stream off
		subdrv_i2c_wr_u8(ctx, ctx->s_ctx.reg_addr_stream, 0x00);
		if (ctx->s_ctx.reg_addr_fast_mode && ctx->fast_mode_on) {
			ctx->fast_mode_on = FALSE;
			ctx->ref_sof_cnt = 0;
			DRV_LOG(ctx, "seamless_switch disabled.");
			set_i2c_buffer(ctx, ctx->s_ctx.reg_addr_fast_mode, 0x00);
			commit_i2c_buffer(ctx);
		}
	}
	mdelay(10);
	ctx->is_streaming = enable;
	DRV_LOG(ctx, "X! enable:%u\n", enable);
}

static int himalayanmain_streaming_resume(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
		DRV_LOG(ctx, "SENSOR_FEATURE_SET_STREAMING_RESUME, shutter:%u\n", *(u32 *)para);
		if (*(u32 *)para)
			himalayanmain_set_shutter(ctx, para, len);
		streaming_ctrl(ctx, true);
		return 0;
}

static int himalayanmain_streaming_suspend(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
		DRV_LOG(ctx, "streaming control para:%d\n", *para);
		streaming_ctrl(ctx, false);
		return 0;
}

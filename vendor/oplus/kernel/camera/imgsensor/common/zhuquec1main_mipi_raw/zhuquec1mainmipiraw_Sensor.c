// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 MediaTek Inc.

/*****************************************************************************
 *
 * Filename:
 * ---------
 *	 zhuquec1mainmipiraw_Sensor.c
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
#include "zhuquec1mainmipiraw_Sensor.h"
//OV50E40
#define SENSOR_NAME  SENSOR_DRVNAME_ZHUQUEC1MAIN_MIPI_RAW

#define ZHUQUEC1MAIN_EEPROM_ADDR         (0xA0)
#define ZHUQUEC1MAIN_EEPROM_MAX_OFFSET   (0x4000)
//#define OPLUS_CAMERA_COMMON_DATA_LENGTH  (40)

#define PFX "zhuquec1main_camera_sensor"
#define LOG_INF(format, args...) pr_err(PFX "[%s] " format, __func__, ##args)


#ifdef  EEPROM_WRITE_DATA_MAX_LENGTH
#undef  EEPROM_WRITE_DATA_MAX_LENGTH
#endif
#define EEPROM_WRITE_DATA_MAX_LENGTH          (64)
#define ZHUQUEC1MAIN_STEREO_MW_START_ADDR     (0x2B00)
#define ZHUQUEC1MAIN_STEREO_MT_START_ADDR     (0x31A0)
#define ZHUQUEC1MAIN_STEREO_MT105_START_ADDR  (0x3840)
#define ZHUQUEC1MAIN_AESYNC_START_ADDR        (0x3EE0)

#define  OTP_XTC_ADDR             (0x1A00)
#define  OTP_XTC_LENGTH           (3584)
#define  OTP_XTC_VALID_ADDR       (0x2800)
#define  OTP_XTC_IS_VALID_VAL     (0x01)

#define  XTC_SENSOR_ADDR_PART1    (0x5A20)
#define  XTC_SENSOR_LENGTH_PART1  (0x5A3F - 0x5A20 + 1) //32
#define  XTC_SENSOR_ADDR_PART2    (0x5AC0)
#define  XTC_SENSOR_LENGTH_PART2  (0x688F - 0x5AC0 + 1) //3536
#define  XTC_SENSOR_ADDR_PART3    (0x68AE)
#define  XTC_SENSOR_LENGTH_PART3  (0x68BD - 0x68AE + 1) //16
static u8 xtc_is_valid = 0;

#define ZHUQUEC1MAIN_IMGSENSOR_ID   (0x565045)

#define ZHUQUEC1MAIN_UNIQUE_SENSOR_ID_ADDR    (0x7000)  //??????
#define ZHUQUEC1MAIN_UNIQUE_SENSOR_ID_LENGTH  (16)
//static BYTE zhuquec1main_unique_id[ZHUQUEC1MAIN_UNIQUE_SENSOR_ID_LENGTH] = { 0 };
static struct oplus_eeprom_info_struct  oplus_eeprom_info = {0};

static bool is_first_exp = true;
static u8 hgc_value[2] = {0};
static u8 scg_flag = false;
static u8 exp_offset = 0;
static u16 fix_short_exp = 0;
static kal_uint8 otp_data_checksum[ZHUQUEC1MAIN_EEPROM_MAX_OFFSET] = {0};
static void zhuquec1main_set_sensor_cali(void *arg);
static int get_sensor_temperature(void *arg);
#define MAX_BURST_LEN  (2048)
static u8 * msg_buf = NULL;
static void set_group_hold(void *arg, u8 en);
static u16 get_gain2reg(u32 gain);
static int zhuquec1main_seamless_switch(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_set_test_pattern(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_check_sensor_id(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_get_eeprom_comdata(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_set_eeprom_calibration(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_get_eeprom_calibration(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_get_otp_checksum_data(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_get_min_shutter_by_scenario_adapter(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int get_imgsensor_id(struct subdrv_ctx *ctx, u32 *sensor_id);
static int open(struct subdrv_ctx *ctx);
static int init_ctx(struct subdrv_ctx *ctx,	struct i2c_client *i2c_client, u8 i2c_write_id);
static int vsync_notify(struct subdrv_ctx *ctx,	unsigned int sof_cnt);
static void zhuquec1main_get_sensor_cali(void* arg);
static int zhuquec1main_set_max_framerate_by_scenario(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_extend_frame_length(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static void zhuquec1main_set_gain_convert(struct subdrv_ctx *ctx, u32 gain);
static int zhuquec1main_set_gain(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static void zhuquec1main_set_multi_gain(struct subdrv_ctx *ctx, u32 *gains, u16 exp_cnt);
static void zhuquec1main_set_hdr_tri_gain(struct subdrv_ctx *ctx, u64 *gains, u16 exp_cnt);
static int zhuquec1main_set_hdr_tri_gain2(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_set_hdr_tri_gain3(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static void zhuquec1main_set_shutter_convert(struct subdrv_ctx *ctx, u64 shutter);
static int zhuquec1main_set_shutter(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_set_shutter_frame_length(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static void zhuquec1main_set_shutter_frame_length_convert(struct subdrv_ctx *ctx, u64 shutter, u32 frame_length);
static void zhuquec1main_set_multi_shutter_frame_length(struct subdrv_ctx *ctx, u64 *shutters, u16 exp_cnt, u16 frame_length);
static int zhuquec1main_set_multi_shutter_frame_length_ctrl(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_set_hdr_tri_shutter2(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_set_hdr_tri_shutter3(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static bool read_cmos_eeprom_p8(struct subdrv_ctx *ctx, kal_uint16 addr,
                    BYTE *data, int size);
static int zhuquec1main_i2c_burst_wr_regs_u8(struct subdrv_ctx *ctx, u16 * list, u32 len);
static int adapter_i2c_burst_wr_regs_u8(struct subdrv_ctx * ctx,
		u16 addr, u16 *list, u32 len);
static bool g_id_from_dts_flag = false;
static void get_imgsensor_id_from_dts(struct subdrv_ctx *ctx, u32 *sensor_id);
//static int zhuquec1main_streaming_resume(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int zhuquec1main_streaming_suspend(struct subdrv_ctx *ctx, u8 *para, u32 *len);
//static void zhuquec1main_write_frame_length(struct subdrv_ctx *ctx, u32 fll);
// static int zhuquec1main_get_unique_sensorid(struct subdrv_ctx *ctx, u8 *para, u32 *len);
// static int zhuquec1main_get_cloud_otp_info(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static void zhuquec1main_send_diff_settings(struct subdrv_ctx *ctx, kal_uint16 * target_list, u32 target_length, kal_uint16 * base_list, u32 base_length);
static void zhuquec1main_get_hgc_from_settings(struct subdrv_ctx *ctx);
static void zhuquec1main_set_hgc_to_buf(struct subdrv_ctx *ctx, u32 reg_gain, u32 reg_gain1);
static int zhuquec1main_common_control(struct subdrv_ctx *ctx,
			enum SENSOR_SCENARIO_ID_ENUM scenario_id,
			MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data);
static void zhuquec1main_get_exp_offset_from_settings(struct subdrv_ctx *ctx);
/* STRUCT */

static struct mtk_sensor_saturation_info imgsensor_saturation_info_10bit = {
	.gain_ratio = 1000,
	.OB_pedestal = 64,
	.saturation_level = 1023,
};

static struct eeprom_map_info zhuquec1main_eeprom_info[] = {
	{ EEPROM_META_MODULE_ID, 0x0000, 0x0010, 0x0011, 2, true },
	{ EEPROM_META_SENSOR_ID, 0x0006, 0x0010, 0x0011, 2, true },
	{ EEPROM_META_LENS_ID, 0x0008,0x0010, 0x0011, 2, true },
	{ EEPROM_META_VCM_ID, 0x000A, 0x0010, 0x0011, 2, true },
	{ EEPROM_META_MIRROR_FLIP, 0x000E, 0x0010, 0x0011, 1, true },
	{ EEPROM_META_MODULE_SN, 0x00B0, 0x00C7, 0x00C8,23, true },
	{ EEPROM_META_AF_CODE, 0x0092, 0x0098, 0x0099, 6, true },
	{ EEPROM_META_AF_FLAG, 0x0098, 0x0098, 0x0099, 1, true },
	{ EEPROM_META_STEREO_DATA, 0x0000, 0x0000, 0x0000, 0x0000, false },
	{ EEPROM_META_STEREO_MW_MAIN_DATA, ZHUQUEC1MAIN_STEREO_MW_START_ADDR, 0xFFFF, 0xFFFF, CALI_DATA_MASTER_LENGTH, true },
	{ EEPROM_META_STEREO_MT_MAIN_DATA, ZHUQUEC1MAIN_STEREO_MT_START_ADDR, 0xFFFF, 0xFFFF, CALI_DATA_MASTER_LENGTH, true },
	{ EEPROM_META_STEREO_MT_MAIN_DATA_105CM, ZHUQUEC1MAIN_STEREO_MT105_START_ADDR, 0xFFFF, 0xFFFF, CALI_DATA_MASTER_LENGTH, true },
	{ EEPROM_META_DISTORTION_DATA, 0, 0, 0, 0, false },
};

static struct subdrv_feature_control feature_control_list[] = {
	{SENSOR_FEATURE_SET_TEST_PATTERN, zhuquec1main_set_test_pattern},
	{SENSOR_FEATURE_SEAMLESS_SWITCH, zhuquec1main_seamless_switch},
	{SENSOR_FEATURE_CHECK_SENSOR_ID, zhuquec1main_check_sensor_id},
	{SENSOR_FEATURE_GET_EEPROM_COMDATA, zhuquec1main_get_eeprom_comdata},
	{SENSOR_FEATURE_SET_SENSOR_OTP, zhuquec1main_set_eeprom_calibration},
	{SENSOR_FEATURE_GET_EEPROM_STEREODATA, zhuquec1main_get_eeprom_calibration},
	{SENSOR_FEATURE_GET_SENSOR_OTP_ALL, zhuquec1main_get_otp_checksum_data},
	{SENSOR_FEATURE_GET_MIN_SHUTTER_BY_SCENARIO, zhuquec1main_get_min_shutter_by_scenario_adapter},
	{SENSOR_FEATURE_SET_ESHUTTER, zhuquec1main_set_shutter},
	{SENSOR_FEATURE_SET_SHUTTER_FRAME_TIME, zhuquec1main_set_shutter_frame_length},
	{SENSOR_FEATURE_SET_HDR_SHUTTER, zhuquec1main_set_hdr_tri_shutter2},
	{SENSOR_FEATURE_SET_HDR_TRI_SHUTTER, zhuquec1main_set_hdr_tri_shutter3},
	{SENSOR_FEATURE_SET_MULTI_SHUTTER_FRAME_TIME, zhuquec1main_set_multi_shutter_frame_length_ctrl},
	{SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO, zhuquec1main_set_max_framerate_by_scenario},
	{SENSOR_FEATURE_SET_SEAMLESS_EXTEND_FRAME_LENGTH, zhuquec1main_extend_frame_length},
	{SENSOR_FEATURE_SET_GAIN, zhuquec1main_set_gain},
	{SENSOR_FEATURE_SET_DUAL_GAIN, zhuquec1main_set_hdr_tri_gain2},
	{SENSOR_FEATURE_SET_HDR_TRI_GAIN, zhuquec1main_set_hdr_tri_gain3},
	{SENSOR_FEATURE_SET_STREAMING_SUSPEND, zhuquec1main_streaming_suspend},
//	{SENSOR_FEATURE_SET_STREAMING_RESUME, zhuquec1main_streaming_resume},
	// {SENSOR_FEATURE_GET_UNIQUE_SENSORID, zhuquec1main_get_unique_sensorid},
	// {SENSOR_FEATURE_GET_CLOUD_OTP_INFO, zhuquec1main_get_cloud_otp_info},
};

static struct eeprom_info_struct eeprom_info[] = {
	{
		.header_id = 0x01480116,
		.addr_header_id = 0x00000006,
		.i2c_write_id = ZHUQUEC1MAIN_EEPROM_ADDR,

// XTC/QPDC
		.pdc_support = TRUE,
		.pdc_size = OTP_XTC_LENGTH,
		.addr_pdc = OTP_XTC_ADDR,
		.sensor_reg_addr_pdc = XTC_SENSOR_ADDR_PART1,

	},

};

static struct SET_PD_BLOCK_INFO_T imgsensor_pd_info = {  //QPD
	.i4OffsetX = 0,
	.i4OffsetY = 0,
	.i4PitchX = 0,
	.i4PitchY = 0,
	.i4PairNum = 0,
	.i4SubBlkW = 0,
	.i4SubBlkH = 0,
	.i4PosL = {{0, 0} },
	.i4PosR = {{0, 0} },
	.i4BlockNumX = 0,
	.i4BlockNumY = 0,
	.i4LeFirst = 0,
	.i4Crop = {
		/* <pre> <cap> <normal_video> <hs_video> <slim_video> */
		{0, 0}, {0, 0}, {0, 384}, {0, 384}, {0, 0},
		/* <cus1> <cus2> <cus3> <cus4> <cus5> <cus6>*/
		{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
		/* <cus7> <cus8> <cus9> <cus10> <cus11> */
		{1160, 870}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
		/* <cus12> <cus13> <cus14> <cus15>*/
		{0, 512}, {0, 384}, {0, 0}, {0, 0},
	},
	.iMirrorFlip = IMAGE_NORMAL,
	.i4FullRawW = 4096,
	.i4FullRawH = 3072,
	.i4VCPackNum = 1,
	.PDAF_Support = PDAF_SUPPORT_CAMSV_QPD,//PDAF_SUPPORT_CAMSV_QPD,
	.i4ModeIndex = 0x3,
	.sPDMapInfo[0] = {
		.i4PDPattern = 1,//all-pd
		.i4BinFacX = 2,
		.i4BinFacY = 4,
		.i4PDRepetition = 0,
		.i4PDOrder = {1}, //R=1, L=0
	},
};

static struct SET_PD_BLOCK_INFO_T imgsensor_pd_info_v2h2 = {
	.i4OffsetX = 0,
	.i4OffsetY = 0,
	.i4PitchX = 0,
	.i4PitchY = 0,
	.i4PairNum = 0,
	.i4SubBlkW = 0,
	.i4SubBlkH = 0,
	.i4PosL = {{0, 0} },
	.i4PosR = {{0, 0} },
	.i4BlockNumX = 0,
	.i4BlockNumY = 0,
	.i4LeFirst = 0,
	.i4Crop = {
		/* <pre> <cap> <normal_video> <hs_video> <slim_video> */
		{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 192},
		/* <cus1> <cus2> <cus3> <cus4> <cus5> <cus6>*/
		{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
		/* <cus7> <cus8> <cus9> <cus10> <cus11> */
		{136, 102}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
		/* <cus12> <cus13> <cus14> <cus15>*/
		{0, 0}, {0, 0}, {0, 0}, {0, 0},
	},
	.iMirrorFlip = IMAGE_NORMAL,
	.i4FullRawW = 2048,
	.i4FullRawH = 1536,
	.i4VCPackNum = 1,
	.PDAF_Support = PDAF_SUPPORT_CAMSV_QPD,//PDAF_SUPPORT_CAMSV_QPD,
	.i4ModeIndex = 0x3,
	.sPDMapInfo[0] = {
		.i4PDPattern = 1,//all-pd
		.i4BinFacX = 2,
		.i4BinFacY = 4,
		.i4PDRepetition = 0,
		.i4PDOrder = {1}, //R=1, L=0
	},
};

static struct SET_PD_BLOCK_INFO_T imgsensor_pd_info_full = {
	.i4OffsetX = 0,
	.i4OffsetY = 0,
	.i4PitchX = 0,
	.i4PitchY = 0,
	.i4PairNum = 0,
	.i4SubBlkW = 0,
	.i4SubBlkH = 0,
	.i4PosL = {{0, 0} },
	.i4PosR = {{0, 0} },
	.i4BlockNumX = 0,
	.i4BlockNumY = 0,
	.i4LeFirst = 0,
	.i4Crop = {
		/* <pre> <cap> <normal_video> <hs_video> <slim_video> */
		{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
		/* <cus1> <cus2> <cus3> <cus4> <cus5> <cus6>*/
		{0, 0}, {0, 0}, {0, 0}, {0, 0}, {2048, 1536}, {0, 0},
		/* <cus7> <cus8> <cus9> <cus10> <cus11> */
		{0, 0}, {0, 0}, {2048, 1536}, {2048, 1536}, {2048, 1920},
		/* <cus12> <cus13> <cus14> <cus15>*/
		{0, 0}, {0, 0}, {0, 0}, {2048, 1536},
	},
	.iMirrorFlip = IMAGE_NORMAL,
	.i4FullRawW = 8192,
	.i4FullRawH = 6144,
	.i4VCPackNum = 1,
	.PDAF_Support = PDAF_SUPPORT_CAMSV_QPD,//PDAF_SUPPORT_CAMSV_QPD,
	.i4ModeIndex = 0x3,
	.sPDMapInfo[0] = {
		.i4PDPattern = 1,//all-pd
		.i4BinFacX = 4,
		.i4BinFacY = 4,
		.i4PDRepetition = 0,
		.i4PDOrder = {1}, //R=1, L=0
	},
};


static struct mtk_mbus_frame_desc_entry frame_desc_prev_cap[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 4096,
			.vsize = 768,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_vid[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 2304,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 4096,
			.vsize = 576,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};
static struct mtk_mbus_frame_desc_entry frame_desc_hs[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 2304,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
    // partial pd
	{
	    .bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 4096,
			.vsize = 576,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_slim[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 2048,
			.vsize = 1152,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 2048,
			.vsize = 576,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus1[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 2048,
			.vsize = 1152,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus2[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 4096,
			.vsize = 768,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus3[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 8196,
			.vsize = 6144,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus4[] = {
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus5[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 2048,
			.vsize = 768,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus6[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 1088,
			.vsize = 612,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus7[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 1776,
			.vsize = 1332,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 1776,
			.vsize = 333,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus8[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 2048,
			.vsize = 768,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus9[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 2048,
			.vsize = 768,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus10[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 2048,
			.vsize = 768,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus11[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 2304,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 2048,
			.vsize = 576,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus12[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 2048,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_ONLY_ONE,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 4096,
			.vsize = 512,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus13[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 2304,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_FIRST,
		},
	},
	{
		.bus.csi2 = {
			.channel = 1,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 2304,
			.user_data_desc = VC_STAGGER_ME,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_LAST,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 4096,
			.vsize = 576,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus14[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_FIRST,
		},
	},
	{
		.bus.csi2 = {
			.channel = 1,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_ME,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_LAST,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 4096,
			.vsize = 768,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus15[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_FIRST,
		},
	},
	{
		.bus.csi2 = {
			.channel = 1,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_ME,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_LAST,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 2048,
			.vsize = 768,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct mtk_mbus_frame_desc_entry frame_desc_cus16[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_NE,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_FIRST,
		},
	},
	{
		.bus.csi2 = {
			.channel = 1,
			.data_type = 0x2b,
			.hsize = 4096,
			.vsize = 3072,
			.user_data_desc = VC_STAGGER_ME,
			.fs_seq = MTK_FRAME_DESC_FS_SEQ_LAST,
		},
	},
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x30,
			.hsize = 4096,
			.vsize = 768,
			.user_data_desc = VC_PDAF_STATS_NE_PIX_1,
			.dt_remap_to_type = MTK_MBUS_FRAME_DESC_REMAP_TO_RAW10,
		},
	},
};

static struct subdrv_mode_struct mode_struct[] = {
    {//0_OV50E40_4096x3072_4C2PlusSCG_10bit_30fps_AG64_PDDT_4096x768_20241202.txt
		.frame_desc = frame_desc_prev_cap,
		.num_entries = ARRAY_SIZE(frame_desc_prev_cap),
		.mode_setting_table = zhuquec1main_preview_capture_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_preview_capture_setting),
		.seamless_switch_group = 1,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 6248,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,  //2.4Gsps * 3 * 2.28 / 10
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 4096,
			.scale_h = 3072,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = NORMAL_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_BIN,
		},
		.buffer_increase = 1,
	},
	{//0_OV50E40_4096x3072_4C2PlusSCG_10bit_30fps_AG64_PDDT_4096x768_20241202.txt
		.frame_desc = frame_desc_prev_cap,
		.num_entries = ARRAY_SIZE(frame_desc_prev_cap),
		.mode_setting_table = zhuquec1main_preview_capture_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_preview_capture_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 6248,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,  //2.4Gsps * 3 * 2.28 / 10
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 4096,
			.scale_h = 3072,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.ae_binning_ratio = 1,//cc
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = UNUSE_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_UNDEFINED,
		},
	},
	{//2_OV50E40_4096x2304_4C2PlusSCG_10bit_30fps_AG64_PDDT_4096x576_20241202.txt
		.frame_desc = frame_desc_vid,
		.num_entries = ARRAY_SIZE(frame_desc_vid),
		.mode_setting_table = zhuquec1main_normal_video_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_normal_video_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 6248,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 768,
			.w0_size = 8192,
			.h0_size = 4608,
			.scale_w = 4096,
			.scale_h = 2304,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 4096,
			.h1_size = 2304,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 2304,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = NORMAL_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_BIN,
		},
	},
    {//3_OV50E40_4096x2304_4C2PlusSCG_10bit_60fps_AG64_PDDT_4096x576_20241202.txt
		.frame_desc = frame_desc_hs,
		.num_entries = ARRAY_SIZE(frame_desc_hs),
		.mode_setting_table = zhuquec1main_hs_video_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_hs_video_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 3124,
		.max_framerate = 600,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 768,
			.w0_size = 8192,
			.h0_size = 4608,
			.scale_w = 4096,
			.scale_h = 2304,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 4096,
			.h1_size = 2304,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 2304,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = NORMAL_MASK,
			.equivalent_fps = 60,
			.sensorScenario = SENSOR_SCENARIO_BIN,
		},
		.buffer_increase = 1,
	},
    {//4_OV50E40_2048x1152_4C2PlusSCG_10bit_120fps_AG64_PDDT_2048x576_20241202.txt
		.frame_desc = frame_desc_slim,
		.num_entries = ARRAY_SIZE(frame_desc_slim),
		.mode_setting_table = zhuquec1main_slim_video_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_slim_video_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 1562,
		.max_framerate = 1200,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 768,
			.w0_size = 8192,
			.h0_size = 4608,
			.scale_w = 2048,
			.scale_h = 1152,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 2048,
			.h1_size = 1152,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 2048,
			.h2_tg_size = 1152,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info_v2h2,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = NORMAL_MASK,
			.equivalent_fps = 120,
			.sensorScenario = SENSOR_SCENARIO_BIN,
		},
	},
    {//5_OV50E40_2048x1152_4C1SCG_10bit_240fps_AG64_20241202.txt
		.frame_desc = frame_desc_cus1,
		.num_entries = ARRAY_SIZE(frame_desc_cus1),
		.mode_setting_table = zhuquec1main_custom1_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom1_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 237,
		.framelength = 1318,
		.max_framerate = 2400,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 768,
			.w0_size = 8192,
			.h0_size = 4608,
			.scale_w = 2048,
			.scale_h = 1152,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 2048,
			.h1_size = 1152,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 2048,
			.h2_tg_size = 1152,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = NORMAL_MASK,
			.equivalent_fps = 240,
			.sensorScenario = SENSOR_SCENARIO_BIN,
		},
	},
    {//6_OV50E40_4096x3072_4C2PlusSCG_10bit_24fps_AG64_PDDT_4096x768_20241202.txt
		.frame_desc = frame_desc_cus2,
		.num_entries = ARRAY_SIZE(frame_desc_cus2),
		.mode_setting_table = zhuquec1main_custom2_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom2_setting),
		.seamless_switch_group = 2,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 7812,
		.max_framerate = 240,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 4096,
			.scale_h = 3072,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = NORMAL_MASK,
			.equivalent_fps = 24,
			.sensorScenario = SENSOR_SCENARIO_BIN,
		},
	},
    {//7_OV50E40_8192x6144_10bit_11fps_AG16_PDDT_4096x1536LR_20241202.txt
		.frame_desc = frame_desc_cus3,
		.num_entries = ARRAY_SIZE(frame_desc_cus3),
		.mode_setting_table = zhuquec1main_custom3_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom3_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 600,
		.framelength = 11360,
		.max_framerate = 110,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 1,
		.coarse_integ_step = 1,
		.min_exposure_line = 8,
		.exposure_margin = 32,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 8192,
			.scale_h = 6144,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 8192,
			.h1_size = 6144,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 8192,
			.h2_tg_size = 6144,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1000,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 15.9375,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].max = BASEGAIN * 15.9375,
		.sensor_setting_info = {
			.sensor_scenario_usage = RMSC_MASK,
			.equivalent_fps = 11,
			.sensorScenario = SENSOR_SCENARIO_FULL_NCELL,
		},
	},
    {//8
		.frame_desc = frame_desc_cus4,
		.num_entries = ARRAY_SIZE(frame_desc_cus4),
		.mode_setting_table = zhuquec1main_custom4_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom4_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 800,
		.framelength = 3128,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 4096,
			.scale_h = 3072,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.ae_binning_ratio = 1,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = UNUSE_MASK,
			.equivalent_fps = 0,
			.sensorScenario = SENSOR_SCENARIO_UNDEFINED,
		},
	},
    {//9_OV50E40_4096x3072_Cropping_10bit_30fps_AG16_PDDT_2048x768LR_20241202.txt    //color error
		.frame_desc = frame_desc_cus5,
		.num_entries = ARRAY_SIZE(frame_desc_cus5),
		.mode_setting_table = zhuquec1main_custom5_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom5_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 6248,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 1,
		.coarse_integ_step = 1,
		.min_exposure_line = 8,
		.exposure_margin = 32,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 8192,
			.scale_h = 6144,
			.x1_offset = 2048,
			.y1_offset = 1536,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info_full,
		.ae_binning_ratio = 1000,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 15.9375,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].max = BASEGAIN * 15.9375,
		.sensor_setting_info = {
			.sensor_scenario_usage = INSENSORZOOM_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_CROP_BAYER,
		},
	},
    {//10_OV50E40_1088x612_4C1SCG_10bit_480fps_AG64_20241202.txt   //error
		.frame_desc = frame_desc_cus6,
		.num_entries = ARRAY_SIZE(frame_desc_cus6),
		.mode_setting_table = zhuquec1main_custom6_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom6_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 237,
		.framelength = 658,
		.max_framerate = 4800,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 1920,
			.y0_offset = 1848,
			.w0_size = 4352,
			.h0_size = 2448,
			.scale_w = 1088,
			.scale_h = 612,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 1088,
			.h1_size = 612,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 1088,
			.h2_tg_size = 612,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 1,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = UNUSE_MASK,
			.equivalent_fps = 480,
			.sensorScenario = SENSOR_SCENARIO_BIN,
		},
	},
    {//11-a_OV50E40_1776x1332_4C2PlusSCG_10bit_24fps_AG64_PDDT_1776x333_20250106.txt
		.frame_desc = frame_desc_cus7,
		.num_entries = ARRAY_SIZE(frame_desc_cus7),
		.mode_setting_table = zhuquec1main_custom7_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom7_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 7812,
		.max_framerate = 240,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 2320,
			.y0_offset = 1740,
			.w0_size = 3552,
			.h0_size = 2664,
			.scale_w = 1776,
			.scale_h = 1332,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 1776,
			.h1_size = 1332,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 1776,
			.h2_tg_size = 1332,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = NORMAL_MASK,
			.equivalent_fps = 24,
			.sensorScenario = SENSOR_SCENARIO_BIN,
		},
	},
    {//12_OV50E40_4096x3072_Cropping_Quad_10bit_30fps_AG16_PDDT_2048x768LR_20250206.txt
		.frame_desc = frame_desc_cus8,
		.num_entries = ARRAY_SIZE(frame_desc_cus8),
		.mode_setting_table = zhuquec1main_custom8_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom8_setting),
		.seamless_switch_group = 1,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_4CELL_R,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 6248,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 8192,
			.scale_h = 6144,
			.x1_offset = 2048,
			.y1_offset = 1536,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info_full,
		.ae_binning_ratio = 1000,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 15.9375,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].max = BASEGAIN * 15.9375,
		.sensor_setting_info = {
			.sensor_scenario_usage = INSENSORZOOM_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_UNDEFINED,
		},
	},
    {//13_OV50E40_4096x3072_Cropping_10bit_24fps_AG16_PDDT_2048x768LR_20241202.txt   //color error
		.frame_desc = frame_desc_cus9,
		.num_entries = ARRAY_SIZE(frame_desc_cus9),
		.mode_setting_table = zhuquec1main_custom9_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom9_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 7812,
		.max_framerate = 240,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 1,
		.coarse_integ_step = 1,
		.min_exposure_line = 8,
		.exposure_margin = 32,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 8192,
			.scale_h = 6144,
			.x1_offset = 2048,
			.y1_offset = 1536,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info_full,
		.ae_binning_ratio = 1000,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 15.9375,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].max = BASEGAIN * 15.9375,
		.sensor_setting_info = {
			.sensor_scenario_usage = UNUSE_MASK,
			.equivalent_fps = 0,
			.sensorScenario = SENSOR_SCENARIO_CROP_2X_NCELL,
		},
	},
    {//14_OV50E40_4096x3072_Cropping_Quad_10bit_24fps_AG16_PDDT_2048x768LR_20241212.txt  //color error
		.frame_desc = frame_desc_cus10,
		.num_entries = ARRAY_SIZE(frame_desc_cus10),
		.mode_setting_table = zhuquec1main_custom10_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom10_setting),
		.seamless_switch_group = 2,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_4CELL_R,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 7812,
		.max_framerate = 240,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 8192,
			.scale_h = 6144,
			.x1_offset = 2048,
			.y1_offset = 1536,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info_full,
		.ae_binning_ratio = 1000,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 15.9375,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].max = BASEGAIN * 15.9375,
		.sensor_setting_info = {
			.sensor_scenario_usage = INSENSORZOOM_MASK,
			.equivalent_fps = 24,
			.sensorScenario = SENSOR_SCENARIO_CROP_2X_NCELL,
		},
	},
    {//15_OV50E40_4096x2304_Cropping_10bit_30fps_AG16_PDDT_2048x576LR_20241202.txt  //color error
		.frame_desc = frame_desc_cus11,
		.num_entries = ARRAY_SIZE(frame_desc_cus11),
		.mode_setting_table = zhuquec1main_custom11_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom11_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 6248,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 1,
		.coarse_integ_step = 1,
		.min_exposure_line = 8,
		.exposure_margin = 32,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 8192,
			.scale_h = 6144,
			.x1_offset = 2048,
			.y1_offset = 1920,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 2304,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info_full,
		.ae_binning_ratio = 1000,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 15.9375,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].max = BASEGAIN * 15.9375,
		.sensor_setting_info = {
			.sensor_scenario_usage = INSENSORZOOM_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_CROP_2X_NCELL,
		},
	},
    {//16_OV50E40_4096x2048_4C2PlusSCG_10bit_30fps_AG64_PDDT_4096x512_20241202.txt
		.frame_desc = frame_desc_cus12,
		.num_entries = ARRAY_SIZE(frame_desc_cus12),
		.mode_setting_table = zhuquec1main_custom12_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom12_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 75000000,
		.linelength = 400,
		.framelength = 6248,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 1024,
			.w0_size = 8192,
			.h0_size = 4096,
			.scale_w = 4096,
			.scale_h = 2048,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 4096,
			.h1_size = 2048,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 2048,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = NORMAL_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_BIN,
		},
	},
    {//17_OV50E40_4096x2304_4C2PlusDCG_10bit_30fps_AG16_PDDT_4096x576_20241202.txt
		.frame_desc = frame_desc_cus13,
		.num_entries = ARRAY_SIZE(frame_desc_cus13),
		.mode_setting_table = zhuquec1main_custom13_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom13_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_RAW_DCG_RAW,
		.raw_cnt = 2,
		.exp_cnt = 2,
		.pclk = 75000000,
		.linelength = 775,
		.framelength = 3224,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_LE].min = 8,
		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_ME].min = 8,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 768,
			.w0_size = 8192,
			.h0_size = 4608,
			.scale_w = 4096,
			.scale_h = 2304,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 4096,
			.h1_size = 2304,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 2304,
		},
		.saturation_info = &imgsensor_saturation_info_10bit,
		.dcg_info = {
			.dcg_mode = IMGSENSOR_DCG_RAW,
			.dcg_gain_mode = IMGSENSOR_DCG_DIRECT_MODE,
			.dcg_gain_ratio_min = 1000,
			.dcg_gain_ratio_max = 16000,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].max = BASEGAIN * 63.75,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_ME].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_ME].max = BASEGAIN * 63.75,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = DCG_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_DCG_AP_MERGE_2EXP_BAYER,
		},
	},
    {//18_OV50E40_4096x3072_4C2PlusSCG_STG2_10bit_30fps_AG64_PDDT_4096x768_20241216.txt
		.frame_desc = frame_desc_cus14,
		.num_entries = ARRAY_SIZE(frame_desc_cus14),
		.mode_setting_table = zhuquec1main_custom14_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom14_setting),
		.seamless_switch_group = 1,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_RAW_STAGGER,
		.raw_cnt = 2,
		.exp_cnt = 2,
		.pclk = 75187500,
		.linelength = 400,
		.framelength = 3132 * 2,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 4096,
			.scale_h = 3072,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 63.75,
		.sensor_setting_info = {
			.sensor_scenario_usage = HDR_RAW_STAGGER_2EXP_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_STAGGER_2EXP_BAYER,
		},
		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_LE].min = 8,
//		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_LE].max = 4200,
		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_ME].min = 8,
		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_ME].max = 1936,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].max = BASEGAIN * 4,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_ME].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_ME].max = BASEGAIN * 4,
		.hw_mode = 2,//2:DC MODE, 1:OTF MODE
		.buffer_increase = 1,
	},
    {//19_OV50E40_4096x3072_Cropping_Quad_STG2_10bit_30fps_AG16_PDDT_2048x768LR_20241216.txt
		.frame_desc = frame_desc_cus15,
		.num_entries = ARRAY_SIZE(frame_desc_cus15),
		.mode_setting_table = zhuquec1main_custom15_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom15_setting),
		.seamless_switch_group = 1,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_4CELL_R,
		.hdr_mode = HDR_RAW_STAGGER,
		.raw_cnt = 2,
		.exp_cnt = 2,
		.pclk = 76650000,
		.linelength = 400,
		.framelength = 3192 * 2,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.exposure_margin = 16,
		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_LE].min = 8,
//		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_LE].max = 4240,
		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_ME].min = 8,
		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_ME].max = 1956,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 8192,
			.scale_h = 6144,
			.x1_offset = 2048,
			.y1_offset = 1536,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info_full,
		.ae_binning_ratio = 1000,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 15.9375,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].max = BASEGAIN * 15.9375,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_ME].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_ME].max = BASEGAIN * 15.9375,
		.sensor_setting_info = {
			.sensor_scenario_usage = HDR_RAW_STAGGER_2EXP_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_STAGGER_2EXP_CROP_4CELL,
		},
		.hw_mode = 2,//2:DC MODE, 1:OTF MODE
		.buffer_increase = 1,
	},
	{//20_OV50E40_4096x3072_4C2PlusDCG_10bit_30fps_AG16_PDDT_4096x768_20250116.txt
		.frame_desc = frame_desc_cus16,
		.num_entries = ARRAY_SIZE(frame_desc_cus16),
		.mode_setting_table = zhuquec1main_custom16_setting,
		.mode_setting_len = ARRAY_SIZE(zhuquec1main_custom16_setting),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_RAW_DCG_RAW,
		.raw_cnt = 2,
		.exp_cnt = 2,
		.pclk = 75000000,
		.linelength = 612,
		.framelength = 4084,
		.max_framerate = 300,
		.mipi_pixel_rate = 1641600000,
		.readout_length = 0,
		.read_margin = 0,
		.framelength_step = 2,
		.coarse_integ_step = 2,
		.min_exposure_line = 4,
		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_LE].min = 8,
		.multi_exposure_shutter_range[IMGSENSOR_EXPOSURE_ME].min = 8,
		.exposure_margin = 16,
		.imgsensor_winsize_info = {
			.full_w = 8192,
			.full_h = 6144,
			.x0_offset = 0,
			.y0_offset = 0,
			.w0_size = 8192,
			.h0_size = 6144,
			.scale_w = 4096,
			.scale_h = 3072,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 4096,
			.h1_size = 3072,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 4096,
			.h2_tg_size = 3072,
		},
		.saturation_info = &imgsensor_saturation_info_10bit,
		.dcg_info = {
			.dcg_mode = IMGSENSOR_DCG_RAW,
			.dcg_gain_mode = IMGSENSOR_DCG_DIRECT_MODE,
			.dcg_gain_ratio_min = 1000,
			.dcg_gain_ratio_max = 16000,
		},
		.pdaf_cap = TRUE,
		.imgsensor_pd_info = &imgsensor_pd_info,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].min = BASEGAIN * 4,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_LE].max = BASEGAIN * 15.9375,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_ME].min = BASEGAIN * 1,
		.multi_exposure_ana_gain_range[IMGSENSOR_EXPOSURE_ME].max = BASEGAIN * 15.9375,
		.ae_binning_ratio = 1240,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = {0},
		.ana_gain_max = BASEGAIN * 15.9375,
		.sensor_setting_info = {
			.sensor_scenario_usage = DCG_MASK,
			.equivalent_fps = 30,
			.sensorScenario = SENSOR_SCENARIO_DCG_AP_MERGE_2EXP_BAYER,
		},
	},
};

static struct subdrv_static_ctx static_ctx = {
	.sensor_id = ZHUQUEC1MAIN_SENSOR_ID,
	.reg_addr_sensor_id = {0x300a, 0x300b, 0x300c},
	.i2c_addr_table = {0x20, 0xFF},
	.i2c_burst_write_support = TRUE,
	.i2c_transfer_data_type = I2C_DT_ADDR_16_DATA_8,
	.eeprom_info = eeprom_info,
	.eeprom_num = ARRAY_SIZE(eeprom_info),
	.resolution = {8192, 6144},
	.mirror = IMAGE_NORMAL,

	.mclk = 24,
	.isp_driving_current = ISP_DRIVING_4MA,
	.sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,
	.mipi_sensor_type = MIPI_CPHY,
	.mipi_lane_num = SENSOR_MIPI_3_LANE,
	.ob_pedestal = 0x40,

	.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_4CELL_HW_BAYER_R,
	.ana_gain_def = BASEGAIN * 4,
	.ana_gain_min = BASEGAIN * 1,
	.ana_gain_max = BASEGAIN * 63.75,
	.ana_gain_type = 1,
	.ana_gain_step = 1,
	.ana_gain_table = zhuquec1main_ana_gain_table,
	.ana_gain_table_size = sizeof(zhuquec1main_ana_gain_table),
	.tuning_iso_base = 100,
	.exposure_def = 0x3D0,
	.exposure_min = 8,
	.exposure_max = 0xFFFFFF - 32,
	.exposure_step = 2,
	.exposure_margin = 32,

	.frame_length_max = 0xFFFFFF,
	.ae_effective_frame = 2,
	.frame_time_delay_frame = 2,
	.start_exposure_offset = 2293000,

	.pdaf_type = PDAF_SUPPORT_CAMSV_QPD,
	.hdr_type = HDR_SUPPORT_STAGGER_FDOL|HDR_SUPPORT_DCG,
	.seamless_switch_support = TRUE,
	.temperature_support = TRUE,

	.g_temp = get_sensor_temperature,
	.g_gain2reg = get_gain2reg,
	.g_cali = zhuquec1main_get_sensor_cali,
	.s_gph = set_group_hold,
	.s_cali = zhuquec1main_set_sensor_cali,

	.reg_addr_stream = 0x0100,
	.reg_addr_mirror_flip = PARAM_UNDEFINED, //0x3821  0x3820
	.reg_addr_exposure = {
			{0x3500, 0x3501, 0x3502},//Long exposure
			{0x3500, 0x3501, 0x3502},//Long exposure   skip
			{0x3540, 0x3541, 0x3542},//Medium exposure
//			{0x3580, 0x3581, 0x3582},//short exposure
	},
	.long_exposure_support = PARAM_UNDEFINED,
	.reg_addr_exposure_lshift = PARAM_UNDEFINED,
	.reg_addr_ana_gain = {
			{0x3508, 0x3509},//Long gain
			{0x3508, 0x3509},//Long gain  skip
			{0x3548, 0x3549},//Medium gain
//			{0x3588, 0x3589},//short gain
	},
	.reg_addr_frame_length = {0x3840, 0x380e, 0x380f},
	.reg_addr_temp_en = 0x4D12,
	.reg_addr_temp_read = 0x4D13,
	.reg_addr_auto_extend = PARAM_UNDEFINED,
	.reg_addr_frame_count = PARAM_UNDEFINED,
	.reg_addr_fast_mode = PARAM_UNDEFINED,

	.init_setting_table = zhuquec1main_init_setting,
	.init_setting_len = ARRAY_SIZE(zhuquec1main_init_setting),
	.mode = mode_struct,
	.sensor_mode_num = ARRAY_SIZE(mode_struct),
	.list = feature_control_list,
	.list_len = ARRAY_SIZE(feature_control_list),

	.chk_s_off_sta = 0,
	.chk_s_off_end = 0,
	.checksum_value = 0xcd9966da,
};

static struct subdrv_ops ops = {
	.get_id = get_imgsensor_id,
	.init_ctx = init_ctx,
	.open = open,
	.get_info = common_get_info,
	.get_resolution = common_get_resolution,
	.control = zhuquec1main_common_control,
	.feature_control = common_feature_control,
	.close = common_close,
	.get_frame_desc = common_get_frame_desc,
	.get_temp = common_get_temp,
	.get_csi_param = common_get_csi_param,
	.vsync_notify = vsync_notify,
	.update_sof_cnt = common_update_sof_cnt,
};

static struct subdrv_pw_seq_entry pw_seq[] = {
	{HW_ID_RST, {0}, 1000},
	{HW_ID_MCLK, {24}, 0},
	{HW_ID_MCLK_DRIVING_CURRENT, {4}, 1000},
	{HW_ID_AVDD, {2804000, 2804000}, 0},
	{HW_ID_DOVDD, {1800000, 1800000}, 0},
	{HW_ID_AFVDD, {2804000, 2804000}, 0},
	{HW_ID_DVDD, {1104000, 1104000}, 1000},
	{HW_ID_RST, {1}, 5000},
};

struct subdrv_entry zhuquec1main_mipi_raw_entry = {
	.name = "zhuquec1main_mipi_raw",
	.id = ZHUQUEC1MAIN_SENSOR_ID,
	.pw_seq = pw_seq,
	.pw_seq_cnt = ARRAY_SIZE(pw_seq),
	.ops = &ops,
};


/* FUNCTION */

static unsigned int read_zhuquec1main_eeprom_info(struct subdrv_ctx *ctx, kal_uint16 meta_id,
	BYTE *data, int size)
{
	kal_uint16 addr;
	int readsize;

	if (meta_id != zhuquec1main_eeprom_info[meta_id].meta)
		return -1;

	if (size != zhuquec1main_eeprom_info[meta_id].size)
		return -1;

	addr = zhuquec1main_eeprom_info[meta_id].start;
	readsize = zhuquec1main_eeprom_info[meta_id].size;

	if(!read_cmos_eeprom_p8(ctx, addr, data, readsize)) {
		DRV_LOGE(ctx, "read meta_id(%d) failed", meta_id);
	}

	return 0;
}

static struct eeprom_addr_table_struct oplus_eeprom_addr_table = {
	.i2c_read_id = 0xA1,
	.i2c_write_id = 0xA0,

	.addr_modinfo = 0x0000,
	.addr_sensorid = 0x0006,
	.addr_lens = 0x0008,
	.addr_vcm = 0x000A,
    .addr_modinfoflag = 0x0010,

	.addr_af = 0x0092,
	.addr_afmacro = 0x0092,
	.addr_afinf = 0x0094,
	.addr_afflag = 0x0098,

	.addr_qrcode = 0x00B0,
	.addr_qrcodeflag = 0x00C7,
};

// static struct SENSOR_OTP_INFO_STRUCT cloud_otp_info[OPLUS_CAM_CAL_DATA_MAX] = {
// 	{
// 		.OtpInfoLen = 1,
// 		.OtpInfo = {{0x0000, 17}}, /*{addr_modinfo, addr_modinfolen}*/
// 	}, /*OPLUS_CAM_CAL_DATA_MODULE_VERSION*/
// 	{
// 		.OtpInfoLen = 1,
// 		.OtpInfo = {{0x0000, 17}}, /*{addr_modinfo, addr_modinfolen}*/
// 	}, /*OPLUS_CAM_CAL_DATA_PART_NUMBER*/
// 	{
// 		.OtpInfoLen = 1,
// 		.OtpInfo = {{0x1d60, 1868}},
// 	}, /*OPLUS_CAM_CAL_DATA_SHADING_TABLE--LSC*/
// 	{
// 		.OtpInfoLen = 5,
// 		.OtpInfo = {{0x0020, 16}, {0x0044, 16}, {0x0060, 4}, {0x006c, 4}, {0x0092, 6}},
// 		.isAFCodeOffset = KAL_FALSE,
// 	}, /*OPLUS_CAM_CAL_DATA_3A_GAIN-awb5000\awb2850\awb5000Light\awb2850light\af*/
// 	{
// 		.OtpInfoLen = 2,
// 		.OtpInfo = {{0x1300, 496}, {0x1500, 1004}},
// 	}, /*OPLUS_CAM_CAL_DATA_PDAF*/
// 	{
// 		.OtpInfoLen = 8,
// 		.OtpInfo = {{0x0000, 17}, {0x0006, 2}, {0x0008, 2}, {0x000a, 2}, {0x0092, 7}, {0x0092, 2}, {0x0094, 2}, {0x00b0, 24}},
// 		.isAFCodeOffset = KAL_FALSE,
// 	}, /*OPLUS_CAM_CAL_DATA_CAMERA_INFO-modid\sensor\lens\vcmid\af\macpos\infpos\qrcode\*/
// 	{
// 		.OtpInfoLen = 1,
// 		.OtpInfo = {{0x0008, 2}},
// 	}, /*OPLUS_CAM_CAL_DATA_DUMP*/
// 	{
// 		.OtpInfoLen = 1,
// 		.OtpInfo = {{0x0008, 2}},
// 	}, /*OPLUS_CAM_CAL_DATA_LENS_ID*/
// 	{
// 		.OtpInfoLen = 0,
// 	}, /*OPLUS_CAM_CAL_DATA_QSC*/
// 	{
// 		.OtpInfoLen = 0,
// 	}, /*OPLUS_CAM_CAL_DATA_LRC*/
// 	{
// 		.OtpInfoLen = 1,
// 		.OtpInfo = {{0x0000, 16384}},
// 	}, /*OPLUS_CAM_CAL_DATA_ALL*/
// };

// static int zhuquec1main_get_cloud_otp_info(struct subdrv_ctx *ctx, u8 *para, u32 *len)
// {
// 	u64 *feature_data = (u64 *)para;
// 	struct SENSOR_OTP_INFO_STRUCT *cloudinfo;
// 	LOG_INF("SENSOR_FEATURE_GET_CLOUD_OTP_INFO otp_type:%d", (UINT32)(*feature_data));
// 	cloudinfo = (struct SENSOR_OTP_INFO_STRUCT *)(uintptr_t)(*(feature_data + 1));
// 	switch (*feature_data) {
// 	case OPLUS_CAM_CAL_DATA_MODULE_VERSION:
// 	case OPLUS_CAM_CAL_DATA_PART_NUMBER:
// 	case OPLUS_CAM_CAL_DATA_SHADING_TABLE:
// 	case OPLUS_CAM_CAL_DATA_3A_GAIN:
// 	case OPLUS_CAM_CAL_DATA_PDAF:
// 	case OPLUS_CAM_CAL_DATA_CAMERA_INFO:
// 	case OPLUS_CAM_CAL_DATA_DUMP:
// 	case OPLUS_CAM_CAL_DATA_LENS_ID:
// 	case OPLUS_CAM_CAL_DATA_QSC:
// 	case OPLUS_CAM_CAL_DATA_LRC:
// 	case OPLUS_CAM_CAL_DATA_ALL:
// 		memcpy((void *)cloudinfo, (void *)&cloud_otp_info[*feature_data], sizeof(struct SENSOR_OTP_INFO_STRUCT));
// 		break;
// 	default:
// 		break;
// 	}
// 	return 0;
// }

// static void read_unique_sensorid(struct subdrv_ctx *ctx)
// {
// 	u8 i = 0;
// 	LOG_INF("read sensor unique sensorid");
// 	while (ctx->s_ctx.i2c_addr_table[i] != 0xFF) {
// 		ctx->i2c_write_id = ctx->s_ctx.i2c_addr_table[i];
// 		subdrv_i2c_wr_u8(ctx, 0x0103, 0x01);
// 		subdrv_i2c_wr_u8(ctx, 0x3d84, 0x00);
// 		subdrv_i2c_wr_u8(ctx, 0x3d85, 0x1b);
// 		subdrv_i2c_wr_u8(ctx, 0x0100, 0x01);
// 		msleep(5);
// 		if (adaptor_i2c_rd_p8(ctx->i2c_client, ctx->i2c_write_id >> 1, ZHUQUEC1MAIN_UNIQUE_SENSOR_ID_ADDR,
// 			&(zhuquec1main_unique_id[0]), ZHUQUEC1MAIN_UNIQUE_SENSOR_ID_LENGTH) < 0) {
// 			LOG_INF("Read sensor unique sensorid fail. i2c_write_id: 0x%x\n", ctx->i2c_write_id);
// 		}
// 		i++;
// 	}
// }

// static int zhuquec1main_get_unique_sensorid(struct subdrv_ctx *ctx, u8 *para, u32 *len)
// {
// 	u32 *feature_return_para_32 = (u32 *)para;
// 	*len = ZHUQUEC1MAIN_UNIQUE_SENSOR_ID_LENGTH;
// 	memcpy(feature_return_para_32, zhuquec1main_unique_id,
// 		ZHUQUEC1MAIN_UNIQUE_SENSOR_ID_LENGTH);
// 	LOG_INF("para :%x, get unique sensorid", *para);
// 	return 0;
// }

static int zhuquec1main_get_eeprom_comdata(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	LOG_INF("+");
	memcpy(para, (u8*)(&oplus_eeprom_info), sizeof(oplus_eeprom_info));
	*len = sizeof(oplus_eeprom_info);
	return 0;
}

static kal_uint16 read_cmos_eeprom_8(struct subdrv_ctx *ctx, kal_uint16 addr)
{
	kal_uint16 get_byte = 0;

	adaptor_i2c_rd_u8(ctx->i2c_client, ZHUQUEC1MAIN_EEPROM_ADDR >> 1, addr, (u8 *)&get_byte);
	return get_byte;
}

static kal_int32 table_write_eeprom_one_packet(struct subdrv_ctx *ctx,
        kal_uint16 addr, kal_uint8 *para, kal_uint32 len)
{
    kal_int32 ret = ERROR_NONE;
    ret = adaptor_i2c_wr_p8(ctx->i2c_client, ZHUQUEC1MAIN_EEPROM_ADDR >> 1,
            addr, para, len);

    return ret;
}

static kal_int32 write_eeprom_protect(struct subdrv_ctx *ctx, kal_uint16 enable)
{
    kal_int32 ret = ERROR_NONE;
    kal_uint16 reg = 0xA000;

    if (enable) {
        adaptor_i2c_wr_u8(ctx->i2c_client, ZHUQUEC1MAIN_EEPROM_ADDR >> 1, reg, 0x0E);
    }
    else {
        adaptor_i2c_wr_u8(ctx->i2c_client, ZHUQUEC1MAIN_EEPROM_ADDR >> 1, reg, 0x00);
    }

    return ret;
}

static kal_uint16 get_64align_addr(kal_uint16 data_base) {

	kal_uint16 multiple = 0;
	kal_uint16 surplus = 0;
	kal_uint16 addr_64align = 0;

	multiple = data_base / 64;
	surplus = data_base % 64;
	if(surplus) {
		addr_64align = (multiple + 1) * 64;
	} else {
		addr_64align = multiple * 64;
	}
	//LOG_INF("data_base(0x%x), multiple(%d), surplus(%d), addr_64align(0x%x)", data_base, multiple, surplus, addr_64align);
	return addr_64align;
}

static kal_int32 eeprom_table_write(struct subdrv_ctx *ctx, kal_uint16 data_base, kal_uint8 *pData, kal_uint16 data_length) {

	kal_uint16 idx;
	kal_uint16 idy;
	kal_int32 ret = ERROR_NONE;
	UINT32 i = 0;

	idx = data_length / EEPROM_WRITE_DATA_MAX_LENGTH;
	idy = data_length % EEPROM_WRITE_DATA_MAX_LENGTH;

    LOG_INF("data_base(0x%x) data_length(%d) idx(%d) idy(%d)\n", data_base, data_length, idx, idy);

	for (i = 0; i < idx; i++ ) {
		ret = table_write_eeprom_one_packet(ctx, (data_base + EEPROM_WRITE_DATA_MAX_LENGTH * i),
				&pData[EEPROM_WRITE_DATA_MAX_LENGTH*i], EEPROM_WRITE_DATA_MAX_LENGTH);
		if (ret != ERROR_NONE) {
			LOG_INF("write_eeprom error: i=%d\n", i);
			return -1;
		}
		msleep(6);
	}

	msleep(6);
	if(idy) {
		ret = table_write_eeprom_one_packet(ctx, (data_base + EEPROM_WRITE_DATA_MAX_LENGTH*idx),
				&pData[EEPROM_WRITE_DATA_MAX_LENGTH*idx], idy);
		if (ret != ERROR_NONE) {
			LOG_INF("write_eeprom error: idx= %d idy= %d\n", idx, idy);
			return -1;
		}
	}
	return 0;
}

static kal_int32 eeprom_64align_write(struct subdrv_ctx *ctx, kal_uint16 data_base, kal_uint8 *pData, kal_uint16 data_length) {

	kal_uint16 addr_64align = 0;
	kal_uint16 part1_length = 0;
	kal_uint16 part2_length = 0;
	kal_int32 ret = ERROR_NONE;

    addr_64align = get_64align_addr(data_base);

	part1_length = addr_64align - data_base;
	if(part1_length > data_length) {
		part1_length = data_length;
	}
	part2_length = data_length - part1_length;

	write_eeprom_protect(ctx, 0);
	msleep(6);

	if (part1_length) {
		ret = eeprom_table_write(ctx, data_base, pData, part1_length);
		if (ret == -1) {
			/* open write protect */
			write_eeprom_protect(ctx, 1);
			LOG_INF("write_eeprom error part1\n");
			msleep(6);
			return -1;
		}
	}

	msleep(6);
	if (part2_length) {
		ret = eeprom_table_write(ctx, addr_64align, pData + part1_length, part2_length);
		if (ret == -1) {
			/* open write protect */
			write_eeprom_protect(ctx, 1);
			LOG_INF("write_eeprom error part2\n");
			msleep(6);
			return -1;
		}
	}
	msleep(6);
	write_eeprom_protect(ctx, 1);
	msleep(6);

	return 0;
}
static kal_int32 write_Module_data(struct subdrv_ctx *ctx,
    ACDK_SENSOR_ENGMODE_STEREO_STRUCT * pStereodata)
{
    kal_int32  ret = ERROR_NONE;
    kal_uint16 data_base, data_length;
    kal_uint8 *pData;

    if(pStereodata != NULL) {
        LOG_INF("SET_SENSOR_OTP: 0x%x %d 0x%x %d\n",
                       pStereodata->uSensorId,
                       pStereodata->uDeviceId,
                       pStereodata->baseAddr,
                       pStereodata->dataLength);

        data_base = pStereodata->baseAddr;
        data_length = pStereodata->dataLength;
        pData = pStereodata->uData;
        if (((pStereodata->uSensorId == ZHUQUEC1MAIN_SENSOR_ID) || (pStereodata->uSensorId == ZHUQUES1MAIN_SENSOR_ID))
            && (data_length == CALI_DATA_MASTER_LENGTH)
            && ((data_base == ZHUQUEC1MAIN_STEREO_MW_START_ADDR)
                || (data_base == ZHUQUEC1MAIN_STEREO_MT_START_ADDR)
                || (data_base == ZHUQUEC1MAIN_STEREO_MT105_START_ADDR))) {
            LOG_INF("Write: %x %x %x %x\n", pData[0], pData[39], pData[40], pData[1556]);

            eeprom_64align_write(ctx, data_base, pData, data_length);

            LOG_INF("com_0:0x%x\n", read_cmos_eeprom_8(ctx, data_base));
            LOG_INF("com_39:0x%x\n", read_cmos_eeprom_8(ctx, data_base+39));
            LOG_INF("innal_40:0x%x\n", read_cmos_eeprom_8(ctx, data_base+40));
            LOG_INF("innal_1556:0x%x\n", read_cmos_eeprom_8(ctx, data_base+1556));
            LOG_INF("write_Module_data Write end\n");

        } else if (((pStereodata->uSensorId == ZHUQUEC1MAIN_SENSOR_ID) || (pStereodata->uSensorId == ZHUQUES1MAIN_SENSOR_ID))
            && (data_length < AESYNC_DATA_LENGTH_TOTAL)
            && (data_base == ZHUQUEC1MAIN_AESYNC_START_ADDR)) {
            LOG_INF("write main aesync: %x %x %x %x %x %x %x %x\n", pData[0], pData[1],
                pData[2], pData[3], pData[4], pData[5], pData[6], pData[7]);

            eeprom_64align_write(ctx, data_base, pData, data_length);

            LOG_INF("readback main aesync: %x %x %x %x %x %x %x %x\n",
                    read_cmos_eeprom_8(ctx, ZHUQUEC1MAIN_AESYNC_START_ADDR),
                    read_cmos_eeprom_8(ctx, ZHUQUEC1MAIN_AESYNC_START_ADDR+1),
                    read_cmos_eeprom_8(ctx, ZHUQUEC1MAIN_AESYNC_START_ADDR+2),
                    read_cmos_eeprom_8(ctx, ZHUQUEC1MAIN_AESYNC_START_ADDR+3),
                    read_cmos_eeprom_8(ctx, ZHUQUEC1MAIN_AESYNC_START_ADDR+4),
                    read_cmos_eeprom_8(ctx, ZHUQUEC1MAIN_AESYNC_START_ADDR+5),
                    read_cmos_eeprom_8(ctx, ZHUQUEC1MAIN_AESYNC_START_ADDR+6),
                    read_cmos_eeprom_8(ctx, ZHUQUEC1MAIN_AESYNC_START_ADDR+7));
            LOG_INF("AESync write_Module_data Write end\n");
        } else {
            LOG_INF("Invalid Sensor id:0x%x write eeprom\n", pStereodata->uSensorId);
            return -1;
        }
    } else {
        LOG_INF("zhuquec1main write_Module_data pStereodata is null\n");
        return -1;
    }
    return ret;
}

static int zhuquec1main_set_eeprom_calibration(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
    int ret = ERROR_NONE;
    ret = write_Module_data(ctx, (ACDK_SENSOR_ENGMODE_STEREO_STRUCT *)(para));
    if (ret != ERROR_NONE) {
        LOG_INF("ret=%d\n", ret);
    }
	return 0;
}

static int zhuquec1main_get_eeprom_calibration(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	UINT16 *feature_data_16 = (UINT16 *) para;
	UINT32 *feature_return_para_32 = (UINT32 *) para;
	if(*len > CALI_DATA_MASTER_LENGTH)
		*len = CALI_DATA_MASTER_LENGTH;
	LOG_INF("feature_data mode: %d", *feature_data_16);
	switch (*feature_data_16) {
	case EEPROM_STEREODATA_MT_MAIN:
		read_zhuquec1main_eeprom_info(ctx, EEPROM_META_STEREO_MT_MAIN_DATA,
				(BYTE *)feature_return_para_32, *len);
		break;
	case EEPROM_STEREODATA_MW_MAIN:
		read_zhuquec1main_eeprom_info(ctx, EEPROM_META_STEREO_MW_MAIN_DATA,
				(BYTE *)feature_return_para_32, *len);
		break;
	case EEPROM_STEREODATA_MT_MAIN_105CM:
		read_zhuquec1main_eeprom_info(ctx, EEPROM_META_STEREO_MT_MAIN_DATA_105CM,
				(BYTE *)feature_return_para_32, *len);
		break;
	default:
		break;
	}
	return 0;
}

static bool read_cmos_eeprom_p8(struct subdrv_ctx *ctx, kal_uint16 addr,
                    BYTE *data, int size)
{
	if (adaptor_i2c_rd_p8(ctx->i2c_client, ZHUQUEC1MAIN_EEPROM_ADDR >> 1,
			addr, data, size) < 0) {
		return false;
	}
	return true;
}

static void read_otp_info(struct subdrv_ctx *ctx)
{
	DRV_LOGE(ctx, "zhuquec1main read_otp_info begin\n");
	read_cmos_eeprom_p8(ctx, 0, otp_data_checksum, ZHUQUEC1MAIN_EEPROM_MAX_OFFSET);
	DRV_LOGE(ctx, "zhuquec1main read_otp_info end\n");
}

static int zhuquec1main_get_otp_checksum_data(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u32 *feature_return_para_32 = (u32 *)para;
	u32 length = sizeof(otp_data_checksum);

	if(*len < sizeof(otp_data_checksum)) {
		length = *len;
	}

	DRV_LOGE(ctx, "get otp data length:0x%x", length);
	if (otp_data_checksum[0] == 0) {
		read_otp_info(ctx);
	} else {
		DRV_LOG(ctx, "otp data has already read");
	}

	memcpy(feature_return_para_32, (UINT32 *)otp_data_checksum, length);

	return 0;
}


static int get_sof_timeout(struct adaptor_ctx *ctx, const struct sensor_mode *mode)
{
	int timeout = 0;
	u64 tmp = 0;

	if (ctx->shutter_for_timeout > 0) {
		tmp = mode->linetime_in_ns * ctx->shutter_for_timeout;
		timeout = tmp / 1000;
	}
	if (ctx->framelength_for_timeout > 0) {
		tmp = mode->linetime_in_ns * ctx->framelength_for_timeout / 1000;
		timeout = (timeout < tmp) ? tmp : timeout;
	}
	if (ctx->subctx.current_fps > 0) {
		tmp = 10000000 / ctx->subctx.current_fps;
		timeout = (timeout < tmp) ? tmp : timeout;
	}
	if (timeout < 0)
		timeout = 0;

	DRV_LOG(ctx,
		"X! sof timeout value in us %llu|%llu|%llu|%d|%d\n",
		ctx->shutter_for_timeout,
		ctx->framelength_for_timeout,
		mode->linetime_in_ns,
		timeout,
		ctx->subctx.current_fps > 0 ? 10000000 / ctx->subctx.current_fps : 0);

	return timeout;
}

static void reset_group(struct subdrv_ctx *ctx)
{
	//reset group 0/1/2
	subdrv_ixc_wr_u8(ctx, 0x320d, 0x01);
	subdrv_ixc_wr_u8(ctx, 0x3208, 0x01);
	subdrv_ixc_wr_u8(ctx, 0x300a, 0x00);
	subdrv_ixc_wr_u8(ctx, 0x3208, 0x11);
	subdrv_ixc_wr_u8(ctx, 0x3208, 0x00);
	subdrv_ixc_wr_u8(ctx, 0x300a, 0x00);
	subdrv_ixc_wr_u8(ctx, 0x3208, 0x10);
	subdrv_ixc_wr_u8(ctx, 0x3208, 0x02);
	subdrv_ixc_wr_u8(ctx, 0x300a, 0x00);
	subdrv_ixc_wr_u8(ctx, 0x3208, 0x12);
	DRV_LOG(ctx, "Reset group\n");
}

static void streaming_ctrl(struct subdrv_ctx *ctx, bool enable)
{
	u64 stream_ctrl_delay_timing = 0;
	u64 stream_ctrl_delay = 0;
	struct adaptor_ctx *_adaptor_ctx = NULL;
	struct v4l2_subdev *sd = NULL;

	DRV_LOG(ctx, "E! enable:%u\n", enable);

	if (ctx->i2c_client)
		sd = i2c_get_clientdata(ctx->i2c_client);
	if (ctx->ixc_client.protocol)
		sd = adaptor_ixc_get_clientdata(&ctx->ixc_client);
	if (sd)
		_adaptor_ctx = to_ctx(sd);
	if (!_adaptor_ctx) {
		DRV_LOGE(ctx, "null _adaptor_ctx\n");
		return;
	}

	check_current_scenario_id_bound(ctx);
	if (ctx->s_ctx.aov_sensor_support && ctx->s_ctx.streaming_ctrl_imp) {
		if (ctx->s_ctx.s_streaming_control != NULL)
			ctx->s_ctx.s_streaming_control((void *) ctx, enable);
		else
			DRV_LOG_MUST(ctx,
				"please implement drive own streaming control!(sid:%u)\n",
				ctx->current_scenario_id);
		ctx->is_streaming = enable;
		DRV_LOG_MUST(ctx, "enable:%u\n", enable);
		return;
	}
	if (ctx->s_ctx.aov_sensor_support && ctx->s_ctx.mode[ctx->current_scenario_id].aov_mode) {
		DRV_LOG_MUST(ctx,
			"stream ctrl implement on scp side!(sid:%u)\n",
			ctx->current_scenario_id);
		ctx->is_streaming = enable;
		DRV_LOG_MUST(ctx, "enable:%u\n", enable);
		return;
	}

	if (enable) {
		/* MCSS low power mode update para */
		if (ctx->s_ctx.mcss_update_subdrv_para != NULL)
			ctx->s_ctx.mcss_update_subdrv_para((void *) ctx, ctx->current_scenario_id);
		/* MCSS register init */
		if (ctx->s_ctx.mcss_init != NULL)
			ctx->s_ctx.mcss_init((void *) ctx);

		set_dummy(ctx);
		subdrv_ixc_wr_u8(ctx, ctx->s_ctx.reg_addr_stream, 0x01);
		ctx->stream_ctrl_start_time = ktime_get_boottime_ns();
		ctx->stream_ctrl_start_time_mono = ktime_get_ns();
	} else {
		ctx->stream_ctrl_end_time = ktime_get_boottime_ns();
		if (ctx->s_ctx.custom_stream_ctrl_delay &&
			ctx->stream_ctrl_start_time && ctx->stream_ctrl_end_time) {
			stream_ctrl_delay_timing =
				(ctx->stream_ctrl_end_time - ctx->stream_ctrl_start_time) / 1000000;
			stream_ctrl_delay = (u64)get_sof_timeout(_adaptor_ctx, _adaptor_ctx->cur_mode) / 1000;
			DRV_LOG_MUST(ctx,
				"stream_ctrl_delay(sof)/stream_ctrl_delay_timing(end-start):%llums/%llums\n",
				stream_ctrl_delay,
				stream_ctrl_delay_timing);
			if (stream_ctrl_delay_timing < stream_ctrl_delay)
				mdelay(stream_ctrl_delay - stream_ctrl_delay_timing);
		}
		subdrv_ixc_wr_u8(ctx, ctx->s_ctx.reg_addr_stream, 0x00);
		reset_group(ctx);
		if (ctx->s_ctx.reg_addr_fast_mode && ctx->fast_mode_on) {
			ctx->fast_mode_on = FALSE;
			ctx->ref_sof_cnt = 0;
			DRV_LOG(ctx, "seamless_switch disabled.");
			set_i2c_buffer(ctx, ctx->s_ctx.reg_addr_fast_mode, 0x00);
			commit_i2c_buffer(ctx);
		}
		memset(ctx->exposure, 0, sizeof(ctx->exposure));
		memset(ctx->ana_gain, 0, sizeof(ctx->ana_gain));
		ctx->autoflicker_en = FALSE;
		ctx->extend_frame_length_en = 0;
		ctx->is_seamless = 0;
		if (ctx->s_ctx.chk_s_off_end)
			check_stream_off(ctx);
		ctx->stream_ctrl_start_time = 0;
		ctx->stream_ctrl_end_time = 0;
		ctx->stream_ctrl_start_time_mono = 0;

		ctx->mcss_init_info.enable_mcss = 0;
		if (ctx->s_ctx.mcss_init != NULL)
			ctx->s_ctx.mcss_init((void *) ctx); // disable MCSS
	}
	ctx->sof_no = 0;
	ctx->is_streaming = enable;
	DRV_LOG(ctx, "X! enable:%u\n", enable);
}



//static int zhuquec1main_streaming_resume(struct subdrv_ctx *ctx, u8 *para, u32 *len)
//{
//		DRV_LOGE(ctx, "SENSOR_FEATURE_SET_STREAMING_RESUME, shutter:%u\n", *(u32 *)para);
//		if (*(u32 *)para)
//			zhuquec1main_set_shutter_convert(ctx, (u32 *)para);
//		streaming_ctrl(ctx, true);
//		return 0;
//}

static int zhuquec1main_streaming_suspend(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
		DRV_LOGE(ctx, "streaming control para:%d\n", *para);
		streaming_ctrl(ctx, false);
		return 0;
}

static int zhuquec1main_check_sensor_id(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	get_imgsensor_id(ctx, (u32 *)para);
	return 0;
}

static int get_imgsensor_id(struct subdrv_ctx *ctx, u32 *sensor_id)
{
	u8 i = 0;
	u8 retry = 2;
	static bool first_read = KAL_TRUE;
	u32 addr_h = ctx->s_ctx.reg_addr_sensor_id.addr[0];
	u32 addr_l = ctx->s_ctx.reg_addr_sensor_id.addr[1];
	u32 addr_ll = ctx->s_ctx.reg_addr_sensor_id.addr[2];
	LOG_INF("rst delay = %d, func: %s, line: %d\n", pw_seq[1].delay, __FUNCTION__, __LINE__);
	while (ctx->s_ctx.i2c_addr_table[i] != 0xFF) {
		ctx->i2c_write_id = ctx->s_ctx.i2c_addr_table[i];
		do {
			*sensor_id = (subdrv_i2c_rd_u8(ctx, addr_h) << 8) |
				subdrv_i2c_rd_u8(ctx, addr_l);
			if (addr_ll)
				*sensor_id = ((*sensor_id) << 8) | subdrv_i2c_rd_u8(ctx, addr_ll);
			LOG_INF("i2c_write_id(0x%x) sensor_id(0x%x/0x%x)\n",
				ctx->i2c_write_id, *sensor_id, ctx->s_ctx.sensor_id);
			if (*sensor_id == ZHUQUEC1MAIN_IMGSENSOR_ID) {
				*sensor_id = ctx->s_ctx.sensor_id;
				get_imgsensor_id_from_dts(ctx, sensor_id);
				if (first_read) {
					read_eeprom_common_data(ctx, &oplus_eeprom_info, oplus_eeprom_addr_table);
					//read_unique_sensorid(ctx);
					first_read = KAL_FALSE;
					msg_buf = kmalloc(MAX_BURST_LEN, GFP_KERNEL);
					if(!msg_buf) {
						LOG_INF("boot stage, malloc msg_buf error");
					}
				}
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

static u16 get_gain2reg(u32 gain)
{
	return gain * (0x100) / BASEGAIN;
}

static int open(struct subdrv_ctx *ctx)
{
	u32 sensor_id = 0;
	u32 scenario_id = 0;
	/* get sensor id */
	if (get_imgsensor_id(ctx, &sensor_id) != ERROR_NONE)
		return ERROR_SENSOR_CONNECT_FAIL;

	// software reset
	subdrv_i2c_wr_regs_u8(ctx, zhuquec1main_soft_reset, ARRAY_SIZE(zhuquec1main_soft_reset));
	msleep(10);
	//sensor_init(ctx);
	zhuquec1main_i2c_burst_wr_regs_u8(ctx, ctx->s_ctx.init_setting_table, ctx->s_ctx.init_setting_len);
	// XTC
	if (ctx->s_ctx.s_cali != NULL) {
		ctx->s_ctx.s_cali((void*)ctx);
	}

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
	ctx->extend_frame_length_en = FALSE;
	ctx->is_seamless = 0;
	ctx->fast_mode_on = 0;
	ctx->sof_cnt = 0;
	ctx->ref_sof_cnt = 0;
	ctx->is_streaming = 0;

	return ERROR_NONE;
}

static void zhuquec1main_get_sensor_cali(void* arg)
{
	struct subdrv_ctx *ctx = (struct subdrv_ctx *)arg;
	u16 idx = 0;
	u8 support = FALSE;
	u8 *buf = NULL;
	u16 size = 0;
	u16 addr = 0;
	u8 write_id = 0;
	struct eeprom_info_struct *info = ctx->s_ctx.eeprom_info;

	/* Probe EEPROM device */
	if (!probe_eeprom(ctx))
		return;

	idx = ctx->eeprom_index;

	/* pdc data */
	support = info[idx].pdc_support;
	size = info[idx].pdc_size;
	addr = info[idx].addr_pdc;
	buf = info[idx].pdc_table;

	if (support && size > 0) {

		//xtc_is_valid = i2c_read_eeprom(ctx, OTP_XTC_VALID_ADDR);
		write_id = ctx->s_ctx.eeprom_info[idx].i2c_write_id;
		adaptor_i2c_rd_u8(ctx->i2c_client, write_id >> 1, OTP_XTC_VALID_ADDR, (u8 *)&xtc_is_valid);

		if(xtc_is_valid != OTP_XTC_IS_VALID_VAL) {
			DRV_LOGE(ctx, "xtc is invalid %d", xtc_is_valid);
			return;
		}

		if (info[idx].preload_pdc_table == NULL) {
			info[idx].preload_pdc_table = kmalloc(size, GFP_KERNEL);
			if (buf == NULL)
				i2c_multi_read_eeprom(ctx, addr, size, info[idx].preload_pdc_table);
			else
				memcpy(info[idx].preload_pdc_table, buf, size);
			DRV_LOG(ctx, "preload pdc data %u bytes", size);
		} else {
			DRV_LOG(ctx, "pdc data is already preloaded %u bytes", size);
		}
	}

	ctx->is_read_preload_eeprom = 1;
}

static void zhuquec1main_set_sensor_cali(void *arg)
{
	struct subdrv_ctx *ctx = (struct subdrv_ctx *)arg;
	u16 idx = 0;
	u8 support = FALSE;
	u8 *pbuf = NULL;
	u16 size = 0;
	u16 addr = 0;
	struct eeprom_info_struct *info = ctx->s_ctx.eeprom_info;

	if (!probe_eeprom(ctx)) {
		subdrv_i2c_wr_regs_u8(ctx, zhuquec1main_default_QPDC_setting, ARRAY_SIZE(zhuquec1main_default_QPDC_setting));
		DRV_LOG(ctx, "set default xtc\n");
		return;
	}

	idx = ctx->eeprom_index;
	/* XTC data */
	support = info[idx].pdc_support;
	if (support && (xtc_is_valid == OTP_XTC_IS_VALID_VAL)) {
		pbuf = info[idx].preload_pdc_table;
		size = info[idx].pdc_size;
		addr = info[idx].sensor_reg_addr_pdc;

		if(pbuf[0] == 0xFF) {
			subdrv_i2c_wr_regs_u8(ctx, zhuquec1main_default_QPDC_setting, ARRAY_SIZE(zhuquec1main_default_QPDC_setting));
			DRV_LOG(ctx, "error EEPROM xtc data, set default xtc\n");
			return;
		}

		subdrv_i2c_wr_seq_p8(ctx, XTC_SENSOR_ADDR_PART1, pbuf, XTC_SENSOR_LENGTH_PART1);  //part1
		DRV_LOG(ctx, "xtc part1 buf[0] = %d ,XTC_SENSOR_ADDR_PART1(0x%x)\n", pbuf[0], XTC_SENSOR_ADDR_PART1);
		pbuf += XTC_SENSOR_LENGTH_PART1;

		subdrv_i2c_wr_seq_p8(ctx, XTC_SENSOR_ADDR_PART2, pbuf, XTC_SENSOR_LENGTH_PART2);  //part2
		DRV_LOG(ctx, "xtc part2 buf[%d] = %d ,XTC_SENSOR_ADDR_PART2(0x%x)\n", XTC_SENSOR_LENGTH_PART1, pbuf[0], XTC_SENSOR_ADDR_PART2);
		pbuf += XTC_SENSOR_LENGTH_PART2;

		subdrv_i2c_wr_seq_p8(ctx, XTC_SENSOR_ADDR_PART3, pbuf, XTC_SENSOR_LENGTH_PART3);  //part3
		DRV_LOG(ctx, "xtc part3 buf[%d] = %d ,XTC_SENSOR_ADDR_PART3(0x%x)\n", XTC_SENSOR_LENGTH_PART1 + XTC_SENSOR_LENGTH_PART2, pbuf[0], XTC_SENSOR_ADDR_PART3);
	} else {
		subdrv_i2c_wr_regs_u8(ctx, zhuquec1main_default_QPDC_setting, ARRAY_SIZE(zhuquec1main_default_QPDC_setting));
		DRV_LOG(ctx, "set default xtc\n");
	}
}

//100ms
#define TEMP_READY_TIME_DELAY_NS (100 * 1000000)
#define INVALID_TEMP_VALUE (0x7FFF)

static int get_sensor_temperature(void *arg)
{
	struct subdrv_ctx *ctx = (struct subdrv_ctx *)arg;
	static u64 stream_ctrl_start_time = 0;
	static bool need_check_delay = true;
	u64 boot_time_ns = 0;
	u8 temperature = 0;
	int temperature_convert = 0;

	if (stream_ctrl_start_time != ctx->stream_ctrl_start_time) {
		stream_ctrl_start_time = ctx->stream_ctrl_start_time;
		need_check_delay = true;
		DRV_LOG(ctx, "stream on(%llu), first read\n", stream_ctrl_start_time);
	}

	if (need_check_delay == true) {
		boot_time_ns = ktime_get_boottime_ns();
		if ((boot_time_ns - stream_ctrl_start_time) > TEMP_READY_TIME_DELAY_NS) {
			need_check_delay = false;
		} else {
			temperature_convert = INVALID_TEMP_VALUE;
			DRV_LOG(ctx, "INVALID_TEMP_VALUE(%d)\n", temperature_convert);
			return temperature_convert;
		}
	}

	if (ctx->s_ctx.reg_addr_temp_read) {
		subdrv_i2c_wr_u8(ctx, ctx->s_ctx.reg_addr_temp_en, 0x01); //trigger temperature calculation 0x4D12
		temperature = subdrv_i2c_rd_u8(ctx, ctx->s_ctx.reg_addr_temp_read);
		if (temperature < 0xC0) {
			temperature_convert = temperature;
		} else {
			temperature_convert = ((char)temperature) | 0xFFFFF00;
		}
	}
	DRV_LOG(ctx, "reg_val:0x%x, temperature: %d degrees\n", temperature, temperature_convert);

	return temperature_convert;
}

static void set_group_hold(void *arg, u8 en)
{
	struct subdrv_ctx *ctx = (struct subdrv_ctx *)arg;
	if (en) {
		set_i2c_buffer(ctx, 0x3208, 0x00);
	} else {
		set_i2c_buffer(ctx, 0x3208, 0x10);
		set_i2c_buffer(ctx, 0x3208, 0xa0);
	}
}

void zhuquec1main_get_min_shutter_by_scenario(struct subdrv_ctx *ctx,
		enum SENSOR_SCENARIO_ID_ENUM scenario_id,
		u64 *min_shutter, u64 *exposure_step)
{
	u32 exp_cnt = 0;
	exp_cnt = ctx->s_ctx.mode[scenario_id].exp_cnt;
	check_current_scenario_id_bound(ctx);
	LOG_INF("sensor_mode_num[%d]", ctx->s_ctx.sensor_mode_num);
	if (scenario_id < ctx->s_ctx.sensor_mode_num) {
		switch (ctx->s_ctx.mode[scenario_id].hdr_mode) {
			case HDR_RAW_STAGGER:
				*exposure_step = ctx->s_ctx.exposure_step * exp_cnt;
				*min_shutter = ctx->s_ctx.exposure_min * exp_cnt;
				break;
			case HDR_NONE:
				if (ctx->s_ctx.mode[scenario_id].coarse_integ_step &&
					ctx->s_ctx.mode[scenario_id].min_exposure_line) {
					*exposure_step = ctx->s_ctx.mode[scenario_id].coarse_integ_step;
					*min_shutter = ctx->s_ctx.mode[scenario_id].min_exposure_line;
				} else {
					*exposure_step = ctx->s_ctx.exposure_step;
					*min_shutter = ctx->s_ctx.exposure_min;
				}
				break;
			default:
				*exposure_step = ctx->s_ctx.exposure_step;
				*min_shutter = ctx->s_ctx.exposure_min;
				break;
		}
	} else {
		DRV_LOG(ctx, "over sensor_mode_num[%d], use default", ctx->s_ctx.sensor_mode_num);
		*exposure_step = ctx->s_ctx.exposure_step;
		*min_shutter = ctx->s_ctx.exposure_min;
	}
	DRV_LOG(ctx, "scenario_id[%d] exposure_step[%llu] min_shutter[%llu]\n", scenario_id, *exposure_step, *min_shutter);
}

int zhuquec1main_get_min_shutter_by_scenario_adapter(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u64 *feature_data = (u64 *) para;
	zhuquec1main_get_min_shutter_by_scenario(ctx,
		(enum SENSOR_SCENARIO_ID_ENUM)*(feature_data),
		feature_data + 1, feature_data + 2);
	return 0;
}

static void zhuquec1main_get_hgc_from_settings(struct subdrv_ctx *ctx)
{
	u32 i = 0;
	bool hgc_flag1 = false;
	bool hgc_flag2 = false;

	kal_uint16 * list = ctx->s_ctx.mode[ctx->current_scenario_id].mode_setting_table;
	u32 length = ctx->s_ctx.mode[ctx->current_scenario_id].mode_setting_len;

	for (i = 0; i < length; i += 2) {
		if(list[i] == 0x3506 && hgc_flag1 == false) {
			hgc_value[0] = list[i + 1];
			DRV_LOG(ctx, "get hgc[0]= 0x%02x", hgc_value[0]);
			hgc_flag1 = true;
		}
		if(list[i] == 0x3546 && hgc_flag2 == false) {
			hgc_value[1] = list[i + 1];
			DRV_LOG(ctx, "get hgc[1]= 0x%02x", hgc_value[1]);
			hgc_flag2 = true;
		}
		if(hgc_flag1 == true && hgc_flag2 == true) {
			break;
		}
	}

	switch(ctx->current_scenario_id) {
		case SENSOR_SCENARIO_ID_CUSTOM3  :
		case SENSOR_SCENARIO_ID_CUSTOM5  :
		case SENSOR_SCENARIO_ID_CUSTOM8  :
		case SENSOR_SCENARIO_ID_CUSTOM9  :
		case SENSOR_SCENARIO_ID_CUSTOM10 :
		case SENSOR_SCENARIO_ID_CUSTOM11 :
		case SENSOR_SCENARIO_ID_CUSTOM13 :
		case SENSOR_SCENARIO_ID_CUSTOM14 :
		case SENSOR_SCENARIO_ID_CUSTOM15 :
		case SENSOR_SCENARIO_ID_CUSTOM16 :
			scg_flag = false;
			break;
		default :
			scg_flag = true;
			break;
	}
	DRV_LOG(ctx, "current_scenario_id(%d),  scg_flag(%d)", ctx->current_scenario_id, scg_flag);
}

static void zhuquec1main_set_hgc_to_buf(struct subdrv_ctx *ctx, u32 reg_gain, u32 reg_gain1)
{
	if (scg_flag == true) {

		if (reg_gain != 0) {  //1dol
			if(reg_gain >= 4 * 0x100){  //  >= 4x
				hgc_value[0] |= 0x02;
			} else {  // < 4x
				hgc_value[0] &= 0xFD;
			}
			DRV_LOG(ctx, "reg_gain(0x%x)  changed 1dol hgc_value(0x%x)\n", reg_gain, hgc_value[0]);
			set_i2c_buffer(ctx, 0x3506, hgc_value[0]);
		}

		if (reg_gain1 != 0) {  //2dol

			if(reg_gain1 >= 4 * 0x100){  //  >= 4x
				hgc_value[1] |= 0x02;
			} else {  // < 4x
				hgc_value[1] &= 0xFD;
			}
			DRV_LOG(ctx, "reg_gain1 (0x%x) changed 2dol hgc_value(0x%x)\n", reg_gain1, hgc_value[1]);
			set_i2c_buffer(ctx, 0x3546, hgc_value[1]);
		}
	}
}

static void zhuquec1main_send_diff_settings(struct subdrv_ctx *ctx, kal_uint16 * target_list, u32 target_length, kal_uint16 * base_list, u32 base_length)
{
	u32 target_loop = 0;
	u32 base_loop = 0;
	u32 i = 0;
	u32 min_length = 0;

	if(target_length != base_length) {
		LOG_INF("[test] error target_length(%d)  base_length(%d)", target_length, base_length);
		return ;
	}
	min_length = min(target_length, base_length);

	for (i = 0; i < min_length; i += 2) {
		if (target_list[target_loop] == base_list[base_loop]) { //addr equal
			if(target_list[target_loop + 1] == base_list[base_loop + 1]) { //data equal
				DRV_LOG(ctx, "[test] addr data equ skip");
			} else { //addr equal, data not equal
				DRV_LOG(ctx, "diff  addr data(0x%04x 0x%02x)", target_list[target_loop], target_list[target_loop + 1]);
				set_i2c_buffer(ctx, target_list[target_loop], target_list[target_loop + 1]);
			}
		} else {
			LOG_INF("error  addr data(0x%04x 0x%02x)", target_list[target_loop], target_list[target_loop + 1]);
			return;
		}

		target_loop += 2;
		base_loop   += 2;
	}
}

static int zhuquec1main_seamless_switch(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	enum SENSOR_SCENARIO_ID_ENUM scenario_id;
	struct mtk_hdr_ae *ae_ctrl = NULL;
	u64 *feature_data = (u64 *)para;
	enum SENSOR_SCENARIO_ID_ENUM pre_seamless_scenario_id;
	//u32 frame_length_in_lut[IMGSENSOR_STAGGER_EXPOSURE_CNT] = {0};
	u32 exp_cnt = 0;
	int i;

	if (feature_data == NULL) {
		DRV_LOGE(ctx, "input scenario is null!");
		return ERROR_INVALID_SCENARIO_ID;
	}
	scenario_id = *feature_data;
	if ((feature_data + 1) != NULL)
		ae_ctrl = (struct mtk_hdr_ae *)((uintptr_t)(*(feature_data + 1)));
	else
		DRV_LOGE(ctx, "no ae_ctrl input");

	check_current_scenario_id_bound(ctx);
	DRV_LOG(ctx, "E: set seamless switch %u %u\n", ctx->current_scenario_id, scenario_id);

	if (scenario_id >= ctx->s_ctx.sensor_mode_num) {
		DRV_LOGE(ctx, "invalid sid:%u, mode_num:%u\n",
			scenario_id, ctx->s_ctx.sensor_mode_num);
		return ERROR_INVALID_SCENARIO_ID;
	}
	if (ctx->s_ctx.mode[scenario_id].seamless_switch_group == 0 ||
		ctx->s_ctx.mode[scenario_id].seamless_switch_group !=
			ctx->s_ctx.mode[ctx->current_scenario_id].seamless_switch_group) {
		DRV_LOGE(ctx, "seamless_switch not supported\n");
		return ERROR_INVALID_SCENARIO_ID;
	}

	exp_cnt = ctx->s_ctx.mode[scenario_id].exp_cnt;
	ctx->is_seamless = TRUE;
	pre_seamless_scenario_id = ctx->current_scenario_id;
	update_mode_info(ctx, scenario_id);

	commit_i2c_buffer(ctx);

//seamless begin setting
	switch (ctx->s_ctx.mode[pre_seamless_scenario_id].hdr_mode) {
	case HDR_RAW_STAGGER:
		DRV_LOGE(ctx, "seamless_switch stagger to other start\n");
		for(i = 0 ; i < ARRAY_SIZE(zhuquec1main_stg_to_other_begin); i += 2) {
			 set_i2c_buffer(ctx, zhuquec1main_stg_to_other_begin[i], zhuquec1main_stg_to_other_begin[i + 1]);
		}
		break;
	default:
		DRV_LOGE(ctx, "seamless_switch linear to other start\n");
		for(i = 0 ; i < ARRAY_SIZE(zhuquec1main_linear_to_other_begin); i += 2) {
			 set_i2c_buffer(ctx, zhuquec1main_linear_to_other_begin[i], zhuquec1main_linear_to_other_begin[i + 1]);
		}
		break;
	}


//	i2c_table_write(ctx,
//		ctx->s_ctx.mode[scenario_id].seamless_switch_mode_setting_table,
//		ctx->s_ctx.mode[scenario_id].seamless_switch_mode_setting_len);
	zhuquec1main_send_diff_settings(ctx, ctx->s_ctx.mode[scenario_id].mode_setting_table, ctx->s_ctx.mode[scenario_id].mode_setting_len,
		ctx->s_ctx.mode[pre_seamless_scenario_id].mode_setting_table, ctx->s_ctx.mode[pre_seamless_scenario_id].mode_setting_len);

	zhuquec1main_get_hgc_from_settings(ctx);

	if (ae_ctrl) {
		switch (ctx->s_ctx.mode[scenario_id].hdr_mode) {
		case HDR_RAW_STAGGER:
			zhuquec1main_set_multi_shutter_frame_length(ctx, (u64*)&ae_ctrl->exposure, exp_cnt, 0);
			zhuquec1main_set_multi_gain(ctx, (u32 *)&ae_ctrl->gain, exp_cnt);
			break;
		default:
			zhuquec1main_set_shutter_convert(ctx, ae_ctrl->exposure.le_exposure);
			zhuquec1main_set_gain_convert(ctx, ae_ctrl->gain.le_gain);
			break;
		}
	}


//seamless end setting
	switch (ctx->s_ctx.mode[pre_seamless_scenario_id].hdr_mode) {
	case HDR_RAW_STAGGER:
		DRV_LOGE(ctx, "seamless_switch stagger to other end\n");
		for(i = 0 ; i < ARRAY_SIZE(zhuquec1main_stg_to_other_end); i += 2) {
			set_i2c_buffer(ctx, zhuquec1main_stg_to_other_end[i], zhuquec1main_stg_to_other_end[i + 1]);
		}
		break;
	default:
		DRV_LOGE(ctx, "seamless_switch linear to other  end\n");
		for(i = 0 ; i < ARRAY_SIZE(zhuquec1main_linear_to_other_end); i += 2) {
			set_i2c_buffer(ctx, zhuquec1main_linear_to_other_end[i], zhuquec1main_linear_to_other_end[i + 1]);
		}
		break;
	}

	DRV_LOG(ctx, "write seamless switch setting done\n");

	commit_i2c_buffer(ctx);

	ctx->ref_sof_cnt = ctx->sof_cnt;
	ctx->is_seamless = FALSE;
	DRV_LOG(ctx, "X: set seamless switch done\n");
	return ERROR_NONE;
}

static int zhuquec1main_set_test_pattern(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u32 mode = *((u32 *)para);
	if (mode) {
		DRV_LOG(ctx, "mode(%u->%u)\n", ctx->test_pattern, mode);
		LOG_INF("mode(%u->%u)\n", ctx->test_pattern, mode);
	/* 1:Solid Color 2:Color Bar 5:Black */
		switch (mode) {
		case 5:
			subdrv_i2c_wr_u8(ctx, 0x50C1, 0x01);
			subdrv_i2c_wr_u8(ctx, 0x50C2, 0x04);
			subdrv_i2c_wr_u8(ctx, 0x53C1, 0x01);
			subdrv_i2c_wr_u8(ctx, 0x53C2, 0x04);
			subdrv_i2c_wr_u8(ctx, 0x56C1, 0x01);
			subdrv_i2c_wr_u8(ctx, 0x56C2, 0x04);
			break;
		default:
			subdrv_i2c_wr_u8(ctx, 0x50C1, mode);
			subdrv_i2c_wr_u8(ctx, 0x53C1, mode);
			subdrv_i2c_wr_u8(ctx, 0x56C1, mode);
			break;
		}
	} else if (ctx->test_pattern) {
		LOG_INF("mode(%u->%u)\n", ctx->test_pattern, mode);
		subdrv_i2c_wr_u8(ctx, 0x50C1, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x50C2, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x53C1, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x53C2, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x56C1, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x56C2, 0x00);
	}
	ctx->test_pattern = mode;
	return 0;
}

static int init_ctx(struct subdrv_ctx *ctx,	struct i2c_client *i2c_client, u8 i2c_write_id)
{
	memcpy(&(ctx->s_ctx), &static_ctx, sizeof(struct subdrv_static_ctx));
	subdrv_ctx_init(ctx);
	//hw_init_time
	for (int scenario_id = 0; scenario_id < ctx->s_ctx.sensor_mode_num; scenario_id++){
		 ctx->hw_time_info[scenario_id].init_time_ns = 3 * 1000000;
	}
	ctx->i2c_client = i2c_client;
	ctx->i2c_write_id = i2c_write_id;
	return 0;
}

static int vsync_notify(struct subdrv_ctx *ctx,	unsigned int sof_cnt)
{
	DRV_LOG(ctx, "sof_cnt(%u) ctx->ref_sof_cnt(%u) ctx->fast_mode_on(%d)",
		sof_cnt, ctx->ref_sof_cnt, ctx->fast_mode_on);
	ctx->sof_cnt = sof_cnt;

	return 0;
}

static void zhuquec1main_set_multi_shutter_frame_length(struct subdrv_ctx *ctx, u64 *shutters, u16 exp_cnt, u16 frame_length)
{
	int i = 0;
	int fine_integ_line = 0;
	u16 last_exp_cnt = 1;
	u32 calc_fl[4] = {0};
//	int readout_diff = 0;
	bool gph = !ctx->is_seamless && (ctx->s_ctx.s_gph != NULL);
	u32 rg_shutters[3] = {0};
	u32 cit_step = 0;
	u32 fll = 0, fll_temp = 0, s_fll;
	u32 value1;

	if(exp_cnt == 1) {  //force to 1exp func
		zhuquec1main_set_shutter_frame_length_convert(ctx, shutters[0], frame_length);
		return ;
	}

	fll = frame_length ? frame_length : ctx->min_frame_length;
	if (exp_cnt > ARRAY_SIZE(ctx->exposure)) {
		DRV_LOGE(ctx, "invalid exp_cnt:%u>%lu\n", exp_cnt, ARRAY_SIZE(ctx->exposure));
		exp_cnt = ARRAY_SIZE(ctx->exposure);
	}
	check_current_scenario_id_bound(ctx);

	/* check boundary of shutter */
	for (i = 1; i < ARRAY_SIZE(ctx->exposure); i++)
		last_exp_cnt += ctx->exposure[i] ? 1 : 0;
	fine_integ_line = ctx->s_ctx.mode[ctx->current_scenario_id].fine_integ_line;
	cit_step = ctx->s_ctx.mode[ctx->current_scenario_id].coarse_integ_step;
	for (i = 0; i < exp_cnt; i++) {
		shutters[i] = FINE_INTEG_CONVERT(shutters[i], fine_integ_line);
		shutters[i] = max_t(u64, shutters[i],
			(u64)ctx->s_ctx.mode[ctx->current_scenario_id].multi_exposure_shutter_range[i].min);
		shutters[i] = min_t(u64, shutters[i],
			(u64)ctx->s_ctx.mode[ctx->current_scenario_id].multi_exposure_shutter_range[i].max);
		if (cit_step)
			shutters[i] = roundup(shutters[i], cit_step);
	}

	if (exp_cnt == 2) { //2dol
		if( ctx->is_seamless == TRUE || is_first_exp) {
			ctx->exposure[1] = shutters[1];
			is_first_exp = false;
			zhuquec1main_get_exp_offset_from_settings(ctx);
			LOG_INF("seamless change to Exp_M(n+1), exp_offset= 0x%2x", exp_offset);
		}

		//4C2Plus  : Exp_L(n+2) + Exp_M(n+1)< VTS(n+1) – 32 - (R3830 + 1)*2
		//Full crop: Exp_L(n+2) + Exp_M(n+1)< VTS(n+1) – 49 -  R3830
		//fix short exp : Exp_M(n+1) = (R3846 R3847)*2
		if(ctx->current_scenario_id == SENSOR_SCENARIO_ID_CUSTOM15) {  //izoom 2dol qbc
			value1 = exp_offset + 49 + fix_short_exp;
		} else {  //2dol
			value1 = 2 * (exp_offset + 1) + 32 + fix_short_exp * 2;
		}
		value1 = roundup(value1, 2);

		calc_fl[0] = shutters[0] + value1 * 2 + 4;
		//LOG_INF("error  change VTS(n+1) calc_fl[0] (%u)   value1(%d)", calc_fl[0], value1);

		// Exp_M(n+2) >= Exp_M(n+1) + Img Height - VTS(n+1)    // VTS(n+1)  >= Exp_M(n+1) + Img Height - Exp_M(n+2)
		// calc_fl[1] = ctx->exposure[1] + ctx->s_ctx.mode[ctx->current_scenario_id].imgsensor_winsize_info.h2_tg_size * 2 - shutters[1];
		// LOG_INF("error  change VTS(n+1) calc_fl[1] (%u)", calc_fl[1]);

		//4C2Plus   Non-overlap adds:  VTS(n+1) > Exp_M(n+1) + Image Height + 152
		//Full crop Non-overlap adds:  VTS(n+1) > Exp_M(n+1) + Image Height + 200

		// if(ctx->current_scenario_id == SENSOR_SCENARIO_ID_CUSTOM15) {  //izoom 2dol qbc
		// 	value2 = exp_offset + 105;
		// } else {  //2dol
		// 	value2 = 2 * (exp_offset + 1) + 56;
		// }
		// calc_fl[2] = ctx->exposure[1] + ctx->s_ctx.mode[ctx->current_scenario_id].imgsensor_winsize_info.h2_tg_size * 2  + value2 * 2 + 4;

		// LOG_INF("error  change VTS(n+1) calc_fl[2] (%u)", calc_fl[2]);
	}

	/* check boundary of framelength */
	/* - (1) previous se + previous me + current le */ //N+1 framelength
	//calc_fl[0] = (u32) shutters[0];
	//for (i = 1; i < last_exp_cnt; i++)
	//	calc_fl[0] += ctx->exposure[i];
	//calc_fl[0] += ctx->s_ctx.mode[ctx->current_scenario_id].exposure_margin*exp_cnt*exp_cnt;

	///* - (2) current se + current me + current le */
	//calc_fl[1] = (u32) shutters[0];
	//for (i = 1; i < exp_cnt; i++)
	//	calc_fl[1] += (u32) shutters[i];
	//calc_fl[1] += ctx->s_ctx.mode[ctx->current_scenario_id].exposure_margin*exp_cnt*exp_cnt;

	///* - (3) readout time cannot be overlapped */
	//calc_fl[2] =
	//	(ctx->s_ctx.mode[ctx->current_scenario_id].readout_length +
	//	ctx->s_ctx.mode[ctx->current_scenario_id].read_margin);
	//if (last_exp_cnt == exp_cnt)
	//	for (i = 1; i < exp_cnt; i++) {
	//		readout_diff = ctx->exposure[i] - (u32) shutters[i];
	//		calc_fl[2] += readout_diff > 0 ? readout_diff : 0;
	//	}
	///* - (4) For DOL (non-FDOL), N-th frame SE and N+1-th frame LE readout cannot be overlapped */
	//if ((ctx->s_ctx.hdr_type & HDR_SUPPORT_STAGGER_DOL) &&
	//	ctx->s_ctx.mode[ctx->current_scenario_id].hdr_mode == HDR_RAW_STAGGER) {
	//	for (i = 1; i < last_exp_cnt; i++)
	//		calc_fl[3] += ctx->exposure[i];
	//	calc_fl[3] += ctx->s_ctx.mode[ctx->current_scenario_id].exposure_margin*exp_cnt*(exp_cnt-1);
	//	calc_fl[3] += ctx->readout_length + ctx->min_vblanking_line;
	//	DRV_LOG(ctx,
	//		"calc_fl[3]: %u, pre-LE/ME/SE (%u/%u/%u), cur-LE/ME/SE (%llu/%llu/%llu), readout_length:%u, min_vblanking_line:%u\n",
	//		calc_fl[3],
	//		ctx->exposure[0], ctx->exposure[1], ctx->exposure[2],
	//		shutters[0], shutters[1], shutters[2],
	//		ctx->readout_length,
	//		ctx->min_vblanking_line);
	//}

	for (i = 0; i < ARRAY_SIZE(calc_fl); i++)
		fll = max(fll, calc_fl[i]);
	fll =	max(fll, ctx->min_frame_length);
	fll =	min(fll, ctx->s_ctx.frame_length_max);
	/* restore shutter */
	memset(ctx->exposure, 0, sizeof(ctx->exposure));
	for (i = 0; i < exp_cnt; i++)
		ctx->exposure[i] = (u32) shutters[i];
	/* group hold start */
	if (gph)
		ctx->s_ctx.s_gph((void *)ctx, 1);
	/* enable auto extend */
	if (ctx->s_ctx.reg_addr_auto_extend)
		set_i2c_buffer(ctx, ctx->s_ctx.reg_addr_auto_extend, 0x01);

	if (ctx->s_ctx.mode[ctx->current_scenario_id].sw_fl_delay) {
		fll_temp = ctx->frame_length_next;
		ctx->frame_length_next = fll;
		s_fll = calc_fl[0];
		for (i = 1; i < ARRAY_SIZE(calc_fl); i++)
			s_fll = max(s_fll, calc_fl[i]);
		if (s_fll > frame_length) {
			fll = s_fll;
			fll = max(fll, ctx->min_frame_length);
			fll = min(fll, ctx->s_ctx.frame_length_max);
			if(fll<fll_temp)
				fll = fll_temp;
		} else {
			if (s_fll > fll_temp)
				fll = s_fll;
			else
				fll = fll_temp;
		}
		DRV_LOG(ctx, "fll:%u, s_fll:%u, fll_temp:%u, frame_length:%u\n",
			fll, s_fll, fll_temp, frame_length);
	}
	ctx->frame_length = fll;
	/* write framelength */
	if (set_auto_flicker(ctx, 0) || frame_length || !ctx->s_ctx.reg_addr_auto_extend)
		write_frame_length(ctx, ctx->frame_length);
	else if (ctx->s_ctx.reg_addr_auto_extend)
		write_frame_length(ctx, ctx->min_frame_length);
	/* write shutter */
	switch (exp_cnt) {
	case 1:
		rg_shutters[0] = (u32) shutters[0] / exp_cnt;

		if( ctx->s_ctx.mode[ctx->current_scenario_id].hdr_mode == HDR_RAW_DCG_RAW) {  //DCG  EXP_L = EXP_M
			rg_shutters[2] = (u32) shutters[0] / exp_cnt;
		}
		break;
	case 2:
		rg_shutters[0] = (u32) shutters[0] / exp_cnt;
		rg_shutters[2] = (u32) shutters[1] / exp_cnt;
		break;
	case 3:
		rg_shutters[0] = (u32) shutters[0] / exp_cnt;
		rg_shutters[1] = (u32) shutters[1] / exp_cnt;
		rg_shutters[2] = (u32) shutters[2] / exp_cnt;
		break;
	default:
		break;
	}
	if (ctx->s_ctx.reg_addr_exposure_lshift != PARAM_UNDEFINED) {
		set_i2c_buffer(ctx, ctx->s_ctx.reg_addr_exposure_lshift, 0);
		ctx->l_shift = 0;
	}
	for (i = 0; i < 3; i++) {
		if (rg_shutters[i]) {
			if (ctx->s_ctx.reg_addr_exposure[i].addr[2]) {
				set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[i].addr[0],
					(rg_shutters[i] >> 16) & 0xFF);
				set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[i].addr[1],
					(rg_shutters[i] >> 8) & 0xFF);
				set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[i].addr[2],
					rg_shutters[i] & 0xFF);
			} else {
				set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[i].addr[0],
					(rg_shutters[i] >> 8) & 0xFF);
				set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[i].addr[1],
					rg_shutters[i] & 0xFF);
			}
		}
	}
	DRV_LOG(ctx, "exp[0x%x/0x%x/0x%x], fll(input/output):%u/%u, flick_en:%d\n",
		rg_shutters[0], rg_shutters[1], rg_shutters[2],
		frame_length, ctx->frame_length, ctx->autoflicker_en);
	if (!ctx->ae_ctrl_gph_en) {
		if (gph)
			ctx->s_ctx.s_gph((void *)ctx, 0);
		commit_i2c_buffer(ctx);
	}
	/* group hold end */
}

static int zhuquec1main_set_multi_shutter_frame_length_ctrl(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u64* feature_data = (u64*)para;

	zhuquec1main_set_multi_shutter_frame_length(ctx, (u64 *)(*feature_data),
		(u64) (*(feature_data + 1)), (u64) (*(feature_data + 2)));
	return 0;
}

static void zhuquec1main_set_hdr_tri_shutter(struct subdrv_ctx *ctx, u64 *shutters, u16 exp_cnt)
{
	int i = 0;
	u64 values[3] = {0};

	if (shutters != NULL) {
		for (i = 0; i < 3; i++)
			values[i] = (u64) *(shutters + i);
	}
	zhuquec1main_set_multi_shutter_frame_length(ctx, values, exp_cnt, 0);
}

static int zhuquec1main_set_hdr_tri_shutter2(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u64* feature_data = (u64*)para;

	zhuquec1main_set_hdr_tri_shutter(ctx, feature_data, 2);
	return 0;
}

static int zhuquec1main_set_hdr_tri_shutter3(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u64* feature_data = (u64*)para;

	zhuquec1main_set_hdr_tri_shutter(ctx, feature_data, 3);
	return 0;
}

static void zhuquec1main_set_shutter_frame_length_convert(struct subdrv_ctx *ctx, u64 shutter, u32 frame_length)
{
	int fine_integ_line = 0;
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
	ctx->frame_length = max((u32)shutter + ctx->s_ctx.mode[ctx->current_scenario_id].exposure_margin, ctx->frame_length);
	ctx->frame_length = min(ctx->frame_length, ctx->s_ctx.frame_length_max);
	ctx->frame_length = max(ctx->frame_length, ctx->min_frame_length);
	/* restore shutter */
	memset(ctx->exposure, 0, sizeof(ctx->exposure));
	ctx->exposure[0] = (u32) shutter;
	/* group hold start */
	if (gph)
		ctx->s_ctx.s_gph((void *)ctx, 1);
	/* enable auto extend */
	if (ctx->s_ctx.reg_addr_auto_extend)
		set_i2c_buffer(ctx, ctx->s_ctx.reg_addr_auto_extend, 0x01);
	/* write framelength */
	if (set_auto_flicker(ctx, 0) || frame_length || !ctx->s_ctx.reg_addr_auto_extend)
		write_frame_length(ctx, ctx->frame_length);
	else if (ctx->s_ctx.reg_addr_auto_extend)
		write_frame_length(ctx, ctx->min_frame_length);
	/* write shutter */
	//set_long_exposure(ctx);
	if (ctx->s_ctx.reg_addr_exposure[0].addr[2]) {
		set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[0].addr[0],
			(ctx->exposure[0] >> 16) & 0xFF);
		set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[0].addr[1],
			(ctx->exposure[0] >> 8) & 0xFF);
		set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[0].addr[2],
			ctx->exposure[0] & 0xFF);

		if(ctx->s_ctx.mode[ctx->current_scenario_id].hdr_mode == HDR_RAW_DCG_RAW) {  //DCG  EXP_L = EXP_M
			set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[2].addr[0],
				(ctx->exposure[0] >> 16) & 0xFF);
			set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[2].addr[1],
				(ctx->exposure[0] >> 8) & 0xFF);
			set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[2].addr[2],
				ctx->exposure[0] & 0xFF);
		}
	} else {
		set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[0].addr[0],
			(ctx->exposure[0] >> 8) & 0xFF);
		set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_exposure[0].addr[1],
			ctx->exposure[0] & 0xFF);
	}
	DRV_LOG(ctx, "exp[0x%x], fll(input/output):%u/%u, flick_en:%d\n",
		ctx->exposure[0], frame_length, ctx->frame_length, ctx->autoflicker_en);
	if (!ctx->ae_ctrl_gph_en) {
		if (gph)
			ctx->s_ctx.s_gph((void *)ctx, 0);
		commit_i2c_buffer(ctx);
	}
	/* group hold end */
}

static int zhuquec1main_set_shutter_frame_length(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	zhuquec1main_set_shutter_frame_length_convert(ctx, ((u64*)para)[0], ((u64*)para)[1]);
	return 0;
}

static void zhuquec1main_set_shutter_convert(struct subdrv_ctx *ctx, u64 shutter)
{
    zhuquec1main_set_shutter_frame_length_convert(ctx, shutter, 0);
}

static int zhuquec1main_set_shutter(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	zhuquec1main_set_shutter_frame_length_convert(ctx, ((u64*)para)[0], 0);
	return 0;
}
void zhuquec1main_set_dummy(struct subdrv_ctx *ctx)
{
}

static int zhuquec1main_set_max_framerate_by_scenario(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	enum SENSOR_SCENARIO_ID_ENUM scenario_id = (u32)((u64*)para)[0];
	u32 framerate = (u32)((u64*)para)[1];
	u32 frame_length;
	u32 frame_length_step;
	u32 frame_length_min;
	u32 frame_length_max;

	if (scenario_id >= ctx->s_ctx.sensor_mode_num) {
		DRV_LOGE(ctx, "invalid sid:%u, mode_num:%u\n",
			scenario_id, ctx->s_ctx.sensor_mode_num);
		scenario_id = SENSOR_SCENARIO_ID_NORMAL_PREVIEW;
	}
	if (!framerate) {
		DRV_LOGE(ctx, "framerate (%u) is invalid\n", framerate);
		return 0;
	}
	if (!ctx->s_ctx.mode[scenario_id].linelength) {
		DRV_LOGE(ctx, "linelength (%u) is invalid\n",
			ctx->s_ctx.mode[scenario_id].linelength);
		return 0;
	}
	if (framerate > ctx->s_ctx.mode[scenario_id].max_framerate) {
		DRV_LOGE(ctx, "framerate (%u) is greater than max_framerate (%u)\n",
			framerate, ctx->s_ctx.mode[scenario_id].max_framerate);
		framerate = ctx->s_ctx.mode[scenario_id].max_framerate;
	}
	if (ctx->s_ctx.mode[scenario_id].hdr_mode == HDR_RAW_LBMF) {
		set_max_framerate_in_lut_by_scenario(ctx, scenario_id, framerate);
		return 0;
	}

	///* MCSS low power mode update para */
	//if (ctx->s_ctx.mcss_update_subdrv_para != NULL) {
	//	set_max_framerate_mcss_by_scenario(ctx, scenario_id, framerate);
	//	return 0;
	//}
	frame_length_step = ctx->s_ctx.mode[scenario_id].framelength_step;
	/* set on the step of frame length */
	frame_length = ctx->s_ctx.mode[scenario_id].pclk / framerate * 10
		/ ctx->s_ctx.mode[scenario_id].linelength;
	frame_length = frame_length_step ?
		(frame_length - (frame_length % frame_length_step)) : frame_length;
	frame_length_min = ctx->s_ctx.mode[scenario_id].framelength;
	frame_length_max = ctx->s_ctx.frame_length_max;
	frame_length_max = frame_length_step ?
		(frame_length_max - (frame_length_max % frame_length_step)) : frame_length_max;


	/* set in the range of frame length */
	ctx->frame_length = max(frame_length, frame_length_min);
	ctx->frame_length = min(ctx->frame_length, frame_length_max);
	ctx->frame_length = frame_length_step ?
		roundup(ctx->frame_length,frame_length_step) : ctx->frame_length;

	/* set default frame length if given default framerate */
	if (framerate == ctx->s_ctx.mode[scenario_id].max_framerate)
		ctx->frame_length = ctx->s_ctx.mode[scenario_id].framelength;

	ctx->current_fps = ctx->s_ctx.mode[scenario_id].pclk /
						ctx->frame_length * 10 /
						ctx->s_ctx.mode[scenario_id].linelength;
	ctx->min_frame_length = ctx->frame_length;
	DRV_LOG(ctx, "max_fps(input/output):%u/%u(sid:%u), min_fl_en:1, ctx->frame_length:%u\n",
		framerate, ctx->current_fps, scenario_id, ctx->frame_length);
	if (ctx->s_ctx.reg_addr_auto_extend ||
			(ctx->frame_length >
			(ctx->exposure[0] + ctx->s_ctx.mode[scenario_id].exposure_margin))) {
		if (ctx->s_ctx.aov_sensor_support &&
			ctx->s_ctx.mode[scenario_id].aov_mode &&
			!ctx->s_ctx.mode[scenario_id].s_dummy_support)
			DRV_LOG_MUST(ctx, "AOV mode not support set_dummy!\n");
		else
			zhuquec1main_set_dummy(ctx);
	}
	return 0;
}

void zhuquec1main_extend_frame_length_convert(struct subdrv_ctx *ctx, u32 ns)
{
	return ;
}

static int zhuquec1main_extend_frame_length(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u32 ns = (u32)((u64*)para)[0];

	zhuquec1main_extend_frame_length_convert(ctx, ns);
	return 0;
}

static bool dump_i2c_enable = false;

static void dump_i2c_buf(struct subdrv_ctx *ctx, u8 * buf, u32 length)
{
	int i;
	char *out_str = NULL;
	char *strptr = NULL;
	size_t buf_size = SUBDRV_I2C_BUF_SIZE * sizeof(char);
	size_t remind = buf_size;
	int num = 0;

	out_str = kzalloc(buf_size + 1, GFP_KERNEL);
	if (!out_str)
		return;

	strptr = out_str;
	memset(out_str, 0, buf_size + 1);

	num = snprintf(strptr, remind,"[ ");
	remind -= num;
	strptr += num;

	for (i = 0 ; i < length; i ++) {
		num = snprintf(strptr, remind,"0x%02x, ", buf[i]);

		if (num <= 0) {
			DRV_LOG(ctx, "snprintf return negative at line %d\n", __LINE__);
			kfree(out_str);
			return;
		}

		remind -= num;
		strptr += num;

		if (remind <= 20) {
			DRV_LOG(ctx, " write %s\n", out_str);
			memset(out_str, 0, buf_size + 1);
			strptr = out_str;
			remind = buf_size;
		}
	}

	num = snprintf(strptr, remind," ]");
	remind -= num;
	strptr += num;

	DRV_LOG(ctx, " write %s\n", out_str);
	strptr = out_str;
	remind = buf_size;

	kfree(out_str);
}

static int zhuquec1main_i2c_burst_wr_regs_u8(struct subdrv_ctx * ctx, u16 * list, u32 len)
{
	adapter_i2c_burst_wr_regs_u8(ctx, ctx->i2c_write_id >> 1, list, len);
	return 	0;
}

//addr16 data8
static int adapter_i2c_burst_wr_regs_u8(struct subdrv_ctx * ctx ,
		u16 addr, u16 *list, u32 len)
{
	struct i2c_client *i2c_client = ctx->i2c_client;
	struct i2c_msg  msg;
	struct i2c_msg *pmsg = &msg;

	u8 *pbuf = NULL;
	u16 *plist = NULL;
	u16 *plist_end = NULL;

	u32 sent = 0;
	u32 total = 0;
	u32 per_sent = 0;
	int ret, i;

	if(!msg_buf) {
		LOG_INF("malloc msg_buf retry");
		msg_buf = kmalloc(MAX_BURST_LEN, GFP_KERNEL);
		if(!msg_buf) {
			LOG_INF("malloc error");
			return -ENOMEM;
		}
	}

	/* each msg contains addr(u16) + val(u8 *) */
	sent = 0;
	total = len / 2;
	plist = list;
	plist_end = list + len - 2;

	DRV_LOG(ctx, "len(%u)  total(%u)", len, total);

	while (sent < total) {

		per_sent = 0;
		pmsg = &msg;
		pbuf = msg_buf;

		pmsg->addr = addr;
		pmsg->flags = i2c_client->flags;
		pmsg->buf = pbuf;

		pbuf[0] = plist[0] >> 8;    //address
		pbuf[1] = plist[0] & 0xff;
		pbuf[2] = plist[1] & 0xff;

		pbuf += 3;
		pmsg->len = 3;
		per_sent += 1;

		for (i = 0; i < total - sent - 1; i++) {  //Maximum number of remaining cycles - 1
			if(plist[0] + 1 == plist[2] ) {  //Addresses are consecutive
				pbuf[0] = plist[3] & 0xff;

				pbuf += 1;
				pmsg->len += 1;
				per_sent += 1;
				plist += 2;

				if(pmsg->len >= MAX_BURST_LEN) {
					break;
				}
			}
		}
		plist += 2;

		if(dump_i2c_enable) {
			DRV_LOG(ctx, "pmsg->len(%d) buff: ", pmsg->len);
			dump_i2c_buf(ctx, msg_buf, pmsg->len);
		}

		ret = i2c_transfer(i2c_client->adapter, pmsg, 1);

		if (ret < 0) {
			dev_info(&i2c_client->dev,
				"i2c transfer failed (%d)\n", ret);
			return -EIO;
		}

		sent += per_sent;

		DRV_LOG(ctx, "sent(%u)  total(%u)  per_sent(%u)", sent, total, per_sent);
	}

	return 0;
}

static void get_imgsensor_id_from_dts(struct subdrv_ctx *ctx, u32 *sensor_id) {
	struct subdrv_entry *m_subdrv_entry = &zhuquec1main_mipi_raw_entry;
	u32 final_sensor_id = 0xFFFFFFFF;
	const char *of_sensor_names[OF_SENSOR_NAMES_MAXCNT];
	const char *of_sensor_hal_names[OF_SENSOR_NAMES_MAXCNT];
	u32   of_sensor_ids[OF_SENSOR_NAMES_MAXCNT] = {0};
	int i, index, of_sensor_names_cnt, of_sensor_hal_names_cnt, of_sensor_ids_ret;
	struct device *dev = &ctx->i2c_client->dev;

	memset(&of_sensor_ids, 0xFF, sizeof(of_sensor_ids));

	if(g_id_from_dts_flag == false) {
		of_sensor_names_cnt = of_property_read_string_array(dev->of_node,
			"sensor-names", of_sensor_names, ARRAY_SIZE(of_sensor_names));

		of_sensor_hal_names_cnt = of_property_read_string_array(dev->of_node,
			"sensor-hal-names", of_sensor_hal_names, ARRAY_SIZE(of_sensor_hal_names));

		of_sensor_ids_ret = of_property_read_u32_array(dev->of_node,
				"sensor-ids", of_sensor_ids, of_sensor_names_cnt);

		pr_err("%s of_sensor_names_cnt(%d), of_sensor_ids_ret(%d)",
			__func__, of_sensor_names_cnt, of_sensor_ids_ret);
		for(i = 0 ;i < of_sensor_names_cnt; i++) {
				pr_err("%s of_sensor_names[%d] = %s  of_sensor_ids[%d] = %d",
				__func__, i, of_sensor_names[i], i, of_sensor_ids[i]);
		}
		for(i = 0 ;i < of_sensor_hal_names_cnt; i++) {
			pr_err("%s of_sensor_hal_names_cnt[%d] = %s",
				__func__, i, of_sensor_hal_names[i]);
		}

		if (of_sensor_names_cnt && (of_sensor_ids_ret == 0)) {
			for(index = 0; index < of_sensor_names_cnt; index++) {
				if (strncmp(SENSOR_NAME, of_sensor_names[index], strlen(SENSOR_NAME)) == 0) {
					final_sensor_id = of_sensor_ids[index];
					break;
				}
			}
		} else {
			pr_err("%s sensor-ids error in dts", __func__);
		}
		g_id_from_dts_flag = true;
	}

	if(final_sensor_id != 0xFFFFFFFF) {
		*sensor_id = final_sensor_id;
		ctx->s_ctx.sensor_id = final_sensor_id;

		m_subdrv_entry->id = final_sensor_id;
		if(of_sensor_hal_names_cnt == of_sensor_names_cnt) {
			m_subdrv_entry->name = of_sensor_hal_names[index];
		}

		pr_err("%s final index(%d), id(%d) name(%s)",
			__func__, index, m_subdrv_entry->id, m_subdrv_entry->name);
	} else {
		*sensor_id = ctx->s_ctx.sensor_id;
	}

	return;
}

static int zhuquec1main_set_gain(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u64* feature_data = (u64*)para;
	u32 gain = *feature_data;
	zhuquec1main_set_gain_convert(ctx, gain);
	return 0;
}

static void zhuquec1main_set_gain_convert(struct subdrv_ctx *ctx, u32 gain)
{
	u16 rg_gain;
	bool gph = !ctx->is_seamless && (ctx->s_ctx.s_gph != NULL);

	/* check boundary of gain */
	gain = max(gain,
		ctx->s_ctx.mode[ctx->current_scenario_id].multi_exposure_ana_gain_range[0].min);
	gain = min(gain,
		ctx->s_ctx.mode[ctx->current_scenario_id].multi_exposure_ana_gain_range[0].max);
	/* mapping of gain to register value */
	if (ctx->s_ctx.g_gain2reg != NULL)
		rg_gain = ctx->s_ctx.g_gain2reg(gain);
	else
		rg_gain = gain2reg(gain);
	/* restore gain */
	memset(ctx->ana_gain, 0, sizeof(ctx->ana_gain));
	ctx->ana_gain[0] = gain;
	/* group hold start */
	if (gph && !ctx->ae_ctrl_gph_en)
		ctx->s_ctx.s_gph((void *)ctx, 1);
	/* write gain */

	zhuquec1main_set_hgc_to_buf(ctx, rg_gain, 0);

	set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_ana_gain[0].addr[0],
		(rg_gain >> 8) & 0xFF);
	set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_ana_gain[0].addr[1],
		rg_gain & 0xFF);
	DRV_LOG(ctx, "gain[0x%x]\n", rg_gain);

	if (gph)
		ctx->s_ctx.s_gph((void *)ctx, 0);
	commit_i2c_buffer(ctx);
	/* group hold end */
}

void zhuquec1main_set_multi_gain(struct subdrv_ctx *ctx, u32 *gains, u16 exp_cnt)
{
	int i = 0;
	u16 rg_gains[3] = {0};
	u8 has_gains[3] = {0};
	bool gph = !ctx->is_seamless && (ctx->s_ctx.s_gph != NULL);

	if (exp_cnt > ARRAY_SIZE(ctx->ana_gain)) {
		DRV_LOGE(ctx, "invalid exp_cnt:%u>%lu\n", exp_cnt, ARRAY_SIZE(ctx->ana_gain));
		exp_cnt = ARRAY_SIZE(ctx->ana_gain);
	}

	if( ctx->s_ctx.mode[ctx->current_scenario_id].hdr_mode == HDR_RAW_DCG_RAW) {  //DCG  HCG = 3.45LCG
		gains[0] = gains[0] * 1024 / 3532;
	}

	for (i = 0; i < exp_cnt; i++) {
		/* check boundary of gain */
		gains[i] = max(gains[i],
			ctx->s_ctx.mode[ctx->current_scenario_id].multi_exposure_ana_gain_range[i].min);
		gains[i] = min(gains[i],
			ctx->s_ctx.mode[ctx->current_scenario_id].multi_exposure_ana_gain_range[i].max);
		/* mapping of gain to register value */
		if (ctx->s_ctx.g_gain2reg != NULL)
			gains[i] = ctx->s_ctx.g_gain2reg(gains[i]);
		else
			gains[i] = gain2reg(gains[i]);
	}
	/* restore gain */
	memset(ctx->ana_gain, 0, sizeof(ctx->ana_gain));
	for (i = 0; i < exp_cnt; i++)
		ctx->ana_gain[i] = gains[i];
	/* group hold start */
	if (gph && !ctx->ae_ctrl_gph_en)
		ctx->s_ctx.s_gph((void *)ctx, 1);
	/* write gain */
	memset(has_gains, 1, sizeof(has_gains));
	switch (exp_cnt) {
	case 2:
		rg_gains[0] = gains[0];
		has_gains[1] = 0;
		rg_gains[2] = gains[1];
		break;
	case 3:
		rg_gains[0] = gains[0];
		rg_gains[1] = gains[1];
		rg_gains[2] = gains[2];
		break;
	default:
		has_gains[0] = 0;
		has_gains[1] = 0;
		has_gains[2] = 0;
		break;
	}

	zhuquec1main_set_hgc_to_buf(ctx, rg_gains[0], rg_gains[2]);

	for (i = 0; i < 3; i++) {
		if (has_gains[i]) {
			set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_ana_gain[i].addr[0],
				(rg_gains[i] >> 8) & 0xFF);
			set_i2c_buffer(ctx,	ctx->s_ctx.reg_addr_ana_gain[i].addr[1],
				rg_gains[i] & 0xFF);
		}
	}
	DRV_LOG(ctx, "reg[lg/mg/sg]: 0x%x 0x%x 0x%x\n", rg_gains[0], rg_gains[1], rg_gains[2]);
	if (gph)
		ctx->s_ctx.s_gph((void *)ctx, 0);
	commit_i2c_buffer(ctx);
	/* group hold end */
}


static void zhuquec1main_set_hdr_tri_gain(struct subdrv_ctx *ctx, u64 *gains, u16 exp_cnt)
{
	int i = 0;
	u32 values[3] = {0};

	if (gains != NULL) {
		for (i = 0; i < 3; i++)
			values[i] = (u32) *(gains + i);
	}
	zhuquec1main_set_multi_gain(ctx, values, exp_cnt);
}

static int zhuquec1main_set_hdr_tri_gain2(struct subdrv_ctx *ctx, u8 *para, u32 *len) {

	u64 *feature_data = (u64 *) para;
	zhuquec1main_set_hdr_tri_gain(ctx, feature_data, 2);
	return 0;
}

static int zhuquec1main_set_hdr_tri_gain3(struct subdrv_ctx *ctx, u8 *para, u32 *len) {
	u64 *feature_data = (u64 *) para;
	zhuquec1main_set_hdr_tri_gain(ctx, feature_data, 3);
	return 0;
}

static int zhuquec1main_common_control(struct subdrv_ctx *ctx,
			enum SENSOR_SCENARIO_ID_ENUM scenario_id,
			MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	int ret = ERROR_NONE;
	u64 time_boot_begin = 0;
	struct adaptor_ctx *_adaptor_ctx = NULL;
	struct v4l2_subdev *sd = NULL;

	if (ctx->i2c_client)
		sd = i2c_get_clientdata(ctx->i2c_client);
	if (sd)
		_adaptor_ctx = to_ctx(sd);
	if (!_adaptor_ctx) {
		DRV_LOGE(ctx, "null _adaptor_ctx\n");
		return -ENODEV;
	}

	if (scenario_id >= ctx->s_ctx.sensor_mode_num) {
		DRV_LOG(ctx, "invalid sid:%u, mode_num:%u\n",
			scenario_id, ctx->s_ctx.sensor_mode_num);
		scenario_id = SENSOR_SCENARIO_ID_NORMAL_PREVIEW;
		ret = ERROR_INVALID_SCENARIO_ID;
	}
	if (ctx->s_ctx.chk_s_off_sta)
		check_stream_off(ctx);
	update_mode_info(ctx, scenario_id);

	if (ctx->s_ctx.mode[scenario_id].mode_setting_table != NULL) {
		DRV_LOG_MUST(ctx, "E: sid:%u size:%u\n", scenario_id,
			ctx->s_ctx.mode[scenario_id].mode_setting_len);
		if (ctx->power_on_profile_en)
			time_boot_begin = ktime_get_boottime_ns();

		/* initail setting */
		zhuquec1main_i2c_burst_wr_regs_u8(ctx, ctx->s_ctx.mode[scenario_id].mode_setting_table,
			ctx->s_ctx.mode[scenario_id].mode_setting_len);

		zhuquec1main_get_hgc_from_settings(ctx);

		if (ctx->power_on_profile_en) {
			ctx->sensor_pw_on_profile.i2c_cfg_period =
					ktime_get_boottime_ns() - time_boot_begin;

			ctx->sensor_pw_on_profile.i2c_cfg_table_len =
					ctx->s_ctx.mode[scenario_id].mode_setting_len;
		}
		DRV_LOG(ctx, "X: sid:%u size:%u\n", scenario_id,
			ctx->s_ctx.mode[scenario_id].mode_setting_len);
	} else {
		DRV_LOGE(ctx, "please implement mode setting(sid:%u)!\n", scenario_id);
	}

	set_mirror_flip(ctx, ctx->s_ctx.mirror);

	is_first_exp = true;

	return ret;
}

static void zhuquec1main_get_exp_offset_from_settings(struct subdrv_ctx *ctx)
{
	u32 i = 0;
	bool flag[] = {false, false, false};

	kal_uint16 * list = ctx->s_ctx.mode[ctx->current_scenario_id].mode_setting_table;
	u32 length = ctx->s_ctx.mode[ctx->current_scenario_id].mode_setting_len;

	fix_short_exp = 0;

	for (i = 0; i < length; i += 2) {
		if(list[i] == 0x3830) { //short exposure adjust number
			exp_offset = list[i + 1];
			flag[0] = true;
		}
		if(list[i] == 0x3846) {  //stg_hdr_rst_pt_m
			fix_short_exp |= (list[i + 1] & 0xFF) << 8;
			flag[1] = true;
		}
		if(list[i] == 0x3847) {  //stg_hdr_rst_pt_m
			fix_short_exp |=  list[i + 1] & 0xFF;
			flag[2] = true;
		}

		if(flag[0] == true && flag[1] == true && flag[2] == true) {
			DRV_LOG(ctx, "exp_offset= 0x%x = %d", exp_offset, exp_offset);
			DRV_LOG(ctx, "fix_short_exp= 0x%x  = %d", fix_short_exp, fix_short_exp);
			break;
		}
	}
}


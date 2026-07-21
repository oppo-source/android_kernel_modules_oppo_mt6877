// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define PFX "CAM_CAL_HIMAMAIN "
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include "cam_cal_list.h"
#include "eeprom_i2c_common_driver.h"
#include "eeprom_i2c_custom_driver.h"
#include "cam_cal_config.h"
#include "oplus_kd_imgsensor.h"

#define OTP_I2C_ADDR 0x20
#define HIMALAYANMAIN_OTP_RET_FAIL -1
#define HIMALAYANMAIN_OTP_RET_SUCCESS 0
#define HIMALAYANMAIN_TXD_MAIN_OTP_MODULE_LENS 18
#define HIMALAYANMAIN_TXD_MAIN_OTP_AWB_LENS 18
#define HIMALAYANMAIN_TXD_MAIN_OTP_LRC_LENS 6
#define HIMALAYANMAIN_TXD_MAIN_OTP_AF_LENS 	10
#define HIMALAYANMAIN_TXD_MAIN_OTP_SN_LENS 	25
#define HIMALAYANMAIN_TXD_MAIN_OTP_LSC_LENS 1870

//flag addr
#define HIMALAYANMAIN_OTP_MODULE_FLAGADDR 0x827A
#define HIMALAYANMAIN_GROUP1_FLAG 0x01
#define HIMALAYANMAIN_GROUP2_FLAG 0x13
#define HIMALAYANMAIN_INVALID_FLAG 0x0

//data start addr
#define HIMALAYANMAIN_OTP_MODULE_GROUP1_STARTADDR 0x827B
#define HIMALAYANMAIN_OTP_AWB_GROUP1_STARTADDR 0x829F
#define HIMALAYANMAIN_OTP_LRC_GROUP1_STARTADDR 0x82C3
#define HIMALAYANMAIN_OTP_AF_GROUP1_STARTADDR 0x82CF
#define HIMALAYANMAIN_OTP_SN_GROUP1_STARTADDR 0X82E3

#define HIMALAYANMAIN_OTP_MODULE_GROUP2_STARTADDR 0x828D
#define HIMALAYANMAIN_OTP_AWB_GROUP2_STARTADDR 0x82B1
#define HIMALAYANMAIN_OTP_LRC_GROUP2_STARTADDR 0x82C9
#define HIMALAYANMAIN_OTP_AF_GROUP2_STARTADDR 0x82D9
#define HIMALAYANMAIN_OTP_SN_GROUP2_STARTADDR 0x82FD

enum himalayanmain_sensor_otp_page{
	page_0 = 0,
	page_1,
	page_2,
	page_3,
	page_4,
	page_5,
	page_6,
	page_7,
	page_8,
	page_9,
	page_10,
	page_11,
	page_12,
	page_max
};

struct himalayanmain_txd_main_otp_struct {
	unsigned char ModuleFlag;
	unsigned char module_info[HIMALAYANMAIN_TXD_MAIN_OTP_MODULE_LENS];
	unsigned char wb_data[HIMALAYANMAIN_TXD_MAIN_OTP_AWB_LENS];
	unsigned char lrc_data[HIMALAYANMAIN_TXD_MAIN_OTP_LRC_LENS];
	unsigned char af_data[HIMALAYANMAIN_TXD_MAIN_OTP_AF_LENS];
	unsigned char sn_data[HIMALAYANMAIN_TXD_MAIN_OTP_SN_LENS];
	unsigned char lsc_data[HIMALAYANMAIN_TXD_MAIN_OTP_LSC_LENS];
};

struct himalayanmain_txd_main_otp_struct himalayanmain_txd_main_otp = {
	.ModuleFlag = 0
};
EXPORT_SYMBOL(himalayanmain_txd_main_otp);

int himamain_i2c_rd_u8(struct i2c_client *i2c_client,
		u16 addr, u16 reg, u8 *val)
{
	int ret;
	u8 buf[2];
	struct i2c_msg msg[2];

	if (i2c_client == NULL)
		return -ENODEV;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	msg[0].addr = addr;
	msg[0].flags = i2c_client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = addr;
	msg[1].flags = i2c_client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(i2c_client->adapter, msg, 2);
	if (ret < 0) {
		dev_info(&i2c_client->dev, "i2c transfer failed (%d), r - reg = 0x%04x, val = 0x%02x\n", ret, reg, *val);
		return ret;
	}

	*val = buf[0];

	return 0;
}

int himamain_i2c_wr_u8(struct i2c_client *i2c_client,
		u16 addr, u16 reg, u8 val)
{
	int ret;
	u8 buf[3];
	struct i2c_msg msg;

	if (i2c_client == NULL)
		return -ENODEV;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	buf[2] = val;

	msg.addr = addr;
	msg.flags = i2c_client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(i2c_client->adapter, &msg, 1);
	if (ret < 0)
		dev_info(&i2c_client->dev, "i2c transfer failed (%d), w - reg = 0x%04x, val = 0x%02x\n", ret, reg, val);

	return ret;
}

#define subdrv_i2c_rd_u8(i2c_client, reg) \
({ \
	u8 __val = 0xff; \
	himamain_i2c_rd_u8(i2c_client, \
		i2c_client->addr >> 1, reg, &__val); \
	__val; \
})

#define subdrv_i2c_wr_u8(i2c_client, reg, val) \
	himamain_i2c_wr_u8(i2c_client, \
		i2c_client->addr >> 1, reg, val)

static unsigned int do_part_number_himamain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData);
static unsigned int do_single_lsc_himamain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData);
static unsigned int do_2a_gain_himamain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData);
static unsigned int do_lens_id_himamain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData);
static unsigned int layout_check_himamain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int sensorID, unsigned int *_cfg);

static struct STRUCT_CALIBRATION_LAYOUT_STRUCT cal_layout_table = {
	0x00000006, 0x02410320, CAM_CAL_SINGLE_EEPROM_DATA,
	{
		{0x00000001, 0x00000000, 0x00000000, do_module_version},
		{0x00000001, 0x00000000, 0x00000002, do_part_number_himamain},
		{0x00000001, 0x00000530, 0x0000074C, do_single_lsc_himamain},
		{0x00000001, 0x00000007, 0x0000000E, do_2a_gain_himamain}, //Start address, block size is useless
		{0x00000000, 0x00000000, 0x00000000, do_pdaf},
		{0x00000000, 0x00000000, 0x00000000, do_stereo_data},
		{0x00000001, 0x00000000, 0x00002000, do_dump_all},
		{0x00000001, 0x00000008, 0x00000002, do_lens_id_himamain}
	}
};

struct STRUCT_CAM_CAL_CONFIG_STRUCT himamain_op_eeprom = {
	.name = "himamain_op_eeprom",
	.check_layout_function = layout_check_himamain,
	.read_function = Common_read_region,
	.layout = &cal_layout_table,
	.sensor_id = HIMALAYANMAIN_SENSOR_ID,
	.i2c_write_id = OTP_I2C_ADDR,
	.max_size = 0x1FFF,
	.enable_preload = 1,
	.preload_size = 0x1FFF,
};
static struct STRUCT_CAM_CAL_CONFIG_STRUCT *cam_cal_config = &himamain_op_eeprom;

static unsigned int do_part_number_himamain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
				(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];
	unsigned int size_limit = sizeof(pCamCalData->PartNumber);

	memset(&pCamCalData->PartNumber[0], 0, size_limit);

	if (block_size > size_limit) {
		error_log("part number size can't larger than %u\n", size_limit);
		return err;
	}

	memcpy(&pCamCalData->PartNumber[0], &himalayanmain_txd_main_otp.module_info[0], block_size);
	debug_log("partNumber[0] = 0x%x, partNumber[1] = 0x%x\n", pCamCalData->PartNumber[0], pCamCalData->PartNumber[1]);

	return CAM_CAL_ERR_NO_ERR;
}

static unsigned int do_single_lsc_himamain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
				(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;

	unsigned int err = CamCalReturnErr[pCamCalData->Command];
	unsigned short table_size;

	if (pCamCalData->DataVer >= CAM_CAL_TYPE_NUM) {
		err = CAM_CAL_ERR_NO_DEVICE;
		error_log("Read Failed\n");
		show_cmd_error_log(pCamCalData->Command);
		return err;
	}
	if (block_size != CAM_CAL_SINGLE_LSC_SIZE)
		error_log("block_size(%d) is not match (%d)\n",
				block_size, CAM_CAL_SINGLE_LSC_SIZE);

	pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType = 2;//mtk type
	pCamCalData->SingleLsc.LscTable.MtkLcsData.PixId = 8;

	table_size = block_size;

	debug_log("lsc table_size %d\n", table_size);
	pCamCalData->SingleLsc.LscTable.MtkLcsData.TableSize = table_size;
	if (table_size > 0) {
		pCamCalData->SingleLsc.TableRotation = 2;
		debug_log("u4Offset=%d u4Length=%d", start_addr, table_size);
		memcpy(&pCamCalData->SingleLsc.LscTable.MtkLcsData.SlimLscType, &himalayanmain_txd_main_otp.lsc_data[0], block_size);
		err = CAM_CAL_ERR_NO_ERR;
		// read_data_size = read_data(pdata,
		// 	pCamCalData->sensorID, pCamCalData->deviceID,
		// 	start_addr, table_size, (unsigned char *)
		// 	&pCamCalData->SingleLsc.LscTable.MtkLcsData.SlimLscType);
		// if (table_size == read_data_size)
		// 	err = CAM_CAL_ERR_NO_ERR;
		// else {
		// 	error_log("Read Failed\n");
		// 	err = CamCalReturnErr[pCamCalData->Command];
		// 	show_cmd_error_log(pCamCalData->Command);
		// }
	}
	#ifdef DEBUG_CALIBRATION_LOAD
	debug_log("======================SingleLsc Data==================\n");
	debug_log("[1st] = %x, %x, %x, %x\n",
		pCamCalData->SingleLsc.LscTable.Data[0],
		pCamCalData->SingleLsc.LscTable.Data[1],
		pCamCalData->SingleLsc.LscTable.Data[2],
		pCamCalData->SingleLsc.LscTable.Data[3]);
	debug_log("[1st] = SensorLSC(1)?MTKLSC(2)?  %x\n",
		pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType);
	debug_log("CapIspReg =0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[0],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[1],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[2],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[3],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[4]);
	debug_log("RETURN = 0x%x\n", err);
	debug_log("======================SingleLsc Data==================\n");
	#endif

	return err;
}

static unsigned int do_2a_gain_himamain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
				(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];

	long long CalGain, FacGain, CalValue;
	unsigned char AWBAFConfig = 0xf;

	unsigned short AFInf, AFMacro;
	int tempMax = 0;
	int CalR = 1, CalGr = 1, CalGb = 1, CalG = 1, CalB = 1;
	int FacR = 1, FacGr = 1, FacGb = 1, FacG = 1, FacB = 1;
	int rgCalValue = 1, bgCalValue = 1;

	(void) start_addr;
	(void) block_size;

	debug_log("In %s: sensor_id=%x\n", __func__, pCamCalData->sensorID);
	memset((void *)&pCamCalData->Single2A, 0, sizeof(struct STRUCT_CAM_CAL_SINGLE_2A_STRUCT));
	/* Check rule */
	if (pCamCalData->DataVer >= CAM_CAL_TYPE_NUM) {
		err = CAM_CAL_ERR_NO_DEVICE;
		error_log("Read Failed\n");
		show_cmd_error_log(pCamCalData->Command);
		return err;
	}
	/* Check AWB & AF enable bit */
	pCamCalData->Single2A.S2aVer = 0x01;
	pCamCalData->Single2A.S2aBitEn = (0x03 & AWBAFConfig);
	pCamCalData->Single2A.S2aAfBitflagEn = (0x0C & AWBAFConfig);
	debug_log("S2aBitEn=0x%02x", pCamCalData->Single2A.S2aBitEn);
	/* AWB Calibration Data*/
	if (0x1 & AWBAFConfig) {
		pCamCalData->Single2A.S2aAwb.rGainSetNum = 0x02;
		memcpy(&CalValue, &himalayanmain_txd_main_otp.lrc_data[0], 4);
		// awb_offset = 0x60;
		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		awb_offset, 4, (unsigned char *)&CalValue);
		rgCalValue  = CalValue & 0xFFFF;
		bgCalValue = (CalValue >> 16) & 0xFFFF;
		debug_log("Light source calibration 5100K value R/G:%d, B/G:%d",rgCalValue, bgCalValue);
		err = CAM_CAL_ERR_NO_ERR;
		/* AWB Unit Gain (5000K) */
		debug_log("5000K AWB\n");
		memcpy(&CalGain, &himalayanmain_txd_main_otp.wb_data[0], 8);
		// awb_offset = 0x10;
		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		awb_offset, 8, (unsigned char *)&CalGain);
		CalR  = CalGain & 0xFFFF;
		CalGr = (CalGain >> 16) & 0xFFFF;
		CalGb = (CalGain >> 32) & 0xFFFF;
		CalG  = ((CalGr + CalGb) + 1) >> 1;
		CalB  = (CalGain >> 48) & 0xFFFF;
		CalR  = CalR * rgCalValue / 1000;
		CalB  = CalB * bgCalValue / 1000;
		if (CalR > CalG)
			/* R > G */
			if (CalR > CalB)
				tempMax = CalR;
			else
				tempMax = CalB;
		else
			/* G > R */
			if (CalG > CalB)
				tempMax = CalG;
			else
				tempMax = CalB;
		debug_log("UnitR:%d, UnitG:%d, UnitB:%d, New Unit Max=%d",
				CalR, CalG, CalB, tempMax);
		err = CAM_CAL_ERR_NO_ERR;
		if (CalGain != 0x0000000000000000 &&
			CalGain != 0xFFFFFFFFFFFFFFFF &&
			CalR    != 0x00000000 &&
			CalG    != 0x00000000 &&
			CalB    != 0x00000000) {
			pCamCalData->Single2A.S2aAwb.rGainSetNum = 1;
			pCamCalData->Single2A.S2aAwb.rUnitGainu4R =
					(unsigned int)((tempMax * 512 + (CalR >> 1)) / CalR);
			pCamCalData->Single2A.S2aAwb.rUnitGainu4G =
					(unsigned int)((tempMax * 512 + (CalG >> 1)) / CalG);
			pCamCalData->Single2A.S2aAwb.rUnitGainu4B =
					(unsigned int)((tempMax * 512 + (CalB >> 1)) / CalB);
		} else {
			error_log("There are something wrong on EEPROM, plz contact module vendor!!\n");
			error_log("Unit R=%d G=%d B=%d!!\n", CalR, CalG, CalB);
		}
		/* AWB Golden Gain (5100K) */
		memcpy(&FacGain, &himalayanmain_txd_main_otp.wb_data[8], 8);
		// awb_offset = 0x18;
		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		awb_offset, 8, (unsigned char *)&FacGain);
		debug_log("Read FacGain OK\n");
		FacR  = FacGain & 0xFFFF;
		FacGr = (FacGain >> 16) & 0xFFFF;
		FacGb = (FacGain >> 32) & 0xFFFF;
		FacG  = ((FacGr + FacGb) + 1) >> 1;
		FacB  = (FacGain >> 48) & 0xFFFF;
		if (FacR > FacG)
			if (FacR > FacB)
				tempMax = FacR;
			else
				tempMax = FacB;
		else
			if (FacG > FacB)
				tempMax = FacG;
			else
				tempMax = FacB;
		debug_log("GoldenR:%d, GoldenG:%d, GoldenB:%d, New Golden Max=%d",
				FacR, FacG, FacB, tempMax);
		err = CAM_CAL_ERR_NO_ERR;
		if (FacGain != 0x0000000000000000 &&
			FacGain != 0xFFFFFFFFFFFFFFFF &&
			FacR    != 0x00000000 &&
			FacG    != 0x00000000 &&
			FacB    != 0x00000000)	{
			pCamCalData->Single2A.S2aAwb.rGoldGainu4R =
					(unsigned int)((tempMax * 512 + (FacR >> 1)) / FacR);
			pCamCalData->Single2A.S2aAwb.rGoldGainu4G =
					(unsigned int)((tempMax * 512 + (FacG >> 1)) / FacG);
			pCamCalData->Single2A.S2aAwb.rGoldGainu4B =
					(unsigned int)((tempMax * 512 + (FacB >> 1)) / FacB);
		} else {
			error_log("There are something wrong on EEPROM, plz contact module vendor!!");
			error_log("Golden R=%d G=%d B=%d\n", FacR, FacG, FacB);
		}
		/* Set AWB to 3A Layer */
		pCamCalData->Single2A.S2aAwb.rValueR   = CalR;
		pCamCalData->Single2A.S2aAwb.rValueGr  = CalGr;
		pCamCalData->Single2A.S2aAwb.rValueGb  = CalGb;
		pCamCalData->Single2A.S2aAwb.rValueB   = CalB;
		pCamCalData->Single2A.S2aAwb.rGoldenR  = FacR;
		pCamCalData->Single2A.S2aAwb.rGoldenGr = FacGr;
		pCamCalData->Single2A.S2aAwb.rGoldenGb = FacGb;
		pCamCalData->Single2A.S2aAwb.rGoldenB  = FacB;
		#ifdef DEBUG_CALIBRATION_LOAD
		debug_log("======================AWB CAM_CAL==================\n");
		debug_log("AWB Calibration @5100K\n");
		debug_log("[CalGain] = 0x%x\n", CalGain);
		debug_log("[FacGain] = 0x%x\n", FacGain);
		debug_log("[rCalGain.u4R] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4R);
		debug_log("[rCalGain.u4G] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4G);
		debug_log("[rCalGain.u4B] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4B);
		debug_log("[rFacGain.u4R] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4R);
		debug_log("[rFacGain.u4G] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4G);
		debug_log("[rFacGain.u4B] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4B);
		#endif

		/* AWB Unit Gain (4000K) */
		CalR = CalGr = CalGb = CalG = CalB = 0;
		tempMax = 0;
		debug_log("4000K AWB\n");
		rgCalValue = 0;
		bgCalValue = 0;
		memcpy(&CalValue, &himalayanmain_txd_main_otp.lrc_data[0], 4);
		// awb_offset = 0x6C;
		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		awb_offset, 8, (unsigned char *)&CalValue);
		debug_log( "Read CalValue OK\n");
		rgCalValue  = CalValue & 0xFFFF;
		bgCalValue = (CalValue >> 16) & 0xFFFF;
		debug_log("Light source calibration value 3100 R/G:%d, B/G:%d",rgCalValue, bgCalValue);
		err = CAM_CAL_ERR_NO_ERR;
		memcpy(&CalGain, &himalayanmain_txd_main_otp.wb_data[0], 8);
		// awb_offset = 0x34;
		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		awb_offset, 8, (unsigned char *)&CalGain);
		CalR  = CalGain & 0xFFFF;
		CalGr = (CalGain >> 16) & 0xFFFF;
		CalGb = (CalGain >> 32) & 0xFFFF;
		CalG  = ((CalGr + CalGb) + 1) >> 1;
		CalB  = (CalGain >> 48) & 0xFFFF;
		debug_log("CalR:%d, CalB:%d",CalR, CalB);
		CalR  = CalR * rgCalValue / 1000;
		CalB  = CalB * bgCalValue / 1000;
		if (CalR > CalG)
			/* R > G */
			if (CalR > CalB)
				tempMax = CalR;
			else
				tempMax = CalB;
		else
			/* G > R */
			if (CalG > CalB)
				tempMax = CalG;
			else
				tempMax = CalB;
		debug_log("UnitR:%d, UnitG:%d, UnitB:%d, New Unit Max=%d",
				CalR, CalG, CalB, tempMax);
		err = CAM_CAL_ERR_NO_ERR;

		if (CalGain != 0x0000000000000000 &&
			CalGain != 0xFFFFFFFFFFFFFFFF &&
			CalR    != 0x00000000 &&
			CalG    != 0x00000000 &&
			CalB    != 0x00000000) {
			pCamCalData->Single2A.S2aAwb.rGainSetNum = 2;
			pCamCalData->Single2A.S2aAwb.rUnitGainu4R_mid =
				(unsigned int)((tempMax * 512 + (CalR >> 1)) / CalR);
			pCamCalData->Single2A.S2aAwb.rUnitGainu4G_mid =
				(unsigned int)((tempMax * 512 + (CalG >> 1)) / CalG);
			pCamCalData->Single2A.S2aAwb.rUnitGainu4B_mid =
				(unsigned int)((tempMax * 512 + (CalB >> 1)) / CalB);
		} else {
			error_log("There are something wrong on EEPROM, plz contact module vendor!!\n");
			error_log("Unit R=%d G=%d B=%d!!\n", CalR, CalG, CalB);
		}
		/* AWB Golden Gain (4000K) */
		FacR = FacGr = FacGb = FacG = FacB = 0;
		tempMax = 0;
		memcpy(&FacGain, &himalayanmain_txd_main_otp.wb_data[8], 8);
		// awb_offset = 0x3C;
		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		awb_offset, 8, (unsigned char *)&FacGain);
		debug_log("Read FacGain OK\n");
		FacR  = FacGain & 0xFFFF;
		FacGr = (FacGain >> 16) & 0xFFFF;
		FacGb = (FacGain >> 32) & 0xFFFF;
		FacG  = ((FacGr + FacGb) + 1) >> 1;
		FacB  = (FacGain >> 48) & 0xFFFF;
		if (FacR > FacG)
			if (FacR > FacB)
				tempMax = FacR;
			else
				tempMax = FacB;
		else
			if (FacG > FacB)
				tempMax = FacG;
			else
				tempMax = FacB;
		debug_log("GoldenR:%d, GoldenG:%d, GoldenB:%d, New Golden Max=%d",
				FacR, FacG, FacB, tempMax);
		err = CAM_CAL_ERR_NO_ERR;
		if (FacGain != 0x0000000000000000 &&
			FacGain != 0xFFFFFFFFFFFFFFFF &&
			FacR    != 0x00000000 &&
			FacG    != 0x00000000 &&
			FacB    != 0x00000000)	{
			pCamCalData->Single2A.S2aAwb.rGoldGainu4R_mid =
				(unsigned int)((tempMax * 512 + (FacR >> 1)) / FacR);
			pCamCalData->Single2A.S2aAwb.rGoldGainu4G_mid =
				(unsigned int)((tempMax * 512 + (FacG >> 1)) / FacG);
			pCamCalData->Single2A.S2aAwb.rGoldGainu4B_mid =
				(unsigned int)((tempMax * 512 + (FacB >> 1)) / FacB);
		} else {
			error_log("There are something wrong on EEPROM, plz contact module vendor!!");
			error_log("Golden R=%d G=%d B=%d\n", FacR, FacG, FacB);
		}

		/* AWB Unit Gain (3100K) */
		CalR = CalGr = CalGb = CalG = CalB = 0;
		tempMax = 0;
		debug_log("2850K AWB\n");
		rgCalValue = 0;
		bgCalValue = 0;
		memcpy(&CalValue, &himalayanmain_txd_main_otp.lrc_data[0], 4);
		// awb_offset = 0x6C;
		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		awb_offset, 8, (unsigned char *)&CalValue);
		debug_log( "Read CalValue OK\n");
		rgCalValue  = CalValue & 0xFFFF;
		bgCalValue = (CalValue >> 16) & 0xFFFF;
		debug_log("Light source calibration value 3100 R/G:%d, B/G:%d",rgCalValue, bgCalValue);
		err = CAM_CAL_ERR_NO_ERR;
		memcpy(&CalGain, &himalayanmain_txd_main_otp.wb_data[0], 8);
		// awb_offset = 0x34;
		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		awb_offset, 8, (unsigned char *)&CalGain);
		CalR  = CalGain & 0xFFFF;
		CalGr = (CalGain >> 16) & 0xFFFF;
		CalGb = (CalGain >> 32) & 0xFFFF;
		CalG  = ((CalGr + CalGb) + 1) >> 1;
		CalB  = (CalGain >> 48) & 0xFFFF;
		debug_log("CalR:%d, CalB:%d",CalR, CalB);
		CalR  = CalR * rgCalValue / 1000;
		CalB  = CalB * bgCalValue / 1000;
		if (CalR > CalG)
			/* R > G */
			if (CalR > CalB)
				tempMax = CalR;
			else
				tempMax = CalB;
		else
			/* G > R */
			if (CalG > CalB)
				tempMax = CalG;
			else
				tempMax = CalB;
		debug_log("UnitR:%d, UnitG:%d, UnitB:%d, New Unit Max=%d",
				CalR, CalG, CalB, tempMax);
		err = CAM_CAL_ERR_NO_ERR;

		if (CalGain != 0x0000000000000000 &&
			CalGain != 0xFFFFFFFFFFFFFFFF &&
			CalR    != 0x00000000 &&
			CalG    != 0x00000000 &&
			CalB    != 0x00000000) {
			pCamCalData->Single2A.S2aAwb.rGainSetNum = 2;
			pCamCalData->Single2A.S2aAwb.rUnitGainu4R_low =
				(unsigned int)((tempMax * 512 + (CalR >> 1)) / CalR);
			pCamCalData->Single2A.S2aAwb.rUnitGainu4G_low =
				(unsigned int)((tempMax * 512 + (CalG >> 1)) / CalG);
			pCamCalData->Single2A.S2aAwb.rUnitGainu4B_low =
				(unsigned int)((tempMax * 512 + (CalB >> 1)) / CalB);
		} else {
			error_log("There are something wrong on EEPROM, plz contact module vendor!!\n");
			error_log("Unit R=%d G=%d B=%d!!\n", CalR, CalG, CalB);
		}
		/* AWB Golden Gain (3100K) */
		FacR = FacGr = FacGb = FacG = FacB = 0;
		tempMax = 0;
		memcpy(&FacGain, &himalayanmain_txd_main_otp.wb_data[8], 8);
		// awb_offset = 0x3C;
		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		awb_offset, 8, (unsigned char *)&FacGain);
		debug_log("Read FacGain OK\n");
		FacR  = FacGain & 0xFFFF;
		FacGr = (FacGain >> 16) & 0xFFFF;
		FacGb = (FacGain >> 32) & 0xFFFF;
		FacG  = ((FacGr + FacGb) + 1) >> 1;
		FacB  = (FacGain >> 48) & 0xFFFF;
		if (FacR > FacG)
			if (FacR > FacB)
				tempMax = FacR;
			else
				tempMax = FacB;
		else
			if (FacG > FacB)
				tempMax = FacG;
			else
				tempMax = FacB;
		debug_log("GoldenR:%d, GoldenG:%d, GoldenB:%d, New Golden Max=%d",
				FacR, FacG, FacB, tempMax);
		err = CAM_CAL_ERR_NO_ERR;
		if (FacGain != 0x0000000000000000 &&
			FacGain != 0xFFFFFFFFFFFFFFFF &&
			FacR    != 0x00000000 &&
			FacG    != 0x00000000 &&
			FacB    != 0x00000000)	{
			pCamCalData->Single2A.S2aAwb.rGoldGainu4R_low =
				(unsigned int)((tempMax * 512 + (FacR >> 1)) / FacR);
			pCamCalData->Single2A.S2aAwb.rGoldGainu4G_low =
				(unsigned int)((tempMax * 512 + (FacG >> 1)) / FacG);
			pCamCalData->Single2A.S2aAwb.rGoldGainu4B_low =
				(unsigned int)((tempMax * 512 + (FacB >> 1)) / FacB);
		} else {
			error_log("There are something wrong on EEPROM, plz contact module vendor!!");
			error_log("Golden R=%d G=%d B=%d\n", FacR, FacG, FacB);
		}

		#ifdef DEBUG_CALIBRATION_LOAD
		debug_log("AWB Calibration @3100K\n");
		debug_log("[CalGain] = 0x%x\n", CalGain);
		debug_log("[FacGain] = 0x%x\n", FacGain);
		debug_log("[rCalGain.u4R] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4R_low);
		debug_log("[rCalGain.u4G] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4G_low);
		debug_log("[rCalGain.u4B] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4B_low);
		debug_log("[rFacGain.u4R] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4R_low);
		debug_log("[rFacGain.u4G] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4G_low);
		debug_log("[rFacGain.u4B] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4B_low);
		debug_log("======================AWB CAM_CAL==================\n");
		#endif
	}

	/* AF Calibration Data*/
	if (0x2 & AWBAFConfig) {
		memcpy(&AFInf, &himalayanmain_txd_main_otp.af_data[4], 2);
		memcpy(&AFMacro, &himalayanmain_txd_main_otp.af_data[2], 2);
		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		0x94, 2, (unsigned char *)&AFInf);

		// read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		// 		0x92, 2, (unsigned char *)&AFMacro);

/* 		AFInf = AFInf >> 2;
		AFMacro = AFMacro >> 2; */

		pCamCalData->Single2A.S2aAf[0] = AFInf;
		pCamCalData->Single2A.S2aAf[1] = AFMacro;

		////Only AF Gathering <////
		#ifdef DEBUG_CALIBRATION_LOAD
		debug_log("======================AF CAM_CAL==================\n");
		debug_log("[AFInf] = %d\n", AFInf);
		debug_log("[AFMacro] = %d\n", AFMacro);
		debug_log("======================AF CAM_CAL==================\n");
		#endif
	}
	return err;
}

static unsigned int do_lens_id_himamain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
				(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;

	memcpy(&pCamCalData->LensDrvId[0], &himalayanmain_txd_main_otp.module_info[8], 2);
	return CAM_CAL_ERR_NO_ERR;
}

static int himalayanmain_checksum(struct i2c_client *client, u16 checkAddr, unsigned char *checkArray, int length)
{
	int checksum = subdrv_i2c_rd_u8(client, checkAddr);
	int sum = 0;
	for (int i = 0; i < length; i++) {
		sum += checkArray[i];
	}
	pr_info("sum = %d, sum %% 255 = 0x%02x, checksum = 0x%02x\n", sum, sum % 255, checksum);

	if (sum % 255 != checksum)
		return HIMALAYANMAIN_OTP_RET_FAIL;
	return HIMALAYANMAIN_OTP_RET_SUCCESS;
}

static void himalayanmain_read(struct i2c_client *client, unsigned int start, unsigned int end, unsigned char *pinputdata)
{
	u16 addr = start;
	for (int i = 0; i < end - start + 1; i++) {
		pinputdata[i] = subdrv_i2c_rd_u8(client, addr++);
		// printk(PFX "dbgmsg - lsc pinputdata[%d] = 0x%02x\n", i, pinputdata[i]);
	}
}

int himalayanmain_set_threshold(struct i2c_client *client, u8 threshold) //set thereshold
{
	u8 threshold_reg1[3] = { 0x48, 0x48, 0x48 };
	u8 threshold_reg2[3] = { 0x38, 0x18, 0x58 };
	u8 threshold_reg3[3] = { 0x41, 0x41, 0x41 };

	if (threshold < 3 && threshold >= 0) {
		subdrv_i2c_wr_u8(client, 0x36b0, threshold_reg1[threshold]);
		subdrv_i2c_wr_u8(client, 0x36b1, threshold_reg2[threshold]);
		subdrv_i2c_wr_u8(client, 0x36b2, threshold_reg3[threshold]);
		pr_info("himalayanmain_otp set_threshold %d\n", threshold);
	} else {
		pr_err("himalayanmain_otp set invalid threshold %d\n", threshold);

		return HIMALAYANMAIN_OTP_RET_FAIL;
	}

	return HIMALAYANMAIN_OTP_RET_SUCCESS;
}

int himalayanmain_set_page_and_load_data(struct i2c_client *client, int page) //set page
{
	u16 Startaddress = 0;
	u16 EndAddress = 0;
	int delay = 0;
	int pag = 0;

	Startaddress = page * 0x200 + 0x7E00; //set start address in page
	EndAddress = Startaddress + 0x1ff; //set end address in page
	pag = page * 2 - 1; //change page
	subdrv_i2c_wr_u8(client, 0x4408, (Startaddress >> 8) & 0xff);
	subdrv_i2c_wr_u8(client, 0x4409, Startaddress & 0xff);
	subdrv_i2c_wr_u8(client, 0x440a, (EndAddress >> 8) & 0xff);
	subdrv_i2c_wr_u8(client, 0x440b, EndAddress & 0xff);

	subdrv_i2c_wr_u8(client, 0x4401, 0x13); // address set finished
	subdrv_i2c_wr_u8(client, 0x4412, pag & 0xff); // set page
	subdrv_i2c_wr_u8(client, 0x4407, 0x00); // set page finished
	subdrv_i2c_wr_u8(client, 0x4400, 0x11); // manual load begin
	while ((subdrv_i2c_rd_u8(client, 0x4420) & 0x01) == 0x01) {
		delay++;
		pr_info("himalayanmain_otp set_page waitting, OTP is still busy for loading %d times\n", delay);
		if (delay == 10) {
			pr_err("himalayanmain_otp set_page fail, load timeout!!!\n");

			return HIMALAYANMAIN_OTP_RET_FAIL;
		}
		mdelay(10);
	}
	pr_info("himalayanmain_otp set_page success\n");

	return HIMALAYANMAIN_OTP_RET_SUCCESS;
}

static int himalayanmain_sensor_otp_read_data(struct i2c_client *client, u16 ui4_offset,
					unsigned int ui4_length, unsigned char *pinputdata)
{
	int sum = 0, i = 0;
	for (i = 0; i < ui4_length; i++) {
		pinputdata[i] = subdrv_i2c_rd_u8(client, ui4_offset++);
		if (i < ui4_length - 2)
			sum += pinputdata[i];
		// printk(PFX "dbgmsg - func: %s, pinputdata[%d] = 0x%02x\n", __FUNCTION__, i, pinputdata[i]);
	}

	pr_info("sum = %x, sum %% 255 = %x, checksum = %x\n", sum, sum % 255, pinputdata[i - 1]);
	if (sum % 255 != pinputdata[i - 1])
		return -1;
	return 0;
}

static int himalayanmain_sensor_otp_read_module_info(struct i2c_client *client, unsigned char *pinputdata)
{
	int ret = HIMALAYANMAIN_OTP_RET_FAIL;

	himalayanmain_txd_main_otp.ModuleFlag = subdrv_i2c_rd_u8(client, HIMALAYANMAIN_OTP_MODULE_FLAGADDR);
	pr_info("himalayanmain_otp Read ModuleFlag addr :0x%x, data:0x%x\n",
			HIMALAYANMAIN_OTP_MODULE_FLAGADDR,
			himalayanmain_txd_main_otp.ModuleFlag);

	if (himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP1_FLAG) {
		ret = himalayanmain_sensor_otp_read_data(client,
			HIMALAYANMAIN_OTP_MODULE_GROUP1_STARTADDR,
			HIMALAYANMAIN_TXD_MAIN_OTP_MODULE_LENS, pinputdata);
		pr_info("himalayanmain_otp group1 ret = %d!\n", ret);
	} else if (himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP2_FLAG) {
		ret = himalayanmain_sensor_otp_read_data(client,
			HIMALAYANMAIN_OTP_MODULE_GROUP2_STARTADDR,
			HIMALAYANMAIN_TXD_MAIN_OTP_MODULE_LENS, pinputdata);
		pr_info("himalayanmain_otp group2 ret = %d!\n", ret);
	} else {
		pr_err("himalayanmain_otp invalid flag :0x%x\n",
				himalayanmain_txd_main_otp.ModuleFlag);
	}

	return ret;
}

static int himalayanmain_sensor_otp_read_awb_info(struct i2c_client *client, unsigned char *pinputdata)
{
	int ret = HIMALAYANMAIN_OTP_RET_FAIL;

	if(himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP1_FLAG){
		ret = himalayanmain_sensor_otp_read_data(client,
			HIMALAYANMAIN_OTP_AWB_GROUP1_STARTADDR,
			HIMALAYANMAIN_TXD_MAIN_OTP_AWB_LENS, pinputdata);
		pr_info("himalayanmain_otp group1 ret = %d!\n", ret);
	} else if(himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP2_FLAG) {
		ret = himalayanmain_sensor_otp_read_data(client,
			HIMALAYANMAIN_OTP_AWB_GROUP2_STARTADDR,
			HIMALAYANMAIN_TXD_MAIN_OTP_AWB_LENS, pinputdata);
		pr_info("himalayanmain_otp group2 ret = %d!\n", ret);
	} else
		pr_err("himalayanmain_otp invalid!\n");

	return ret;
}

static int himalayanmain_sensor_otp_read_lrc_info(struct i2c_client *client, unsigned char *pinputdata)
{
	int ret = HIMALAYANMAIN_OTP_RET_FAIL;

	if(himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP1_FLAG){
		ret = himalayanmain_sensor_otp_read_data(client,
			HIMALAYANMAIN_OTP_LRC_GROUP1_STARTADDR,
			HIMALAYANMAIN_TXD_MAIN_OTP_LRC_LENS, pinputdata);
		pr_info("himalayanmain_otp group1 ret = %d!\n", ret);
	} else if(himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP2_FLAG) {
		ret = himalayanmain_sensor_otp_read_data(client,
			HIMALAYANMAIN_OTP_LRC_GROUP2_STARTADDR,
			HIMALAYANMAIN_TXD_MAIN_OTP_LRC_LENS, pinputdata);
		pr_info("himalayanmain_otp group2 ret = %d!\n", ret);
	} else
		pr_err("himalayanmain_otp invalid!\n");

	return ret;
}

static int himalayanmain_sensor_otp_read_af_info(struct i2c_client *client, unsigned char *pinputdata)
{
	int ret = HIMALAYANMAIN_OTP_RET_FAIL;

	if(himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP1_FLAG){
		ret = himalayanmain_sensor_otp_read_data(client,
			HIMALAYANMAIN_OTP_AF_GROUP1_STARTADDR,
			HIMALAYANMAIN_TXD_MAIN_OTP_AF_LENS, pinputdata);
		pr_info("himalayanmain_otp group1 ret = %d!\n", ret);
	} else if(himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP2_FLAG) {
		ret = himalayanmain_sensor_otp_read_data(client,
			HIMALAYANMAIN_OTP_AF_GROUP2_STARTADDR,
			HIMALAYANMAIN_TXD_MAIN_OTP_AF_LENS, pinputdata);
		pr_info("himalayanmain_otp group2 ret = %d!\n", ret);
	} else
		pr_err("himalayanmain_otp invalid flag!\n");

	return ret;
}

static int himalayanmain_sensor_otp_read_lsc_info(struct i2c_client *client, unsigned char *pinputdata)
{
	int ret = HIMALAYANMAIN_OTP_RET_FAIL;

	if (himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP1_FLAG) {
		// lsc 1:
		himalayanmain_set_page_and_load_data(client, page_2);
		himalayanmain_read(client, 0x8317, 0x83FF, &himalayanmain_txd_main_otp.lsc_data[0]);

		// lsc 2:
		himalayanmain_set_page_and_load_data(client, page_3);
		himalayanmain_read(client, 0x847A, 0x85FF, &himalayanmain_txd_main_otp.lsc_data[0xE9]);

		// lsc 3:
		himalayanmain_set_page_and_load_data(client, page_4);
		himalayanmain_read(client, 0x867A, 0x87FF, &himalayanmain_txd_main_otp.lsc_data[0x26F]);

		// lsc 4:
		himalayanmain_set_page_and_load_data(client, page_5);
		himalayanmain_read(client, 0x887A, 0x89FF, &himalayanmain_txd_main_otp.lsc_data[0x3F5]);

		// lsc 5:
		himalayanmain_set_page_and_load_data(client, page_6);
		himalayanmain_read(client, 0x8A7A, 0x8BFF, &himalayanmain_txd_main_otp.lsc_data[0x57B]);

		// lsc 6:
		himalayanmain_set_page_and_load_data(client, page_7);
		himalayanmain_read(client, 0x8C7A, 0x8CC4, &himalayanmain_txd_main_otp.lsc_data[0x701]);

		// checksum
		ret = himalayanmain_checksum(client, 0x8CC6, &himalayanmain_txd_main_otp.lsc_data[0], 1868);
	} else if (himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP2_FLAG) {
		// lsc 1:
		himalayanmain_set_page_and_load_data(client, page_7);
		himalayanmain_read(client, 0x8CC7, 0x8DFF, &himalayanmain_txd_main_otp.lsc_data[0]);

		// lsc 2:
		himalayanmain_set_page_and_load_data(client, page_8);
		himalayanmain_read(client, 0x8E7A, 0x8FFF, &himalayanmain_txd_main_otp.lsc_data[0x139]);

		// lsc 3:
		himalayanmain_set_page_and_load_data(client, page_9);
		himalayanmain_read(client, 0x907A, 0x91FF, &himalayanmain_txd_main_otp.lsc_data[0x2BF]);

		// lsc 4:
		himalayanmain_set_page_and_load_data(client, page_10);
		himalayanmain_read(client, 0x927A, 0x93FF, &himalayanmain_txd_main_otp.lsc_data[0x445]);

		// lsc 5:
		himalayanmain_set_page_and_load_data(client, page_11);
		himalayanmain_read(client, 0x947A, 0x95FA, &himalayanmain_txd_main_otp.lsc_data[0x5CB]);

		// checksum
		ret = himalayanmain_checksum(client, 0x95FC, &himalayanmain_txd_main_otp.lsc_data[0], 1868);
	} else {
		pr_info("dbgmsg - read lsc error");
		// err
	}

	return ret;
}

static int himalayanmain_sensor_otp_read_sn_info(struct i2c_client *client, unsigned char *pinputdata)
{
	int ret = HIMALAYANMAIN_OTP_RET_FAIL;

	if (himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP1_FLAG) {
		ret = himalayanmain_sensor_otp_read_data(client,
			HIMALAYANMAIN_OTP_SN_GROUP1_STARTADDR,
			HIMALAYANMAIN_TXD_MAIN_OTP_SN_LENS, pinputdata);
		pr_info("himalayanmain_otp group1 ret = %d!\n", ret);
	} else if (himalayanmain_txd_main_otp.ModuleFlag == HIMALAYANMAIN_GROUP2_FLAG) {
		ret = himalayanmain_sensor_otp_read_data(client,
			HIMALAYANMAIN_OTP_SN_GROUP2_STARTADDR,
			HIMALAYANMAIN_TXD_MAIN_OTP_SN_LENS, pinputdata);
		pr_info("himalayanmain_otp group2 ret = %d!\n", ret);
	} else
		pr_err("himalayanmain_otp sn_info invalid!\n");

	return ret;
}

static int himalayanmain_sensor_otp_read_by_group(struct i2c_client *client)
{
	int threshold = 0;
	int ret = HIMALAYANMAIN_OTP_RET_FAIL;

	for (threshold = 0; threshold < 3; threshold++) {
		himalayanmain_set_threshold(client, threshold);
		himalayanmain_set_page_and_load_data(client, page_2);
		ret = himalayanmain_sensor_otp_read_module_info(client,
			himalayanmain_txd_main_otp.module_info);
		if (ret == HIMALAYANMAIN_OTP_RET_FAIL) {
			himalayanmain_txd_main_otp.ModuleFlag = HIMALAYANMAIN_INVALID_FLAG;
			pr_err("himalayanmain_otp read module info in threshold R%d fail\n", threshold);
			continue;
		}

		ret = himalayanmain_sensor_otp_read_awb_info(client,
			himalayanmain_txd_main_otp.wb_data);
		if (ret == HIMALAYANMAIN_OTP_RET_FAIL) {
			pr_err("himalayanmain_otp read awb info in threshold R%d fail\n", threshold);
			continue;
		}

		ret = himalayanmain_sensor_otp_read_lrc_info(client,
			himalayanmain_txd_main_otp.lrc_data);
		if (ret == HIMALAYANMAIN_OTP_RET_FAIL) {
			pr_err("himalayanmain_otp read light source in threshold R%d fail\n", threshold);
			continue;
		}

		ret = himalayanmain_sensor_otp_read_af_info(client,
			himalayanmain_txd_main_otp.af_data);
		if (ret == HIMALAYANMAIN_OTP_RET_FAIL) {
			pr_err("himalayanmain_otp read af info in threshold R%d fail\n", threshold);
			continue;
		}

		ret = himalayanmain_sensor_otp_read_sn_info(client,
			himalayanmain_txd_main_otp.sn_data);
		if (ret == HIMALAYANMAIN_OTP_RET_FAIL) {
			pr_err("himalayanmain_otp read sn_data in threshold R%d fail\n", threshold);
			continue;
		}

		ret = himalayanmain_sensor_otp_read_lsc_info(client,
			himalayanmain_txd_main_otp.lsc_data);
		if (ret == HIMALAYANMAIN_OTP_RET_FAIL) {
			pr_err("himalayanmain_otp read lsc info in threshold R%d fail\n", threshold);
			continue;
		}

		pr_info("himalayanmain_otp read all otp data in threshold R%d success\n", threshold);
		break;
	}
	if (ret == HIMALAYANMAIN_OTP_RET_FAIL) {
		pr_err("himalayanmain_otp read otp data  in threshold R1 R2 R3 all failed!\n");
	}

	return ret;
}

int himalayanmain_sensor_otp_read_all_data(struct i2c_client *client)
{
	int ret = HIMALAYANMAIN_OTP_RET_FAIL;
	int delay = 0;
	client->addr = OTP_I2C_ADDR;

	if (subdrv_i2c_rd_u8(client, 0x3107) == 0xff) {
		pr_err("i2c tran err!\n");
		return ret;
	}

	ret = himalayanmain_sensor_otp_read_by_group(client);
	if (ret == HIMALAYANMAIN_OTP_RET_FAIL) {
		pr_err("himalayanmain_otp read lsc info in threshold R1 R2 R3 all failed!!!\n");

		return ret;
	}
	pr_info("himalayanmain_otp read otp data success\n");

	subdrv_i2c_wr_u8(client, 0x4408, 0x80);
	subdrv_i2c_wr_u8(client, 0x4409, 0x00);
	subdrv_i2c_wr_u8(client, 0x440a, 0x81);
	subdrv_i2c_wr_u8(client, 0x440b, 0xff);

	subdrv_i2c_wr_u8(client, 0x4401, 0x13);
	subdrv_i2c_wr_u8(client, 0x4412, 0x1);
	subdrv_i2c_wr_u8(client, 0x4407, 0x0e);
	subdrv_i2c_wr_u8(client, 0x4400, 0x11);

	while ((subdrv_i2c_rd_u8(client, 0x4420) & 0x01) == 0x01) {
		delay++;
		pr_info("himalayanmain_otp read otp is waitting, OTP is still busy for loading %d times\n", delay);
		if (delay == 10) {
			pr_err("himalayanmain_otp read otp data fail, load timeout!\n");

			return HIMALAYANMAIN_OTP_RET_FAIL;
		}
		mdelay(10);
	}

	return ret;
}
EXPORT_SYMBOL(himalayanmain_sensor_otp_read_all_data);

static unsigned int layout_check_himamain(struct EEPROM_DRV_FD_DATA *pdata, unsigned int sensorID, unsigned int *_cfg)
{
	// struct STRUCT_CAM_CAL_CONFIG_STRUCT *cfg =
	// 			(struct STRUCT_CAM_CAL_CONFIG_STRUCT *)_cfg;
	unsigned int header_offset = cam_cal_config->layout->header_addr;
	unsigned int check_id = 0x00000000;
	unsigned int result = CAM_CAL_ERR_NO_DEVICE;
	struct i2c_client *client;

	if (cam_cal_config->sensor_id == sensorID)
		pr_info("%s sensor_id matched\n", cam_cal_config->name);
	else {
		pr_info("%s sensor_id not matched\n", cam_cal_config->name);
		return result;
	}

	if (pdata->pdrv->pi2c_client != NULL) {
		client = pdata->pdrv->pi2c_client;
	} else {
		client = NULL;
		pr_err("pdata client is null!\n");
		return result;
	}

	if (himalayanmain_txd_main_otp.ModuleFlag == 0) {
		pr_info("read sensor otp!\n");
		himalayanmain_sensor_otp_read_all_data(client);
	}

	memcpy(&check_id, &himalayanmain_txd_main_otp.module_info[header_offset], 4);

	if (check_id == cam_cal_config->layout->header_id) {	// hearder id on OTP guide
		pr_info("header_id matched 0x%08x\n", check_id);
		result = CAM_CAL_ERR_NO_ERR;
	} else{
		pr_info("header_id not matched 0x%08x\n", check_id);
	}
	return result;
}


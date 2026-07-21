/*
 * Copyright (C) 2024 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*****************************************************************************
 *
 * Filename:
 * ---------
 *	 himalayanmain_otp.h
 *
 * Project:
 * --------
 *	 ALPS
 *
 * Description:
 * ------------
 *	 sensor otp header file
 *
 ****************************************************************************/
#ifndef __HIMALAYANMAIN_OTP_H
#define __HIMALAYANMAIN_OTP_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_errcode.h"
#include "oplus-adaptor-subdrv-ctrl.h"
#include "adaptor-subdrv-ctrl.h"
#include "adaptor-i2c.h"
#include "adaptor.h"

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
	UINT8 ModuleFlag;
	UINT8 module_info[HIMALAYANMAIN_TXD_MAIN_OTP_MODULE_LENS];
	UINT8 wb_data[HIMALAYANMAIN_TXD_MAIN_OTP_AWB_LENS];
	UINT8 lrc_data[HIMALAYANMAIN_TXD_MAIN_OTP_LRC_LENS];
	UINT8 af_data[HIMALAYANMAIN_TXD_MAIN_OTP_AF_LENS];
	UINT8 sn_data[HIMALAYANMAIN_TXD_MAIN_OTP_SN_LENS];
	UINT8 lsc_data[HIMALAYANMAIN_TXD_MAIN_OTP_LSC_LENS];
};

extern struct himalayanmain_txd_main_otp_struct himalayanmain_txd_main_otp;
extern int himalayanmain_sensor_otp_read_all_data(struct i2c_client *client);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2024 Oplus. All rights reserved.
 */

#define ds28e30
#include "ds28e30.h"

#include <linux/slab.h> /* kfree() */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <oplus_chg_module.h>
#include "1wire_protocol.h"
#include "deep_cover_coproc.h"
#include "ecdsa_generic_api.h"
#include "ucl_defs.h"
#include "ucl_sha256.h"
#include "ecc_generate_key.h"
#include "oplus_charger.h"

static int e30_test = BYTE_LENGTH_0;
module_param(e30_test, int, 0644);
MODULE_PARM_DESC(e30_test, "debug e30");

/* define retry times if an testing error is experienced */
#define RETRY_NUMBER   5
/* define testing number */
#define TESTING_ITEM_NUMBER  17
/* define testing item result */
#define FAMILY_CODE_RESULT    0
/* custom ID is special for each mobile maker */
#define CUSTOM_ID_RESULT 1
#define UNIQUE_ID_RESULT  2
#define MAN_ID_RESULT  3
#define STATUS_RESULT     4
#define PAGE0_RESULT  5
#define PAGE1_RESULT  6
#define PAGE2_RESULT  7
#define PAGE3_RESULT  8
#define COUNTERVALUE_RESULT 9
#define VERIFICATION_SIGNATURE_RESULT  10
#define VERIFICATION_CERTIFICATE_RESULT  11
#define PROGRAM_PAGE0_RESULT  12
#define PROGRAM_PAGE1_RESULT  13
#define PROGRAM_PAGE2_RESULT  14
#define PROGRAM_PAGE3_RESULT  15
#define DECREASINGCOUNTERVALUE_RESULT 16

unsigned char TestingItemResult[TESTING_ITEM_NUMBER];   /* maximal testing items */

/* define specific definition for general-purpose DS28E30 if it's used for OPPO */
/* definintion for MI */
#define GP_CID_LSB                         0x00
#define GP_CID_MSB                         0x00
#define GP_MAN_ID_LSB                      0x00
#define GP_MAN_ID_MSB                      0x00
#define GP_CounterValue_LSB                0xFF
#define GP_CounterValue_2LSB               0xFF
#define GP_CounterValue_MSB                0xFF

/* define constant for generating certificate */
/* for general-purpose DS28E30 */
/* TODO: use nvt battery system publickey */
/* define constant for generating/verifying default  */
/* certificate of generic DS28E30 (T0 project stage) */

unsigned char gp_certificate_constant[BYTE_LENGTH_16] = {0xEC, 0x81, 0x75, 0x28, 0x11, 0x24, 0x0D, 0x6F, 0x9F,
					0x30, 0xC8, 0x83, 0x0B, 0xFF, 0x53, 0xA0};

unsigned char gp_system_public_key_x[BYTE_LENGTH_32]= {0x2E, 0x75, 0x76, 0xB1, 0x34, 0x3E, 0xF4, 0xE4,
					0xFB, 0x93, 0x69, 0x79, 0x2E, 0x7A, 0x2E, 0x83,
					0x97, 0x58, 0x14, 0xCA, 0x49, 0x95, 0x84, 0x84,
					0xD7, 0xFA, 0x3E, 0xB7, 0xA0, 0x65, 0x7C, 0x5C};
unsigned char gp_system_public_key_y[BYTE_LENGTH_32]= {0x69, 0xC9, 0x37, 0xF4, 0xE0, 0x6E, 0x37, 0x1D,
					0xAF, 0x17, 0x52, 0x49, 0xF7, 0xD5, 0xCF, 0x4D,
					0x5C, 0xDF, 0x4F, 0xD2, 0x21, 0x0D, 0x20, 0x53,
					0x2D, 0x17, 0xA9, 0xF3, 0xBB, 0x08, 0x2B, 0xD2};
unsigned char  gp_authority_private_key[BYTE_LENGTH_32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
unsigned char  gp_authority_public_key_x[BYTE_LENGTH_32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
unsigned char  gp_authority_public_key_y[BYTE_LENGTH_32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

unsigned char GP_PageProtectionStatus[11]={0x00, 0, 0, 0, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x03};

/* define specific definition for each customer */
/* definintion for OPPO */
#define OPPO_CID_LSB                         0x10
#define OPPO_CID_MSB                         0x06
#define OPPO_MAN_ID_LSB                      0xEC
#define OPPO_MAN_ID_MSB                      0x00
#define OPPO_CounterValue_LSB                0xFF
#define OPPO_CounterValue_2LSB               0xFF
#define OPPO_CounterValue_MSB                0xFF

/* define constant for generating certificate */
unsigned char certificate_constant[BYTE_LENGTH_16] = {0xA5, 0xDB, 0x67, 0xD0, 0xD6, 0x7A, 0x7A, 0xBF, 0x65,
					0x1B, 0x47, 0xF5, 0x59, 0xD7, 0xFE, 0x1A};
/* 32-byte system-level public key X */
unsigned char system_public_key_x[BYTE_LENGTH_32] = {0xDF, 0x47, 0x0F, 0xA1, 0xE3, 0xDB, 0xB9, 0x19,
					0x47, 0x33, 0xB0, 0x36, 0xCB, 0x83, 0x0A, 0x59,
					0x6D, 0xED, 0x66, 0xE6, 0x44, 0xB8, 0xC7, 0x89,
					0xE1, 0xA4, 0x1C, 0x1B, 0x0F, 0x33, 0xF5, 0xD0};
/* 32-byte system-level public key Y */
unsigned char system_public_key_y[BYTE_LENGTH_32] = {0x34, 0xB5, 0x54, 0xB1, 0x40, 0x9E, 0x95, 0x06,
					0x4B, 0x41, 0xBD, 0xCF, 0x60, 0x39, 0x65, 0x9A,
					0x3B, 0xDB, 0x0C, 0x98, 0xFD, 0x75, 0x7A, 0x11,
					0xB8, 0xC6, 0xF8, 0x85, 0x02, 0xE5, 0x75, 0xA3};

unsigned char  authority_private_key[BYTE_LENGTH_32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
unsigned char  authority_public_key_x[BYTE_LENGTH_32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
unsigned char  authority_public_key_y[BYTE_LENGTH_32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

unsigned char page_protection_status[BYTE_LENGTH_11] = {0x02, 0, 0, 0, 0x02, 0x02, 0x00,
					0x00, 0x02, 0x02, 0x03};
/* end definition for OPPO */

void configure_ds28e30_parameters(void);

/* define system-level publick key, authority public key  and certificate constant variables */
unsigned char system_public_key_x[BYTE_LENGTH_32];
unsigned char system_public_key_y[BYTE_LENGTH_32];
unsigned char authority_private_key[BYTE_LENGTH_32];
unsigned char authority_public_key_x[BYTE_LENGTH_32];
unsigned char authority_public_key_y[BYTE_LENGTH_32];
unsigned char certificate_constant[BYTE_LENGTH_16];
unsigned char expected_CID[BYTE_LENGTH_2];
unsigned char expected_main_id[BYTE_LENGTH_2];
unsigned char expected_page_protection_status[BYTE_LENGTH_11];

/* Command Functions (no high level verification) */
int ds28e30_cmd_write_memory(int pg, uchar *data);
int ds28e30_cmd_read_memory(int pg, uchar *data);
int ds28e30_cmd_read_status(int pg, uchar *pr_data, uchar *manid, uchar *hardware_version);
int ds28e30_cmd_set_page_protection(int pg, uchar prot);
int ds28e30_cmd_compute_read_page_authentication(int pg, int anon, uchar *challenge, uchar *sig);
int ds28e30_cmd_decrement_counter(void);
int ds28e30_cmd_device_disable(uchar *release_sequence);
int ds28e30_cmd_authendicated_ecdsa_write_memory(int pg, uchar *data, uchar *sig_r, uchar *sig_s);

/* High level functions */
int ds28e30_cmd_read_device_public_key(uchar *data);
int ds28e30_compute_and_verify_ecdsa(int pg, int anon, uchar *mempage, uchar *challenge,
				uchar *sig_r, uchar *sig_s);
int ds28e30_compute_and_verify_ecdsa_no_read(int pg, int anon, uchar *mempage, uchar *challenge,
				uchar *sig_r, uchar *sig_s);
/* DS28E30 application functions */
int verify_ecdsa_certificate_of_device(uchar *sig_r, uchar *sig_s, uchar *pub_key_x,
				uchar *pub_key_y, uchar *slave_romid, uchar *slave_manid,
				uchar *system_level_pub_key_x, uchar *system_level_pub_key_y);
int ds28e30_write_memory_page_with_ECW(int pg, uchar *new_data);

/* Helper functions */
int standard_cmd_flow(uchar *write_buf, int write_len, int delay_ms, int expect_read_len,
			uchar *read_buf, int *read_len);
uchar ds28e30_get_last_result_byte(void);
void ds28e30_set_public_key(uchar *px, uchar *py);
void ds28e30_set_private_key(uchar *priv);
int ds28e30_read_romno_manid_hardware_version(void);
unsigned short docrc16(unsigned short data);
static unsigned short crc16;

/* int OWBlock(unsigned char *tran_buf, int tran_len); */
int ow_read_rom(void);
int ow_skip_rom(void);

/* misc utility functions */
unsigned char docrc8(unsigned char value);

/* ecdsa algorithm achieved by software */
int  sw_compute_ecdsa_signature(uchar *message, int msg_len, uchar *sig_r, uchar *sig_s);

/* keys in byte array format, used by software compute functions */
uchar private_key[BYTE_LENGTH_32];
uchar public_key_x[BYTE_LENGTH_32];
uchar public_key_y[BYTE_LENGTH_32];

/* ds28e30 state */
static uchar crc8;
uchar rom_no[BYTE_LENGTH_8];
uchar mian_id[BYTE_LENGTH_2];
uchar hardware_version[BYTE_LENGTH_2];

/* last result byte */
uchar last_result_byte = RESULT_SUCCESS;

extern void maxim_delay_ms(unsigned int delay_ms);

#ifdef SPIN_LOCK_ENABLE
struct mutex ds_cmd_lock;
#endif

/* ds28e30 Memory functions
 'Write Memory' command
 @param[in] pg
 page number to write
 @param[in] data
 buffer must be at least 32 bytes
 @return
 TRUE - command successful @n
 FALSE - command failed
 */
int ds28e30_cmd_write_memory(int pg, uchar *data)
{
	uchar write_buf[BYTE_LENGTH_50];
	int write_len;
	uchar read_buf[BYTE_LENGTH_255];
	int read_len;

	/*
		Reset
		Presence Pulse
		<ROM Select>
		TX: XPC Command (66h)
		TX: Length byte 34d
		TX: XPC sub-command 96h (Write Memory)
		TX: Parameter
		TX: New page data (32d bytes)
		RX: crc16 (inverted of XPC command, length, sub-command, parameter)
		TX: Release Byte
		<Delay TBD>
		RX: Dummy Byte
		RX: Length Byte (1d)
		RX: Result Byte
		RX: crc16 (inverted of length and result byte)
		Reset or send XPC command (66h) for a new sequence
	*/

	/* construct the write buffer */
	write_len = ZERO_VALUE;
	write_buf[write_len++] = CMD_WRITE_MEM;
	write_buf[write_len++] = pg;
	memcpy(&write_buf[write_len], data, BYTE_LENGTH_32);
	write_len += BYTE_LENGTH_32;

	/* preload read_len with expected length */
	read_len = EXPECTED_READ_LENGTH_1;

	/* default failure mode */
	last_result_byte = RESULT_FAIL_COMMUNICATION;

	if (standard_cmd_flow(write_buf, write_len, DELAY_DS28E30_EE_WRITE_TWM, read_len, read_buf,
				&read_len)) {
		/* get result byte */
		last_result_byte = read_buf[BYTE_LENGTH_0];
		/* check result */
		if (read_len == EXPECTED_READ_LENGTH_1)
			return (read_buf[BYTE_LENGTH_0] == RESULT_SUCCESS);
	}

	/* no payload in read buffer or failed command */
	return FALSE;
}

/*  'Read Memory' command
 @param[in] pg
 page number to read
 @param[out] data
 buffer length must be at least 32 bytes to hold memory read
 @return
 TRUE - command successful @n
 FALSE - command failed
 */
int ds28e30_cmd_read_memory(int pg, uchar *data)
{
	uchar write_buf[BYTE_LENGTH_10];
	int write_len;
	uchar read_buf[BYTE_LENGTH_255];
	int read_len;

	/*
		Reset
		Presence Pulse
		<ROM Select>
		TX: XPC Command (66h)
		TX: Length byte 2d
		TX: XPC sub-command 69h (Read Memory)
		TX: Parameter (page)
		RX: crc16 (inverted of XPC command, length, sub-command, and parameter)
		TX: Release Byte
		<Delay TBD>
		RX: Dummy Byte
		RX: Length (33d)
		RX: Result Byte
		RX: Read page data (32d bytes)
		RX: crc16 (inverted, length byte, result byte, and page data)
		Reset or send XPC command (66h) for a new sequence
	*/

	/* construct the write buffer */
	write_len = ZERO_VALUE;
	write_buf[write_len++] = CMD_READ_MEM;
	write_buf[write_len++] = pg;

	/* preload read_len with expected length */
	read_len = EXPECTED_READ_LENGTH_33;

	/* default failure mode */
	last_result_byte = RESULT_FAIL_COMMUNICATION;

	if (standard_cmd_flow(write_buf, write_len, DELAY_DS28E30_EE_READ_TRM, read_len, read_buf,
				&read_len)) {
		/* get result byte */
		last_result_byte = read_buf[BYTE_LENGTH_0];
		/* check result */
		if (read_len == EXPECTED_READ_LENGTH_33) {
			if (read_buf[BYTE_LENGTH_0] == RESULT_SUCCESS) {
				memcpy(data, &read_buf[1], BYTE_LENGTH_32);
				return TRUE;
			}
		}
	}

	/* no payload in read buffer or failed command */
	return FALSE;
}

/*  'Read Status' command
 @param[in] pg
 page to read protection
 @param[out] pr_data
 pointer to uchar buffer of length 6 for page protection data
 @param[out] manid
 pointer to uchar buffer of length 2 for manid (manufactorur ID)
 @return
 TRUE - command successful @n
 FALSE - command failed
 */

int ds28e30_cmd_read_status(int pg, uchar *pr_data, uchar *manid, uchar *hardware_version)
{
	uchar write_buf[BYTE_LENGTH_10];
	uchar read_buf[BYTE_LENGTH_255];
	int read_len = EXPECTED_READ_LENGTH_2, write_len;

	/*
		Reset
		Presence Pulse
		<ROM Select>
		TX: XPC Command (66h)
		TX: Length byte 1d
		TX: XPC sub-command AAh (Read Status)
		RX: crc16 (inverted of XPC command, length, and sub-command)
		TX: Release Byte
		<Delay TBD>
		RX: Dummy Byte
		RX: Length Byte (11d)
		RX: Result Byte
		RX: Read protection values (6 Bytes), mian_id (2 Bytes), ROM VERSION (2 bytes)
		RX: crc16 (inverted, length byte, protection values, mian_id, ROM_VERSION)
		Reset or send XPC command (66h) for a new sequence
	*/

	/* construct the write buffer */
	write_len = ZERO_VALUE;
	write_buf[write_len++] = CMD_READ_STATUS;
	write_buf[write_len++] = pg;

	/* preload read_len with expected length */
	if (pg & MSB_CHECK)
		read_len = EXPECTED_READ_LENGTH_5;

	/* default failure mode */
	last_result_byte = RESULT_FAIL_COMMUNICATION;
	/*
	return standard_cmd_flow(write_buf, write_len,
		DELAY_DS28E30_EE_READ_TRM, read_len, read_buf, &read_len);
	*/

	if (standard_cmd_flow(write_buf, write_len, DELAY_DS28E30_EE_READ_TRM, read_len, read_buf,
				&read_len)) {
		/* get result byte */
		last_result_byte = read_buf[BYTE_LENGTH_0];
		/* should always be 2 or 5 length for status data */
		if (read_len == EXPECTED_READ_LENGTH_2 || read_len == EXPECTED_READ_LENGTH_5) {
			if (read_buf[BYTE_LENGTH_0] == RESULT_SUCCESS || read_buf[BYTE_LENGTH_0] == RESULT_DEVICE_DISABLED) {
				if (read_len == EXPECTED_READ_LENGTH_2) {
					memcpy(pr_data, &read_buf[BYTE_LENGTH_1], BYTE_LENGTH_1);
				} else {
					memcpy(manid, &read_buf[BYTE_LENGTH_1], BYTE_LENGTH_2);
					memcpy(hardware_version, &read_buf[BYTE_LENGTH_3], BYTE_LENGTH_2);
				}
				return TRUE;
			}
		}
	}

	/* no payload in read buffer or failed command */
	return FALSE;
}

/*  'Set Page Protection' command
 @param[in] pg
 page to set protection
 @param[in] prot
 protection value to set
 @return
 TRUE - command successful @n
 FALSE - command failed
 */
int ds28e30_cmd_set_page_protection(int pg, uchar prot)
{
	uchar write_buf[BYTE_LENGTH_10];
	int write_len;
	uchar read_buf[BYTE_LENGTH_255];
	int read_len;

	/*
		Reset
		Presence Pulse
		<ROM Select>
		TX: XPC Command (66h)
		TX: Length byte 3d
		TX: XPC sub-command C3h (Set Protection)
		TX: Parameter (page)
		TX: Parameter (protection)
		RX: crc16 (inverted of XPC command, length, sub-command, parameters)
		TX: Release Byte
		<Delay TBD>
		RX: Dummy Byte
		RX: Length Byte (1d)
		RX: Result Byte
		RX: crc16 (inverted, length byte and result byte)
		Reset or send XPC command (66h) for a new sequence
	*/

	/* construct the write buffer */
	write_len = ZERO_VALUE;
	write_buf[write_len++] = CMD_SET_PAGE_PROT;
	write_buf[write_len++] = pg;
	write_buf[write_len++] = prot;

	/* preload read_len with expected length */
	read_len = EXPECTED_READ_LENGTH_1;

	/* default failure mode */
	last_result_byte = RESULT_FAIL_COMMUNICATION;
	if (standard_cmd_flow(write_buf, write_len, DELAY_DS28E30_EE_WRITE_TWM, read_len, read_buf,
				&read_len)) {
		/* get result byte */
		last_result_byte = read_buf[BYTE_LENGTH_0];
		/* check result */
		if (read_len == EXPECTED_READ_LENGTH_1)
			return (read_buf[BYTE_LENGTH_0] == RESULT_SUCCESS);
	}

	/* no payload in read buffer or failed command */
	return FALSE;
}

/*  'Compute and Read Page Authentication' command
 @param[in] pg - page number to compute auth on
 @param[in] anon - anonymous flag (1) for anymous
 @param[in] challenge
 buffer length must be at least 32 bytes containing the challenge
 @param[out] data
 buffer length must be at least 64 bytes to hold ecdsa signature
 @return
 TRUE - command successful @n
 FALSE - command failed
 */
int ds28e30_cmd_compute_read_page_authentication(int pg, int anon, uchar *challenge, uchar *sig)
{
	uchar write_buf[BYTE_LENGTH_200];
	int write_len;
	uchar read_buf[BYTE_LENGTH_255];
	int read_len;

	/*
		Reset
		Presence Pulse
		<ROM Select>
		TX: XPC Command (66h)
		TX: Length byte 34d
		TX: XPC sub-command A5h (Compute and Read Page Authentication)
		TX: Parameter (page)
		TX: Challenge (32d bytes)
		RX: crc16 (inverted of XPC command, length, sub-command, parameter, and challenge)
		TX: Release Byte
		<Delay TBD>
		RX: Dummy Byte
		RX: Length byte (65d)
		RX: Result Byte
		RX: Read ecdsa Signature (64 bytes, �s� and then �r�,
			MSByte first, [same as ES10]),
			signature 00h's if result byte is not AA success
		RX: crc16 (inverted, length byte, result byte, and signature)
		Reset or send XPC command (66h) for a new sequence
	*/

	/* construct the write buffer */
	write_len = ZERO_VALUE;
	write_buf[write_len++] = CMD_COMP_READ_AUTH;
	write_buf[write_len] = pg & BYTE_VALUE_7F;
	if (anon)
	write_buf[write_len] |= BYTE_VALUE_E0;
	write_len++;
	write_buf[write_len++] = BYTE_VALUE_03; /* authentication parameter */
	memcpy(&write_buf[write_len], challenge, BYTE_LENGTH_32);
	write_len += BYTE_LENGTH_32;

	/* preload read_len with expected length */
	read_len = EXPECTED_READ_LENGTH_65;

	/* default failure mode */
	last_result_byte = RESULT_FAIL_COMMUNICATION;

	if (standard_cmd_flow(write_buf, write_len, DELAY_DS28E30_ECDSA_GEN_TGES, read_len, read_buf,
				&read_len)) {
		/* get result byte */
		last_result_byte = read_buf[BYTE_LENGTH_0];
		/* check result */
		if (read_len == EXPECTED_READ_LENGTH_65) {
			if (read_buf[BYTE_LENGTH_0] == RESULT_SUCCESS) {
				memcpy(sig, &read_buf[1], BYTE_LENGTH_64);
				return TRUE;
			}
		}
	}

	/* no payload in read buffer or failed command */
	return FALSE;
}

/*  'Decrement Counter' command
  @return
  TRUE - command successful @n
  FALSE - command failed
 */
int ds28e30_cmd_decrement_counter(void)
{
	int write_len;
	uchar write_buf[BYTE_LENGTH_10];
	uchar read_buf[BYTE_LENGTH_255];
	int read_len;

	/*
		Presence Pulse
		<ROM Select>
		TX: XPC Command (66h)
		TX: Length byte 1d
		TX: XPC sub-command C9h (Decrement Counter)
		RX: crc16 (inverted of XPC command, length, sub-command)
		TX: Release Byte
		<Delay TBD>
		RX: Dummy Byte
		RX: Length Byte (1d)
		RX: Result Byte
		RX: crc16 (inverted, length byte and result byte)
		Reset or send XPC command (66h) for a new sequence
	*/

	/* construct the write buffer */
	write_len = ZERO_VALUE;
	write_buf[write_len++] = CMD_DECREMENT_CNT;

	/* preload read_len with expected length */
	read_len = EXPECTED_READ_LENGTH_1;

	/* default failure mode  */
	last_result_byte = RESULT_FAIL_COMMUNICATION;

	if (standard_cmd_flow(write_buf, write_len, DELAY_DS28E30_EE_WRITE_TWM, read_len,
				read_buf, &read_len)) {
		/* get result byte */
		last_result_byte = read_buf[BYTE_LENGTH_0];
		/* check result byte */
		if (read_len == EXPECTED_READ_LENGTH_1)
			return (read_buf[BYTE_LENGTH_0] == RESULT_SUCCESS);
	}

	/* no payload in read buffer or failed command */
	return FALSE;
}

/*  'Device Disable' command
 @param[in] release_sequence
 8 byte release sequence to disable device
  @return
  TRUE - command successful @n
  FALSE - command failed
 */
int ds28e30_cmd_device_disable(uchar *release_sequence)
{
	uchar write_buf[BYTE_LENGTH_10];
	int write_len;
	uchar read_buf[BYTE_LENGTH_255];
	int read_len;

	/*
		Reset
		Presence Pulse
		<ROM Select>
		TX: XPC Command (66h)
		TX: Length byte 9d
		TX: XPC sub-command 33h (Disable command)
		TX: Release Sequence (8 bytes)
		RX: crc16 (inverted of XPC command, length, sub-command, and release sequence)
		TX: Release Byte
		<Delay TBD>
		RX: Dummy Byte
		RX: Length Byte (1d)
		RX: Result Byte
		RX: crc16 (inverted, length byte and result byte)
		Reset or send XPC command (66h) for a new sequence
	*/

	/* construct the write buffer */
	write_len = ZERO_VALUE;
	write_buf[write_len++] = CMD_DISABLE_DEVICE;
	memcpy(&write_buf[write_len], release_sequence, BYTE_LENGTH_8);
	write_len += BYTE_LENGTH_8;

	/* preload read_len with expected length */
	read_len = EXPECTED_READ_LENGTH_1;

	/* default failure mode  */
	last_result_byte = RESULT_FAIL_COMMUNICATION;

	if (standard_cmd_flow(write_buf, write_len, DELAY_DS28E30_EE_WRITE_TWM, read_len, read_buf,
				&read_len)) {
		/* get result byte */
		last_result_byte = read_buf[BYTE_LENGTH_0];
		/* check result */
		if (read_len == EXPECTED_READ_LENGTH_1)
			return (read_buf[BYTE_LENGTH_0] == RESULT_SUCCESS);
	}

	/* no payload in read buffer or failed command */
	return FALSE;
}

/*  'Read device public key' command
 @param[in]
 no param required
 @param[out] data
 buffer length must be at least 64 bytes to hold device public key read
  @return
  TRUE - command successful @n
  FALSE - command failed */

int ds28e30_cmd_read_device_public_key(uchar *data)
{
	if ((ds28e30_cmd_read_memory(PG_DS28E30_PUB_KEY_X, data)) ==false)
		return FALSE;
	if ((ds28e30_cmd_read_memory(PG_DS28E30_PUB_KEY_Y, data+BYTE_LENGTH_32)) ==false)
		return FALSE;
	return TRUE;
}

/*  'authenticated Write memory' command
 @param[in] pg
 page number to write
 @param[in] data
 buffer must be at least 32 bytes
 @param[in] certificate sig_r
 @param[in] certificate sig_s
  @return
  TRUE - command successful @n
  FALSE - command failed */

int ds28e30_cmd_authendicated_ecdsa_write_memory(int pg, uchar *data, uchar *sig_r, uchar *sig_s)
{
	uchar write_buf[128];
	int write_len;
	uchar read_buf[BYTE_LENGTH_16];
	int read_len;

	/*
		Reset
		Presence Pulse
		<ROM Select>
		TX: XPC Command (66h)
		TX: Length byte 98d
		TX: XPC sub-command 89h (authenticated Write Memory)
		TX: Parameter
		TX: New page data (32d bytes)
		TX: Certificate R&S (64 bytes)
		RX: crc16 (inverted of XPC command, length, sub-command,
			parameter, page data, certificate R&S)
		TX: Release Byte
		<Delay TBD>
		RX: Dummy Byte
		RX: Length Byte (1d)
		RX: Result Byte
		RX: crc16 (inverted of length and result byte)
		Reset or send XPC command (66h) for a new sequence
	*/

	/* construct the write buffer */
	write_len = BYTE_LENGTH_0;
	write_buf[write_len++] = CMD_AUTHENTICATE_WRITE;
	write_buf[write_len++] = pg & BYTE_VALUE_03;
	memcpy(&write_buf[write_len], data, BYTE_LENGTH_32);
	write_len += BYTE_LENGTH_32;
	memcpy(&write_buf[write_len], sig_r, BYTE_LENGTH_32);
	write_len += BYTE_LENGTH_32;
	memcpy(&write_buf[write_len], sig_s, BYTE_LENGTH_32);
	write_len += BYTE_LENGTH_32;


	/* preload read_len with expected length */
	read_len = EXPECTED_READ_LENGTH_1;

	/* default failure mode */
	last_result_byte = RESULT_FAIL_COMMUNICATION;

	if (standard_cmd_flow(write_buf, write_len,
			DELAY_DS28E30_EE_WRITE_TWM + DELAY_DS28E30_VERIFY_ECDSA_SIGNATURE_TEVS,
			read_len, read_buf, &read_len)) {
		/* get result byte */
		last_result_byte = read_buf[BYTE_LENGTH_0];
		/* check result */
		if (read_len == EXPECTED_READ_LENGTH_1)
			return (read_buf[BYTE_LENGTH_0] == RESULT_SUCCESS);
	}

	/* no payload in read buffer or failed command */
	return FALSE;
}

/*  High level function to do a full challenge/response ecdsa operation
 on specified page
 @param[in] pg
 page to do operation on
 @param[in] anon
 flag to indicate in anonymous mode (1) or not anonymous (0)
 @param[out] mempage
 buffer to return the memory page contents
 @param[in] challenge
 buffer containing challenge, must be 32 bytes
 @param[out] sig_r
 buffer for r portion of signature, must be 32 bytes
 @param[out] sig_s
 buffer for s portion of signature, must be 32 bytes
 @return
 TRUE - command successful @n
 FALSE - command failed
 */
int ds28e30_compute_and_verify_ecdsa(int pg, int anon, uchar *mempage, uchar *challenge,
			uchar *sig_r, uchar *sig_s)
{
	/* read destination page */
	if (!ds28e30_cmd_read_memory(pg, mempage))
		return FALSE;

	return ds28e30_compute_and_verify_ecdsa_no_read(pg, anon, mempage, challenge, sig_r, sig_s);
}

/* High level function to do a full challenge/response ecdsa operation
 on specified page
 @param[in] pg
 page to do operation on
 @param[in] anon
 flag to indicate in anonymous mode (1) or not anonymous (0)
 @param[in] mempage
 buffer with memory page contents, required for verification of ecdsa signature
 @param[in] challenge
 buffer containing challenge, must be 32 bytes
 @param[out] sig_r
 buffer for r portion of signature, must be 32 bytes
 @param[out] sig_s
 buffer for s portion of signature, must be 32 bytes
 @return
 TRUE - command successful @n
 FALSE - command failed
 */
int ds28e30_compute_and_verify_ecdsa_no_read(int pg, int anon, uchar *mempage, uchar *challenge,
			uchar *sig_r, uchar *sig_s)
{
	uchar signature[64], message[256];
	int msg_len;
	uchar *pubkey_x, *pubkey_y;

	/* compute and read auth command */
	if (!ds28e30_cmd_compute_read_page_authentication(pg, anon, challenge, signature))
		return FALSE;

	/* put the signature in the return buffers, signature is 's' and then 'r', MSByte first */
	memcpy(sig_s, signature, BYTE_LENGTH_32);
	memcpy(sig_r, &signature[BYTE_LENGTH_32], BYTE_LENGTH_32);

	/* construct the message to hash for signature verification */
	/* ROM NO | Page Data | Challenge (Buffer) | Page# | mian_id */
	/* ROM NO */
	msg_len = ZERO_VALUE;
	if (anon)
		memset(&message[msg_len], BYTE_VALUE_FF, BYTE_LENGTH_8);
	else
		memcpy(&message[msg_len], rom_no, BYTE_LENGTH_8);
	msg_len += BYTE_LENGTH_8;
	/* Page Data */
	memcpy(&message[msg_len], mempage, BYTE_LENGTH_32);
	msg_len += BYTE_LENGTH_32;
	/* Challenge (Buffer) */
	memcpy(&message[msg_len], challenge, BYTE_LENGTH_32);
	msg_len += BYTE_LENGTH_32;
	/* Page# */
	message[msg_len++] = pg;
	/* mian_id */
	memcpy(&message[msg_len], mian_id, BYTE_LENGTH_2);
	msg_len += BYTE_LENGTH_2;
	pubkey_x = public_key_x;
	pubkey_y = public_key_y;

	/* verify Signature and return result */
	return deep_cover_verify_ecdsa_signature(message, msg_len, pubkey_x, pubkey_y, sig_r, sig_s);
}

/*  Verify certificate of devices like DS28C36/DS28C36/DS28E38/DS28E30.
 @param[in] sig_r
 Buffer for R portion of certificate signature (MSByte first)
 @param[in] sig_s
 Buffer for S portion of certificate signature (MSByte first)
 @param[in] pub_x
 Public Key x to verify
 @param[in] pub_y
 Public Key y to verify
 @param[in] SLAVE_ROMID
 device's 64-bit ROMID (LSByte first)
 @param[in] slave_manid
 Maxim defined as manufacturing ID
 @param[in] system_level_pub_key_x
 32-byte buffer container the system level public key x
 @param[in] system_level_pub_key_y
 32-byte buffer container the system level public key y
  @return
  TRUE - certificate valid @n
  FALSE - certificate not valid
 */
int verify_ecdsa_certificate_of_device(uchar *sig_r, uchar *sig_s, uchar *pub_key_x,
					uchar *pub_key_y, uchar *slave_romid, uchar *slave_manid,
					uchar *system_level_pub_key_x,
					uchar *system_level_pub_key_y)
{
	unsigned char buf[BYTE_LENGTH_32];

	/* setup software ecdsa computation */
	deep_cover_coproc_setup(ZERO_VALUE, ZERO_VALUE, ZERO_VALUE, ZERO_VALUE);
	/* create customization field */
	/* 16 zeros (can be set to other customer specific value) */
	memcpy(buf, certificate_constant, BYTE_LENGTH_16); /* TODO: */
	/* ROMID */
	memcpy(&buf[BYTE_LENGTH_16], slave_romid, BYTE_LENGTH_8);
	/* mian_id */
	memcpy(&buf[24], slave_manid, BYTE_LENGTH_2);
	return deep_cover_verify_ecdsa_certificate(sig_r, sig_s, pub_key_x, pub_key_y, buf,
					BYTE_LENGTH_16+BYTE_LENGTH_8+BYTE_LENGTH_2, system_level_pub_key_x, system_level_pub_key_y);
}
/*
	setting expected MAN_ID, protection status, counter value, system-level public key,
	authority public key and certificate constants
*/
void configure_ds28e30_parameters()
{
	unsigned short cid_value;

	cid_value = rom_no[6] << BYTE_VALUE_04;
	cid_value += (rom_no[5] & BYTE_VALUE_F0) >> BYTE_VALUE_04;
	chg_err("%s: cid_value: 0x%x\n", __func__, cid_value);

	switch (cid_value) {
	case OPPO_CID:  /* OPPO device */
		expected_CID[BYTE_LENGTH_0] = OPPO_CID_LSB;
		expected_CID[BYTE_LENGTH_1] = OPPO_CID_MSB;
		expected_main_id[BYTE_LENGTH_0] = OPPO_MAN_ID_LSB;
		expected_main_id[BYTE_LENGTH_1] = OPPO_MAN_ID_MSB;
		memcpy(expected_page_protection_status, page_protection_status, BYTE_LENGTH_11);
		memcpy(certificate_constant, certificate_constant, BYTE_LENGTH_16);
		memcpy(system_public_key_x, system_public_key_x, BYTE_LENGTH_32);
		memcpy(system_public_key_y, system_public_key_y, BYTE_LENGTH_32);
		memcpy(authority_private_key, authority_private_key, BYTE_LENGTH_32);
		memcpy(authority_public_key_x, authority_public_key_x, BYTE_LENGTH_32);
		memcpy(authority_public_key_y, authority_public_key_y, BYTE_LENGTH_32);
		break;
	default:
		expected_CID[BYTE_LENGTH_0] = GP_CID_LSB;
		expected_CID[BYTE_LENGTH_1] = GP_CID_MSB;
		expected_main_id[BYTE_LENGTH_0] = GP_MAN_ID_LSB;
		expected_main_id[BYTE_LENGTH_1] = GP_MAN_ID_MSB;
		memcpy(expected_page_protection_status, GP_PageProtectionStatus, BYTE_LENGTH_11);
		memcpy(certificate_constant, gp_certificate_constant, BYTE_LENGTH_16);
		memcpy(system_public_key_x, gp_system_public_key_x, BYTE_LENGTH_32);
		memcpy(system_public_key_y, gp_system_public_key_y, BYTE_LENGTH_32);
		memcpy(authority_private_key, gp_authority_private_key, BYTE_LENGTH_32);
		memcpy(authority_public_key_x, gp_authority_public_key_x, BYTE_LENGTH_32);
		memcpy(authority_public_key_y, gp_authority_public_key_y, BYTE_LENGTH_32);
		break;
	}
}

/*
 Authenticate both device certificate and digital signautre
 @param[in] PageNumber
 Indicate which EEPROM page number will used, could be 0, 1, 2, 3,4,5,6,7
 @param[in] anon
 if anon='1', then device's ROMID will be displaced with all 0xFFs to generate/verify signature;
 if anon='0', unique ROMID is used
 @param[in] challenge
 reserved challenge data to generate random digital signature
  @return
  TRUE - both certificate/digital signature is valid @n
  FALSE or NotAuthecticated - either certificate or digital signature is invalid
  No_device - DS28E30 is not found
  CRC_ERROR - 1-wire communication error, need to do authentication again
*/
int authenticate_ds28e30(unsigned char *sn_num, int PageNumber)
{
	int i;
	unsigned char flag;
	unsigned char PageData[BYTE_LENGTH_32], buf[128];
	static unsigned char sig_r[BYTE_LENGTH_32], sig_s[BYTE_LENGTH_32];
	unsigned char DevicePublicKey_X[BYTE_LENGTH_32], DevicePublicKey_Y[BYTE_LENGTH_32];
	unsigned char Page_Certificate_R[BYTE_LENGTH_32], Page_Certificate_S[BYTE_LENGTH_32];
	unsigned char challenge[BYTE_LENGTH_32];
	unsigned char page_sn[BYTE_LENGTH_32];
	int ret = true;

	if((ds28e30_read_romno_manid_hardware_version()) == false) {
		chg_err("%s: read romid failed\n", __func__);
		/* return CRC_ERROR; */
	} else {
		configure_ds28e30_parameters();
	}

	if (ds28e30_cmd_read_memory(PG_USER_EEPROM_0, page_sn) == false) {
		chg_err("%s: read sn failed\n", __func__);
	}
	for (i = BYTE_LENGTH_2; i <= BYTE_LENGTH_13; i++) {
		chg_info("%s: read sn[%d] %x\n", __func__, i, page_sn[i]);
	}
	if (strncmp(&page_sn[BYTE_LENGTH_2], sn_num, BYTE_VALUE_12)) {
		chg_err("%s: sn compare failed\n", __func__);
		ret = false;
		goto ERR;
	} else {
		chg_info("%s: sn compare succ\n", __func__);
	}


	/* read the device public key X&Y */
	flag = ds28e30_cmd_read_device_public_key(buf);
	if (flag != true) {
		chg_err("%s: read device publickey failed\n", __func__);
		ret = false;
		goto ERR;
	} else {
		memcpy(DevicePublicKey_X, buf, BYTE_LENGTH_32);  /* reserve device public key X */
		memcpy(DevicePublicKey_Y, &buf[BYTE_LENGTH_32], BYTE_LENGTH_32);  /* reserve device public key Y */
	}
	/* read device certificate */
	for(i = BYTE_LENGTH_0; i < BYTE_LENGTH_2; i++) {
		/* read device Certificate R&S in buf[] */
		flag = ds28e30_cmd_read_memory(PG_CERTIFICATE_R+i, buf+i*BYTE_LENGTH_32);
		if(flag != true) {
			chg_err("%s: %d read device certificate failed\n", __func__, i);
			ret = false;
			goto ERR;
		}
	}
	memcpy(Page_Certificate_R, buf, BYTE_LENGTH_32);  /* reserve device certificate R */
	memcpy(Page_Certificate_S, &buf[BYTE_LENGTH_32], BYTE_LENGTH_32);  /* reserve device certificate S */
	/* authenticate DS28E30 by two steps: digital signature verification, */
	/* device certificate verification */
	/* to verify the digital signature */
	/* prepare to verify the signature */

	/* copy device public key X to public key x buffer */
	memcpy(public_key_x, DevicePublicKey_X, BYTE_LENGTH_32);
	/* copy device public key Y to public key x buffer */
	memcpy(public_key_y, DevicePublicKey_Y, BYTE_LENGTH_32);
	/* read page data for digital signature */
	/* read page data from the given PageNumber */
	flag = ds28e30_cmd_read_memory(PageNumber, PageData);
	if (flag != true) {
		chg_err("%s: read digital signature failed\n", __func__);
		ret = false;
		goto ERR;
	}
	/* generate random challenge */
	memcpy(buf, sig_r, BYTE_LENGTH_32);
	memcpy(&buf[BYTE_LENGTH_32], sig_s, BYTE_LENGTH_32);
	ucl_sha256(challenge, buf, BYTE_LENGTH_64);

	/* setup software ecdsa computation */
	deep_cover_coproc_setup(ZERO_VALUE, ZERO_VALUE, ZERO_VALUE, ZERO_VALUE);
	flag = ds28e30_compute_and_verify_ecdsa(ZERO_VALUE, ZERO_VALUE, PageData, challenge, sig_r, sig_s);
	if (flag == false) {
		chg_err("%s: digital signature verify failed\n", __func__);
		/* digital signature is not verified */
		ret = false;
		goto ERR;
	}

	/* verify device certificate */
	flag = verify_ecdsa_certificate_of_device(Page_Certificate_R, Page_Certificate_S,
				DevicePublicKey_X, DevicePublicKey_Y, rom_no, mian_id,
				system_public_key_x, system_public_key_y);
	if (flag == false) {
		chg_err("%s: verify device certificate failed\n", __func__);
		/* device certificate is not verified */
		ret = false;
		goto ERR;
	}
	chg_info("%s: Authenticate succ\n", __func__);

	/* both digital signature & device certificate are verified */
	ret = true;

ERR:
	set_data_gpio_in();
	return ret;
}

/*
 High level function to do a authenticated write memory  page with ECW mode
 @param[in] pg
 page to do operation on
 @param[in] mempage
 buffer with new memory page contents to be written
 @return
 TRUE - command successful @n
 FALSE - command failed
*/

int ds28e30_write_memory_page_with_ECW(int pg, uchar *new_data)
{
	/* static unsigned char AlreadyAccessDS28E30=false; */
	uchar message[256], old_data[BYTE_LENGTH_32], sig_r[BYTE_LENGTH_32], sig_s[BYTE_LENGTH_32];
	int msg_len;

	/* assume the device ROMID and mian_id has been got from DS28E30 */
	/* read old page data */
	if ((ds28e30_cmd_read_memory(pg, old_data)) == false)
		return false;
	/* construct the message to compute authentication Signature */
	/* (ROM NO | Old Page Data | New Page Data | 0x80 |Page # | mian_id) */

	memcpy(message, rom_no, BYTE_LENGTH_8);
	msg_len = BYTE_LENGTH_8;
	/* old data */
	memcpy(&message[msg_len], old_data, BYTE_LENGTH_32);
	msg_len += BYTE_LENGTH_32;
	/* new data */
	memcpy(&message[msg_len], new_data, BYTE_LENGTH_32);
	msg_len += BYTE_LENGTH_32;
	/* Page# */
	message[msg_len++] = (MSB_CHECK | pg);
	/* mian_id */
	memcpy(&message[msg_len], mian_id, BYTE_LENGTH_2);
	msg_len += BYTE_LENGTH_2;
	deep_cover_coproc_setup(ZERO_VALUE, ZERO_VALUE, ZERO_VALUE, ZERO_VALUE);
	/*
	deep_cover_compute_ecdsa_signature(message, msg_len,
		authority_private_key, sig_r, sig_s); TODO:
	*/
	return ds28e30_cmd_authendicated_ecdsa_write_memory(pg, new_data, sig_r, sig_s);
}

/*  ds28e30 Helper functions
 Return last result byte. Useful if a function fails.
 @return
 Result byte
 */
uchar ds28e30_get_last_result_byte(void)
{
	return last_result_byte;
}

/*  @internal
 Sent/receive standard flow command
 @param[in] write_buf
 Buffer with write contents (preable payload)
 @param[in] write_len
 Total length of data to write in 'write_buf'
 @param[in] delay_ms
 Delay in milliseconds after command/preable.  If == 0 then can use
 repeated-start to re-access the device for read of result byte.
 @param[in] expect_read_len
 Expected result read length
 @param[out] read_buf
 Buffer to hold data read from device. It must be at least 255 bytes long.
 @param[out] read_len
 Pointer to an integer to contain the length of data read and placed in read_buf
 Preloaded with expected read length for 1-Wire mode. If (0) but expected_read=TRUE
 then the first byte read is the length of data to read.
  @return
  TRUE - command successful @n
  FALSE - command failed
 @endinternal
 */

int standard_cmd_flow(uchar *write_buf, int write_len, int delay_ms, int expect_read_len,
			uchar *read_buf, int *read_len)
{
	uchar pkt[256];
	int pkt_len = ZERO_VALUE;
	int i;
	/* int start_offset = 0; */

#ifdef SPIN_LOCK_ENABLE
	mutex_lock(&ds_cmd_lock);
#endif
	/* Reset/presence */
	/* Rom COMMAND (set from select options) */
	if (!ow_skip_rom()) {
#ifdef SPIN_LOCK_ENABLE
		mutex_unlock(&ds_cmd_lock);
#endif
		return FALSE;
	}
	/* set result byte to no response */
	last_result_byte = RESULT_FAIL_COMMUNICATION;

	/* Construct write block, start with XPC command */
	pkt[pkt_len++] = XPC_COMMAND;

	/* Add length */
	pkt[pkt_len++] = write_len;

	/* write (first byte will be sub-command) */
	memcpy(&pkt[pkt_len], write_buf, write_len);
	pkt_len += write_len;

	/* send packet to DS28E30 */
	for(i = BYTE_LENGTH_0; i < pkt_len; i++)
		write_byte(pkt[i]);

	/* read two CRC bytes */
	pkt[pkt_len++] = read_byte();
	pkt[pkt_len++] = read_byte();

	/* check crc16 */
	crc16 = ZERO_VALUE;
	for (i = 0; i < pkt_len; i++)
		docrc16(pkt[i]);
	/* check if ROMID is populated after power up, skip CRC check if rom_no[0] = =0x00. */
	if (rom_no[BYTE_LENGTH_0] != ZERO_VALUE) {
		if (crc16 != CRC_16_CORRECT_VALUE) {
#ifdef SPIN_LOCK_ENABLE
			mutex_unlock(&ds_cmd_lock);
#endif
			return FALSE;
		}
	}

	/* Send release byte, start strong pull-up */
	write_byte(RESULT_SUCCESS);

	/* optional delay */
	if (delay_ms)
	maxim_delay_ms(delay_ms);

	/* turn off strong pull-up */
	/*   OWLevel(MODE_NORMAL); */
	/* read FF and the length byte */
	pkt[BYTE_LENGTH_0] = read_byte();
	pkt[BYTE_LENGTH_1] = read_byte();
	*read_len = pkt[BYTE_LENGTH_1];

	/* make sure there is a valid length */
	if (*read_len != RESULT_FAIL_COMMUNICATION) {
		/* read packet */
		for(i = 0; i <  *read_len+BYTE_LENGTH_2; i++)
			read_buf[i] = read_byte();
		/* check crc16 */
		crc16 = ZERO_VALUE;
		docrc16(*read_len);
		for (i = 0; i < (*read_len + BYTE_LENGTH_2); i++)
			docrc16(read_buf[i]);

		if (crc16 != CRC_16_CORRECT_VALUE) {
#ifdef SPIN_LOCK_ENABLE
			mutex_unlock(&ds_cmd_lock);
#endif
			return FALSE;
		}

		if (expect_read_len != *read_len) {
#ifdef SPIN_LOCK_ENABLE
			mutex_unlock(&ds_cmd_lock);
#endif
			return FALSE;
		}
	} else {
#ifdef SPIN_LOCK_ENABLE
		mutex_unlock(&ds_cmd_lock);
#endif
		return FALSE;
	}

	/* success */
#ifdef SPIN_LOCK_ENABLE
	mutex_unlock(&ds_cmd_lock);
#endif
	return TRUE;
}

/* Calculate a new crc16 from the input data shorteger.  Return the current */
/* crc16 and also update the global variable crc16 */
static short oddparity[BYTE_LENGTH_16] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};

unsigned short docrc16(unsigned short data)
{
	data = (data ^ (crc16 & BYTE_VALUE_FF)) & BYTE_VALUE_FF;
	crc16 >>= BYTE_VALUE_8;

	if (oddparity[data & BYTE_VALUE_0F] ^ oddparity[data >> BYTE_VALUE_04])
		crc16 ^= SHORT_VALUE_C001;

	data <<= BYTE_VALUE_6;
	crc16 ^= data;
	data <<= BYTE_VALUE_1;
	crc16 ^= data;

	return crc16;
}

/*  read device ROM NO, mian_id and hardware version
 @return
 TRUE read ROM ID, man_id and hardware version and confirmed @n
 FALSE failure to read ds28e30
 */

int ds28e30_read_romno_manid_hardware_version(void)
{
	uchar i, temp = ZERO_VALUE, buf[BYTE_LENGTH_10], pg = ZERO_VALUE, flag;

	chg_info("%s entry", __func__);
	rom_no[BYTE_LENGTH_0] = ZERO_VALUE;

	ow_read_rom();      /* search DS28E30 */
	if((rom_no[BYTE_LENGTH_0] & BYTE_VALUE_7F) == DS28E30_FAM) {
		for(i = BYTE_LENGTH_0; i < BYTE_LENGTH_6; i++)
			temp |= rom_no[BYTE_VALUE_1 + i]; /* check if the device is power up at the first time */
		if(temp == ZERO_VALUE) {  /* power up the device, then read ROMID again */
			chg_info("%s temp==0", __func__);
			rom_no[BYTE_LENGTH_0] = ZERO_VALUE;
			ds28e30_cmd_read_status(pg, buf, mian_id, hardware_version);  /* page number=0 */
			ow_read_rom();      /* read ROMID from DS28E30 */
			/* page number=0 */
			flag = ds28e30_cmd_read_status(MSB_CHECK | pg, buf, mian_id, hardware_version);
			chg_info("%s temp==0 flag %d", __func__, flag);
			return flag;
		} else {
			/* page number=0 */
			flag = ds28e30_cmd_read_status(MSB_CHECK | pg, buf, mian_id, hardware_version);
			chg_info("%s temp!=0 flag %d", __func__, flag);
			return flag;
		}
	}
	return false;
}

/*  Set public key in module.  This will be used in other helper functions.
 @param[in] pubkey_x
 buffer for x portion of public key, must be 32 bytes
 @param[in] pubkey_y
 buffer for y portion of public key, must be 32 bytes
 */
void ds28e30_set_public_key(uchar *pubkey_x, uchar *pubkey_y)
{
	memcpy(public_key_x, pubkey_x, BYTE_LENGTH_32);
	memcpy(public_key_y, pubkey_y, BYTE_LENGTH_32);
}

/*
 Set public key in module.  This will be used in other helper functions.
 @param[in] priv
 buffer for private key, must be 32 bytes
 */
void ds28e30_set_private_key(uchar *priv)
{
	memcpy(private_key, priv, BYTE_LENGTH_32);
}

/*******************************************************************
@brief Helper function to compute Signature using the specified private key.
@param[in] message
Messge to hash for signature verification
@param[in] msg_len
Length of message in bytes
@param[in] key
(0, 1) to indicate private key A,B
@param[out] sig_r
signature portion r
@param[out] sig_s
signature portion s
@return
TRUE - signature verified @n
FALSE - signature not verified
*******************************************************************/
int sw_compute_ecdsa_signature(uchar *message, int msg_len, uchar *sig_r, uchar *sig_s)
{
	int configuration;
	ucl_type_ecdsa_signature signature;

	/* hook up r/s to the signature structure */
	signature.r = sig_r;
	signature.s = sig_s;

	/* construct configuration */
	configuration = (SECP256R1 << UCL_CURVE_SHIFT) ^ (UCL_MSG_INPUT << UCL_INPUT_SHIFT) ^
						(UCL_SHA256 << UCL_HASH_SHIFT);

	/* create signature and return result */
	return (ucl_ecdsa_signature(signature, private_key, ucl_sha256, message, msg_len,
					&secp256r1, configuration) == SUCCESS_FINISHED);
}
/*
The 'ow_read_rom' function does a Read-ROM.  This function
uses the read-ROM function 33h to read a ROM number and verify CRC8.
Returns:   TRUE (1) : OWReset successful and Serial Number placed
                      in the global ROM, CRC8 valid
           FALSE (0): OWReset did not have presence or CRC8 invalid
 */
int ow_read_rom(void)
{
	uchar buf[BYTE_LENGTH_16];
	int i;
#ifdef SPIN_LOCK_ENABLE
	mutex_lock(&ds_cmd_lock);
#endif

	if (ow_reset() == TRUE) {
		write_byte(READ_ROM); /* READ ROM command */
		for(i = 0; i < BYTE_LENGTH_8; i++)
			buf[i] = read_byte();
		chg_info("RomID = %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x\n",
			buf[0], buf[1], buf[2], buf[3],
			buf[4], buf[5], buf[6], buf[7]);
		/* verify CRC8 */
		crc8 = ZERO_VALUE;
		for (i = 0; i < BYTE_LENGTH_8; i++)
			docrc8(buf[i]);
		if ((crc8 == ZERO_VALUE) && (buf[BYTE_LENGTH_0] != ZERO_VALUE)) {
			memcpy(rom_no, &buf[BYTE_LENGTH_0], BYTE_LENGTH_8);
#ifdef SPIN_LOCK_ENABLE
			mutex_unlock(&ds_cmd_lock);
#endif
			chg_info("DS28E30_standard_cmd_flow: read ROMID successfully!\n");
			return TRUE;
		}
	}
#ifdef SPIN_LOCK_ENABLE
	mutex_unlock(&ds_cmd_lock);
#endif
	chg_err("DS28E30_standard_cmd_flow: error in reading ROMID!\n");
	return FALSE;
}


/*
The 'ow_skip_rom' function does a skip-ROM.  This function
uses the Skip-ROM function CCh.
Returns:   TRUE (1) : OWReset successful and skip rom sent.
           FALSE (0): OWReset did not have presence
*/

int ow_skip_rom(void)
{
	if (ow_reset() == TRUE) {
		write_byte(SKIP_ROM);
		return TRUE;
	}

	return FALSE;
}

static unsigned char dscrc_table[] = {
	0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
	157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
	35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
	190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
	70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
	219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
	101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
	248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
	140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
	17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
	175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
	50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
	202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
	87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
	233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
	116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53};

/* --------------------------------------------------------------------------
Calculate the CRC8 of the byte value provided with the current
global 'crc8' value.
Returns current global crc8 value
*/
unsigned char docrc8(unsigned char value)
{
	/* See Application Note 27 */
	/* TEST BUILD */
	crc8 = dscrc_table[crc8 ^ value];
	return crc8;
}

MODULE_DESCRIPTION("oplus ds28e30 driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:oplus-ds28e30");

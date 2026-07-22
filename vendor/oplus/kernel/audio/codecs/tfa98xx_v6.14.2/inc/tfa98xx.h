/*
 * Copyright (C) 2014-2020 NXP Semiconductors, All Rights Reserved.
 * Copyright 2021 GOODIX
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __TFA98XX_INC__
#define __TFA98XX_INC__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>

#include "tfa_device.h"
#include "tfa_container.h"
#include "config.h"

#define DISABLE_TFA98XX_ALSA_SUPPORT 	1

#ifdef CONFIG_ARCH_QCOM
#undef DISABLE_TFA98XX_ALSA_SUPPORT
#endif

#ifdef CONFIG_ARCH_SPRD
#undef DISABLE_TFA98XX_ALSA_SUPPORT
#endif

#define ENABLE_IOCTRL_INTERFACE 	1

#ifdef ENABLE_IOCTRL_INTERFACE
#define TFA98XX_MAGIC_NUMBER 0x112233

/*left channel*/
#define TFA98XX_SWITCH_CONFIG_L         _IOWR(TFA98XX_MAGIC_NUMBER, 1,  void *)
#define TFA98XX_POWER_OFF_L             _IOWR(TFA98XX_MAGIC_NUMBER, 3,  void *)
#define TFA98XX_SWITCH_FIRMWARE_L       _IOWR(TFA98XX_MAGIC_NUMBER, 5,  void *)
#define TFA98XX_PRI_GET_CALIB_STATE_L   _IOWR(TFA98XX_MAGIC_NUMBER, 7,  void *)
#define TFA98XX_PRI_SET_RE25C_L         _IOWR(TFA98XX_MAGIC_NUMBER, 9,  void *)
#define TFA98XX_SEC_GET_CALIB_STATE_L   _IOWR(TFA98XX_MAGIC_NUMBER, 11, void *)
#define TFA98XX_SEC_SET_RE25C_L         _IOWR(TFA98XX_MAGIC_NUMBER, 13, void *)

/*right channel*/
#define TFA98XX_SWITCH_CONFIG_R         _IOWR(TFA98XX_MAGIC_NUMBER, 2,  void *)
#define TFA98XX_POWER_OFF_R             _IOWR(TFA98XX_MAGIC_NUMBER, 4,  void *)
#define TFA98XX_SWITCH_FIRMWARE_R       _IOWR(TFA98XX_MAGIC_NUMBER, 6,  void *)
#define TFA98XX_PRI_GET_CALIB_STATE_R   _IOWR(TFA98XX_MAGIC_NUMBER, 8,  void *)
#define TFA98XX_PRI_SET_RE25C_R         _IOWR(TFA98XX_MAGIC_NUMBER, 10, void *)
#define TFA98XX_SEC_GET_CALIB_STATE_R   _IOWR(TFA98XX_MAGIC_NUMBER, 12, void *)
#define TFA98XX_SEC_SET_RE25C_R         _IOWR(TFA98XX_MAGIC_NUMBER, 14, void *)

/*write only for HAL */
#define TFA98XX_SET_ALGO_ID             _IOWR(TFA98XX_MAGIC_NUMBER, 20, void *)
#define TFA98XX_SET_VOLUME              _IOWR(TFA98XX_MAGIC_NUMBER, 21, void *)

/*read only ctrl for tfadsp_parser*/
#define TFA98XX_GET_BOX_ID               _IOR(TFA98XX_MAGIC_NUMBER, 40, void *)
#define TFA98XX_GET_ALGO_ID              _IOR(TFA98XX_MAGIC_NUMBER, 41, void *)
#define TFA98XX_GET_DEV_NUM              _IOR(TFA98XX_MAGIC_NUMBER, 42, void *)

/*factory*/
#define TFA98XX_DO_CALIBRATE            _IOWR(TFA98XX_MAGIC_NUMBER, 50, void *)
#define TFA98XX_DO_REGDUMP              _IOWR(TFA98XX_MAGIC_NUMBER, 51, void *)
//#ifdef OPLUS_ARCH_EXTENDS
#define TFA98XX_GET_RE_RANGE            _IOWR(TFA98XX_MAGIC_NUMBER, 52, void *)
#define TFA98XX_GET_F0_RANGE            _IOWR(TFA98XX_MAGIC_NUMBER, 53, void *)
#define TFA98XX_INIT_CALIB_RE           _IOWR(TFA98XX_MAGIC_NUMBER, 54, void *)
//#endif

#ifdef CONFIG_COMPAT
/*left channel*/
#define TFA98XX_SWITCH_CONFIG_L_COMPAT          _IOWR(TFA98XX_MAGIC_NUMBER, 1,  compat_uptr_t)
#define TFA98XX_POWER_OFF_L_COMPAT              _IOWR(TFA98XX_MAGIC_NUMBER, 3,  compat_uptr_t)
#define TFA98XX_SWITCH_FIRMWARE_L_COMPAT        _IOWR(TFA98XX_MAGIC_NUMBER, 5,  compat_uptr_t)
#define TFA98XX_PRI_GET_CALIB_STATE_L_COMPAT    _IOWR(TFA98XX_MAGIC_NUMBER, 7,  compat_uptr_t)
#define TFA98XX_PRI_SET_RE25C_L_COMPAT          _IOWR(TFA98XX_MAGIC_NUMBER, 9,  compat_uptr_t)
#define TFA98XX_SEC_GET_CALIB_STATE_L_COMPAT    _IOWR(TFA98XX_MAGIC_NUMBER, 11, compat_uptr_t)
#define TFA98XX_SEC_SET_RE25C_L_COMPAT          _IOWR(TFA98XX_MAGIC_NUMBER, 13, compat_uptr_t)

/*right channel*/
#define TFA98XX_SWITCH_CONFIG_R_COMPAT          _IOWR(TFA98XX_MAGIC_NUMBER, 2,  compat_uptr_t)
#define TFA98XX_POWER_OFF_R_COMPAT              _IOWR(TFA98XX_MAGIC_NUMBER, 4,  compat_uptr_t)
#define TFA98XX_SWITCH_FIRMWARE_R_COMPAT        _IOWR(TFA98XX_MAGIC_NUMBER, 6,  compat_uptr_t)
#define TFA98XX_PRI_GET_CALIB_STATE_R_COMPAT    _IOWR(TFA98XX_MAGIC_NUMBER, 8,  compat_uptr_t)
#define TFA98XX_PRI_SET_RE25C_R_COMPAT          _IOWR(TFA98XX_MAGIC_NUMBER, 10, compat_uptr_t)
#define TFA98XX_SEC_GET_CALIB_STATE_R_COMPAT    _IOWR(TFA98XX_MAGIC_NUMBER, 12, compat_uptr_t)
#define TFA98XX_SEC_SET_RE25C_R_COMPAT          _IOWR(TFA98XX_MAGIC_NUMBER, 14, compat_uptr_t)

/*write only for HAL */
#define TFA98XX_SET_ALGO_ID_COMPAT              _IOWR(TFA98XX_MAGIC_NUMBER, 20, compat_uptr_t)
#define TFA98XX_SET_VOLUME_COMPAT               _IOWR(TFA98XX_MAGIC_NUMBER, 21, compat_uptr_t)

/*read only ctrl for tfadsp_parser*/
#define TFA98XX_GET_BOX_ID_COMPAT                _IOR(TFA98XX_MAGIC_NUMBER, 40, compat_uptr_t)
#define TFA98XX_GET_ALGO_ID_COMPAT               _IOR(TFA98XX_MAGIC_NUMBER, 41, compat_uptr_t)
#define TFA98XX_GET_DEV_NUM_COMPAT               _IOR(TFA98XX_MAGIC_NUMBER, 42, compat_uptr_t)

/*factory*/
#define TFA98XX_DO_CALIBRATE_COMPAT             _IOWR(TFA98XX_MAGIC_NUMBER, 50, compat_uptr_t)
#define TFA98XX_DO_REGDUMP_COMPAT               _IOWR(TFA98XX_MAGIC_NUMBER, 51, compat_uptr_t)
//#ifdef OPLUS_ARCH_EXTENDS
#define TFA98XX_GET_RE_RANGE_COMPAT             _IOWR(TFA98XX_MAGIC_NUMBER, 52, compat_uptr_t)
#define TFA98XX_GET_F0_RANGE_COMPAT             _IOWR(TFA98XX_MAGIC_NUMBER, 53, compat_uptr_t)
#define TFA98XX_INIT_CALIB_RE_COMPAT            _IOWR(TFA98XX_MAGIC_NUMBER, 54, compat_uptr_t)
//#endif

#endif

/* hardware channel configuration, the definition should be the same in kernel and smartkit */
#define CHAN_PRI_L    (1<<0)     /* left_top */
#define CHAN_PRI_R    (1<<1)     /* right_top */
#define CHAN_SEC_L    (1<<2)     /* left_bottom */
#define CHAN_SEC_R    (1<<3)     /* right_bottom */
#define CHAN_PRI_LS   (1<<4)     /* left_top */
#define CHAN_PRI_RS   (1<<5)     /* right_top */
#define CHAN_SEC_LS   (1<<6)     /* left_bottom */
#define CHAN_SEC_RS   (1<<7)     /* right_bottom */

#endif /*ENABLE_IOCTRL_INTERFACE*/

/* max. length of a alsa mixer control name */
#define MAX_CONTROL_NAME        48
#define TFA_WORD_LEN        (3)
#define TFA_LIVEDATA_FRAME_LEN  (4*TFA_WORD_LEN)

#define TFA98XX_MAX_REGISTER              0xff

#define TFA98XX_FLAG_SKIP_INTERRUPTS	(1 << 0)
#define TFA98XX_FLAG_SAAM_AVAILABLE		(1 << 1)
#define TFA98XX_FLAG_STEREO_DEVICE		(1 << 2)
#define TFA98XX_FLAG_MULTI_MIC_INPUTS	(1 << 3)
#define TFA98XX_FLAG_TAPDET_AVAILABLE	(1 << 4)
#define TFA98XX_FLAG_CALIBRATION_CTL	(1 << 5)
#define TFA98XX_FLAG_REMOVE_PLOP_NOISE	(1 << 6)
#define TFA98XX_FLAG_LP_MODES	        (1 << 7)
#define TFA98XX_FLAG_TDM_DEVICE         (1 << 8)
#define TFA98XX_FLAG_ADAPT_NOISE_MODE   (1 << 9)
#define TFA98XX_FLAG_OTP_TYPE_DEVICE   	(1 << 10)

#define TFA98XX_NUM_RATES		9

/* DSP init status */
enum tfa98xx_dsp_init_state {
	TFA98XX_DSP_INIT_STOPPED,	/* DSP not running */
	TFA98XX_DSP_INIT_RECOVER,	/* DSP error detected at runtime */
	TFA98XX_DSP_INIT_FAIL,		/* DSP init failed */
	TFA98XX_DSP_INIT_PENDING,	/* DSP start requested */
	TFA98XX_DSP_INIT_DONE,		/* DSP running */
	TFA98XX_DSP_INIT_USEROFF,   /* DSP stopped by user */
	TFA98XX_DSP_INIT_INVALIDATED,	/* DSP was running, requires re-init */
};

enum tfa98xx_dsp_fw_state {
       TFA98XX_DSP_FW_NONE = 0,
       TFA98XX_DSP_FW_PENDING,
       TFA98XX_DSP_FW_FAIL,
       TFA98XX_DSP_FW_OK,
};

struct tfa98xx_firmware {
	void			*base;
	struct tfa98xx_device	*dev;
	char			name[9];	//TODO get length from tfa parameter defs
};

struct tfa98xx_baseprofile {
	char basename[MAX_CONTROL_NAME];    /* profile basename */
	int len;                            /* profile length */
	int item_id;                        /* profile id */
	int sr_rate_sup[TFA98XX_NUM_RATES]; /* sample rates supported by this profile */
	struct list_head list;              /* list of all profiles */
};
enum tfa_reset_polarity{
	LOW=0,
	HIGH=1
};
struct tfa98xx {
	struct regmap *regmap;
	struct i2c_client *i2c;
	struct regulator *vdd;
	struct snd_soc_component *codec;
	struct workqueue_struct *tfa98xx_wq;
	struct delayed_work init_work;
	struct delayed_work monitor_work;
	struct delayed_work interrupt_work;
	struct delayed_work nmodeupdate_work;
#ifdef DISABLE_TFA98XX_ALSA_SUPPORT
	struct delayed_work unmute_work;
#endif

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_MM_FEEDBACK)
	struct delayed_work check_work;
	uint32_t f0_min;
	uint32_t f0_max;
#endif /*CONFIG_OPLUS_FEATURE_MM_FEEDBACK*/

	struct mutex dsp_lock;
	int dsp_init;
	int dsp_fw_state;
	int sysclk;
	int rst_gpio;
	u16 rev;
	int audio_mode;
	struct tfa98xx_firmware fw;
	char *fw_name;
	int rate;
	wait_queue_head_t wq;
	struct device *dev;
	unsigned int init_count;
	int pstream;
	int cstream;
	struct input_dev *input;
	bool tapdet_enabled;		/* service enabled */
	unsigned int tapdet_profiles;	/* tapdet profile bitfield */
	bool tapdet_poll;		/* tapdet running on polling mode */

	unsigned int rate_constraint_list[TFA98XX_NUM_RATES];
	struct snd_pcm_hw_constraint_list rate_constraint;

	int reset_gpio;
	int power_gpio;
	int irq_gpio;
	int channel_config;
	bool is_aux_i2c;
	enum tfa_reset_polarity reset_polarity;
	struct list_head list;
	struct tfa_device *tfa;
	int vstep;
	int profile;
	int prof_vsteps[TFACONT_MAXPROFS]; /* store vstep per profile (single device) */

#ifdef CONFIG_DEBUG_FS
	struct dentry *dbg_dir;
#endif
#ifdef ENABLE_IOCTRL_INTERFACE
    struct miscdevice misc_dev;
#endif
	u8 reg;
	unsigned int flags;
	bool set_mtp_cal;
	uint16_t cal_data;

#ifdef CONFIG_ARCH_MEDIATEK
	/* pmic vs voter */
	struct regmap *vsv;
	u32 vsv_reg;
	u32 vsv_mask;
	u32 vsv_vers;
#endif

#ifdef OPLUS_ARCH_EXTENDS
	struct regulator *tfa98xx_vdd;
#endif /* OPLUS_ARCH_EXTENDS */
};

//#ifdef OPLUS_ARCH_EXTENDS
typedef struct __tfa_param_cal_spk {
    uint32_t r0_ok;
    int32_t r0;
    int32_t wire_r0;
    uint32_t f0_ok;
    int32_t f0;
} __packed TFA_PARAM_CAL_SPK;
//#endif

#endif /* __TFA98XX_INC__ */


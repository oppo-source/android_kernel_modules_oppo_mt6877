/***************************************************************
** Copyright (C), 2025, OPLUS Mobile Comm Corp., Ltd
** File : oplus_apuirdim.h
** Description : oplus_apuirdim header
** Version : 2.0
** Date : 2025/06/15
** Author : Display
***************************************************************/
#ifndef _OPLUS_APUIR_H_
#define _OPLUS_APUIR_H_
#include <linux/kobject.h>
#include "oplus_display_utils.h"
#include "oplus_display_debug.h"
/* log level config */
extern unsigned int oplus_apuir_log_level;
extern unsigned int oplus_adfr_display_id;
extern int apuir_enable;
extern bool apuir_state;
extern int oplus_seed_mode;
/* debug log */
#define APUIR_ERR(fmt, arg...)	\
	do {	\
		if (oplus_apuir_log_level >= OPLUS_APUIR_LOG_LEVEL_ERR)	\
			pr_err("[APUIRDRIVER][%u][ERR][%s:%d]"pr_fmt(fmt), oplus_apuir_display_id, __func__, __LINE__, ##arg);	\
	} while (0)

#define APUIR_WARN(fmt, arg...)	\
	do {	\
		if (oplus_apuir_log_level >= OPLUS_APUIR_LOG_LEVEL_WARN)	\
			pr_warn("[APUIRDRIVER][%u][WARN][%s:%d]"pr_fmt(fmt), oplus_apuir_display_id, __func__, __LINE__, ##arg);	\
	} while (0)

#define APUIR_INFO(fmt, arg...)	\
	do {	\
		if (oplus_apuir_log_level >= OPLUS_APUIR_LOG_LEVEL_INFO)	\
			pr_info("[APUIRDRIVER][%u][INFO][%s:%d]"pr_fmt(fmt), oplus_apuir_display_id, __func__, __LINE__, ##arg);	\
	} while (0)

#define APUIR_DEBUG(fmt, arg...)	\
	do {	\
		if ((oplus_apuir_log_level >= OPLUS_APUIR_LOG_LEVEL_DEBUG) || (oplus_display_log_type & OPLUS_DEBUG_LOG_APUIR))	\
			pr_info("[APUIRDRIVER][%u][DEBUG][%s:%d]"pr_fmt(fmt), oplus_apuir_display_id, __func__, __LINE__, ##arg);	\
	} while (0)

/* debug trace */
#define OPLUS_APUIR_TRACE_BEGIN(fmt, args...) \
	do { \
		if ((oplus_display_trace_enable & OPLUS_DISPLAY_APUIR_TRACE_ENABLE)) { \
			mtk_drm_print_trace("B|%d|"fmt"\n", current->tgid, ##args); \
		} \
	} while (0)

#define OPLUS_APUIR_TRACE_END(fmt, args...) \
	do { \
		if ((oplus_display_trace_enable & OPLUS_DISPLAY_APUIR_TRACE_ENABLE)) { \
			mtk_drm_print_trace("E|%d|"fmt"\n", current->tgid, ##args); \
		} \
	} while (0)

#define OPLUS_APUIR_TRACE_INT(fmt, args...) \
	do { \
		if ((oplus_display_trace_enable & OPLUS_DISPLAY_APUIR_TRACE_ENABLE)) { \
			mtk_drm_print_trace("C|%d|"fmt"\n", current->tgid, ##args); \
		} \
	} while (0)
bool oplus_apuir_get_uir_state(void);
void oplus_apuir_set_cmd(void *mtk_drm_crtc);
void oplus_apuir_setcmd_work_handler(struct work_struct *work_item);
void oplus_apuir_set_upnit_ds_list(int count, u32* list);
void oplus_apuir_set_lessnit_ds_list(int count, u32* list);
void apuir_set_ili_nit_index_list(int count, u32* list);
void apuir_set_nvt_upnit_index_list(int count, u32* list);
void apuir_set_nvt_lessnit_index_list(int count, u32* list);
void apuir_set_nvt_modeset_list(int count, u32* list);
void apuir_set_nvt_apl_lhset_list(int count, u32* list);
int oplus_get_apuir_enable(void);
void oplus_apuir_set_dsprop(void *drm_crtc, int prop_id, unsigned int propval);
void oplus_set_seed_mode(int seed_mode);
void oplus_set_apuir_ictype(int ictype);
#endif /* _OPLUS_APUIR_H_ */

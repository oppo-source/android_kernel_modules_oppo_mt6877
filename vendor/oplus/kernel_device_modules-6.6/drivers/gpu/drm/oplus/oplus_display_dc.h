/***************************************************************
** Copyright (C), 2024, OPLUS Mobile Comm Corp., Ltd
**
** File : oplus_display_dc.h
** Description : oplus display dc header
** Version : 1.0
** Date : 2024/05/20
** Author : Display
******************************************************************/
#ifndef _OPLUS_DISPLAY_DC_H_
#define _OPLUS_DISPLAY_DC_H_
#include <linux/module.h>

extern bool oplus_dc_support;
bool oplus_dc_is_support(void);
int oplus_dc_init(void *node);

int oplus_display_panel_set_dc_alpha(void *buf);
int oplus_display_panel_get_dc_alpha(void *buf);
int oplus_display_panel_get_dimlayer_enable(void *buf);
int oplus_display_panel_set_dimlayer_enable(void *buf);
int oplus_display_panel_set_dim_alpha(void *buf);
int oplus_display_panel_get_dim_alpha(void *buf);
int oplus_display_panel_set_dim_dc_alpha(void *buf);
int oplus_display_panel_get_dim_dc_alpha(void *buf);
#endif /*_OPLUS_DISPLAY_DC_H_*/

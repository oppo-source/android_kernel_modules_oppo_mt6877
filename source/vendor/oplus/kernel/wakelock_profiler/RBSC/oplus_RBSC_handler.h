/***********************************************************
** Copyright (C), 2008-2019, OPLUS Mobile Comm Corp., Ltd.
** VENDOR_EDIT
** File: - oplus_RBSC_monitor.h
** Description: This module support oplus RBSC monitor function for qcom platform such
**              as qcom aosd and cxsd deeepsleep issue
**
** Version: 1.0
** Date : 2020/01/14
**
** ------------------ Revision History:------------------------
** <author> <data> <version > <desc>
** zengyunqing 2020/01/14 1.0 build this module
** chenzhengding 2020/12/14 2.0 build this module
****************************************************************/

int oplus_save_clk_regulator_info_start(void);
void oplus_save_clk_regulator_info_end(void);
void oplus_get_clk_regulater_info(void);
void write_buff_to_dumpfile(char* buff);
void RBSC_issue_handler(int mode, char* issue_type);
int is_clk_dump_start_record(void);
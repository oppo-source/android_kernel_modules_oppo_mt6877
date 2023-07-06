/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __WF_PSE_TOP_REGS_H__
#define __WF_PSE_TOP_REGS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ************************************************************************** */
/* */
/* WF_PSE_TOP CR Definitions */
/* */
/* ************************************************************************** */

#define WF_PSE_TOP_BASE 0x820C8000

#define WF_PSE_TOP_PBUF_CTRL_ADDR \
	(WF_PSE_TOP_BASE + 0x04) /* 8004 */
#define WF_PSE_TOP_PBUF_CTRL_PAGE_SIZE_CFG_MASK \
	0x80000000 /* PAGE_SIZE_CFG[31] */
#define WF_PSE_TOP_PBUF_CTRL_PAGE_SIZE_CFG_SHFT 31
#define WF_PSE_TOP_PBUF_CTRL_PBUF_OFFSET_MASK \
	0x03FE0000 /* PBUF_OFFSET[25..17] */
#define WF_PSE_TOP_PBUF_CTRL_PBUF_OFFSET_SHFT 17
#define WF_PSE_TOP_PBUF_CTRL_TOTAL_PAGE_NUM_MASK \
0x00000FFF /* TOTAL_PAGE_NUM[11..0] */
#define WF_PSE_TOP_PBUF_CTRL_TOTAL_PAGE_NUM_SHFT 0

#define WF_PSE_TOP_INT_N9_ERR_STS_ADDR \
	(WF_PSE_TOP_BASE + 0x34) /* 8034 */

#define WF_PSE_TOP_INT_N9_ERR1_STS_ADDR \
	(WF_PSE_TOP_BASE + 0x38) /* 8038 */

#define WF_PSE_TOP_QUEUE_EMPTY_ADDR \
	(WF_PSE_TOP_BASE + 0xB0) /* 80B0 */
#define WF_PSE_TOP_QUEUE_EMPTY_RLS_Q_EMTPY_MASK 0x80000000 /* RLS_Q_EMTPY[31] */
#define WF_PSE_TOP_QUEUE_EMPTY_RLS_Q_EMTPY_SHFT 31
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_RXIOC1_QUEUE_EMPTY_MASK \
0x08000000 /* MDP_RXIOC1_QUEUE_EMPTY[27] */
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_RXIOC1_QUEUE_EMPTY_SHFT 27
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_TXIOC1_QUEUE_EMPTY_MASK \
0x04000000 /* MDP_TXIOC1_QUEUE_EMPTY[26] */
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_TXIOC1_QUEUE_EMPTY_SHFT 26
#define WF_PSE_TOP_QUEUE_EMPTY_SEC_TX1_QUEUE_EMPTY_MASK \
0x02000000 /* SEC_TX1_QUEUE_EMPTY[25] */
#define WF_PSE_TOP_QUEUE_EMPTY_SEC_TX1_QUEUE_EMPTY_SHFT 25
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_TX1_QUEUE_EMPTY_MASK \
0x01000000 /* MDP_TX1_QUEUE_EMPTY[24] */
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_TX1_QUEUE_EMPTY_SHFT 24
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_RXIOC_QUEUE_EMPTY_MASK \
0x00800000 /* MDP_RXIOC_QUEUE_EMPTY[23] */
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_RXIOC_QUEUE_EMPTY_SHFT 23
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_TXIOC_QUEUE_EMPTY_MASK \
0x00400000 /* MDP_TXIOC_QUEUE_EMPTY[22] */
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_TXIOC_QUEUE_EMPTY_SHFT 22
#define WF_PSE_TOP_QUEUE_EMPTY_SFD_PARK_QUEUE_EMPTY_MASK \
0x00200000 /* SFD_PARK_QUEUE_EMPTY[21] */
#define WF_PSE_TOP_QUEUE_EMPTY_SFD_PARK_QUEUE_EMPTY_SHFT 21
#define WF_PSE_TOP_QUEUE_EMPTY_SEC_RX_QUEUE_EMPTY_MASK  \
0x00100000 /* SEC_RX_QUEUE_EMPTY[20] */
#define WF_PSE_TOP_QUEUE_EMPTY_SEC_RX_QUEUE_EMPTY_SHFT 20
#define WF_PSE_TOP_QUEUE_EMPTY_SEC_TX_QUEUE_EMPTY_MASK  \
0x00080000 /* SEC_TX_QUEUE_EMPTY[19] */
#define WF_PSE_TOP_QUEUE_EMPTY_SEC_TX_QUEUE_EMPTY_SHFT 19
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_RX_QUEUE_EMPTY_MASK  \
0x00040000 /* MDP_RX_QUEUE_EMPTY[18] */
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_RX_QUEUE_EMPTY_SHFT 18
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_TX_QUEUE_EMPTY_MASK  \
0x00020000 /* MDP_TX_QUEUE_EMPTY[17] */
#define WF_PSE_TOP_QUEUE_EMPTY_MDP_TX_QUEUE_EMPTY_SHFT 17
#define WF_PSE_TOP_QUEUE_EMPTY_LMAC_TX_QUEUE_EMPTY_MASK \
0x00010000 /* LMAC_TX_QUEUE_EMPTY[16] */
#define WF_PSE_TOP_QUEUE_EMPTY_LMAC_TX_QUEUE_EMPTY_SHFT 16
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_7_EMPTY_MASK 0x00008000 /* HIF_7_EMPTY[15] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_7_EMPTY_SHFT 15
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_6_EMPTY_MASK 0x00004000 /* HIF_6_EMPTY[14] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_6_EMPTY_SHFT 14
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_5_EMPTY_MASK 0x00002000 /* HIF_5_EMPTY[13] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_5_EMPTY_SHFT 13
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_4_EMPTY_MASK 0x00001000 /* HIF_4_EMPTY[12] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_4_EMPTY_SHFT 12
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_3_EMPTY_MASK 0x00000800 /* HIF_3_EMPTY[11] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_3_EMPTY_SHFT 11
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_2_EMPTY_MASK 0x00000400 /* HIF_2_EMPTY[10] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_2_EMPTY_SHFT 10
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_1_EMPTY_MASK 0x00000200 /* HIF_1_EMPTY[9] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_1_EMPTY_SHFT 9
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_0_EMPTY_MASK 0x00000100 /* HIF_0_EMPTY[8] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_0_EMPTY_SHFT 8
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_11_EMPTY_MASK \
	0x00000080 /* HIF_11_EMPTY[7] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_11_EMPTY_SHFT 7
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_10_EMPTY_MASK \
	0x00000040 /* HIF_10_EMPTY[6] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_10_EMPTY_SHFT 6
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_9_EMPTY_MASK 0x00000020 /* HIF_9_EMPTY[5] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_9_EMPTY_SHFT 5
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_8_EMPTY_MASK 0x00000010 /* HIF_8_EMPTY[4] */
#define WF_PSE_TOP_QUEUE_EMPTY_HIF_8_EMPTY_SHFT 4
#define WF_PSE_TOP_QUEUE_EMPTY_CPU_Q3_EMPTY_MASK \
	0x00000008 /* CPU_Q3_EMPTY[3] */
#define WF_PSE_TOP_QUEUE_EMPTY_CPU_Q3_EMPTY_SHFT 3
#define WF_PSE_TOP_QUEUE_EMPTY_CPU_Q2_EMPTY_MASK \
	0x00000004 /* CPU_Q2_EMPTY[2] */
#define WF_PSE_TOP_QUEUE_EMPTY_CPU_Q2_EMPTY_SHFT 2
#define WF_PSE_TOP_QUEUE_EMPTY_CPU_Q1_EMPTY_MASK \
	0x00000002 /* CPU_Q1_EMPTY[1] */
#define WF_PSE_TOP_QUEUE_EMPTY_CPU_Q1_EMPTY_SHFT 1
#define WF_PSE_TOP_QUEUE_EMPTY_CPU_Q0_EMPTY_MASK \
	0x00000001 /* CPU_Q0_EMPTY[0] */
#define WF_PSE_TOP_QUEUE_EMPTY_CPU_Q0_EMPTY_SHFT 0

#define WF_PSE_TOP_PG_HIF0_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x110) /* 8110 */
#define WF_PSE_TOP_PG_HIF0_GROUP_HIF0_MAX_QUOTA_MASK \
0x0FFF0000 /* HIF0_MAX_QUOTA[27..16] */
#define WF_PSE_TOP_PG_HIF0_GROUP_HIF0_MAX_QUOTA_SHFT 16
#define WF_PSE_TOP_PG_HIF0_GROUP_HIF0_MIN_QUOTA_MASK \
0x00000FFF /* HIF0_MIN_QUOTA[11..0] */
#define WF_PSE_TOP_PG_HIF0_GROUP_HIF0_MIN_QUOTA_SHFT 0

#define WF_PSE_TOP_PG_HIF1_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x114) /* 8114 */

#define WF_PSE_TOP_PG_CPU_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x118) /* 8118 */

#define WF_PSE_TOP_PG_PLE_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x11C) /* 811C */

#define WF_PSE_TOP_PG_PLE1_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x120) /* 8120 */

#define WF_PSE_TOP_PG_LMAC0_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x124) /* 8124 */

#define WF_PSE_TOP_PG_LMAC1_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x128) /* 8128 */

#define WF_PSE_TOP_PG_LMAC2_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x12C) /* 812C */

#define WF_PSE_TOP_PG_LMAC3_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x130) /* 8130 */

#define WF_PSE_TOP_PG_MDP_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x134) /* 8134 */

#define WF_PSE_TOP_PG_MDP1_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x138) /* 8138 */

#define WF_PSE_TOP_PG_MDP2_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x13C) /* 813C */

#define WF_PSE_TOP_PG_HIF2_GROUP_ADDR \
	(WF_PSE_TOP_BASE + 0x140) /* 8140 */

#define WF_PSE_TOP_HIF0_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x150) /* 8150 */
#define WF_PSE_TOP_HIF0_PG_INFO_HIF0_SRC_CNT_MASK \
0x0FFF0000 /* HIF0_SRC_CNT[27..16] */
#define WF_PSE_TOP_HIF0_PG_INFO_HIF0_SRC_CNT_SHFT 16
#define WF_PSE_TOP_HIF0_PG_INFO_HIF0_RSV_CNT_MASK \
0x00000FFF /* HIF0_RSV_CNT[11..0] */
#define WF_PSE_TOP_HIF0_PG_INFO_HIF0_RSV_CNT_SHFT 0

#define WF_PSE_TOP_HIF1_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x154) /* 8154 */

#define WF_PSE_TOP_CPU_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x158) /* 8158 */

#define WF_PSE_TOP_PLE_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x15C) /* 815C */

#define WF_PSE_TOP_PLE1_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x160) /* 8160 */

#define WF_PSE_TOP_LMAC0_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x164) /* 8164 */

#define WF_PSE_TOP_LMAC1_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x168) /* 8168 */

#define WF_PSE_TOP_LMAC2_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x16C) /* 816C */

#define WF_PSE_TOP_LMAC3_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x170) /* 8170 */

#define WF_PSE_TOP_MDP_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x174) /* 8174 */

#define WF_PSE_TOP_MDP1_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x178) /* 8178 */

#define WF_PSE_TOP_MDP2_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x17C) /* 817C */

#define WF_PSE_TOP_HIF2_PG_INFO_ADDR \
	(WF_PSE_TOP_BASE + 0x180) /* 8180 */

#define WF_PSE_TOP_FL_QUE_CTRL_0_ADDR \
	(WF_PSE_TOP_BASE + 0x1B0) /* 81B0 */
#define WF_PSE_TOP_FL_QUE_CTRL_0_EXECUTE_MASK 0x80000000 /* EXECUTE[31] */
#define WF_PSE_TOP_FL_QUE_CTRL_0_Q_BUF_QID_SHFT 24
#define WF_PSE_TOP_FL_QUE_CTRL_0_Q_BUF_PID_SHFT 10

#define WF_PSE_TOP_FL_QUE_CTRL_2_ADDR \
	(WF_PSE_TOP_BASE + 0x1B8) /* 81B8 */
#define WF_PSE_TOP_FL_QUE_CTRL_2_QUEUE_TAIL_FID_MASK \
0x0FFF0000 /* QUEUE_TAIL_FID[27..16] */
#define WF_PSE_TOP_FL_QUE_CTRL_2_QUEUE_TAIL_FID_SHFT 16
#define WF_PSE_TOP_FL_QUE_CTRL_2_QUEUE_HEAD_FID_MASK \
0x00000FFF /* QUEUE_HEAD_FID[11..0] */
#define WF_PSE_TOP_FL_QUE_CTRL_2_QUEUE_HEAD_FID_SHFT 0

#define WF_PSE_TOP_FL_QUE_CTRL_3_ADDR \
	(WF_PSE_TOP_BASE + 0x1BC) /* 81BC */
#define WF_PSE_TOP_FL_QUE_CTRL_3_QUEUE_PKT_NUM_MASK \
0x00000FFF /* QUEUE_PKT_NUM[11..0] */
#define WF_PSE_TOP_FL_QUE_CTRL_3_QUEUE_PKT_NUM_SHFT 0

#define WF_PSE_TOP_FREEPG_CNT_ADDR \
	(WF_PSE_TOP_BASE + 0x380) /* 8380 */
#define WF_PSE_TOP_FREEPG_CNT_FFA_CNT_MASK 0x0FFF0000 /* FFA_CNT[27..16] */
#define WF_PSE_TOP_FREEPG_CNT_FFA_CNT_SHFT 16
#define WF_PSE_TOP_FREEPG_CNT_FREEPG_CNT_MASK 0x00000FFF /* FREEPG_CNT[11..0] */
#define WF_PSE_TOP_FREEPG_CNT_FREEPG_CNT_SHFT 0

#define WF_PSE_TOP_FREEPG_HEAD_TAIL_ADDR \
	(WF_PSE_TOP_BASE + 0x384) /* 8384 */
#define WF_PSE_TOP_FREEPG_HEAD_TAIL_FREEPG_TAIL_MASK \
0x0FFF0000 /* FREEPG_TAIL[27..16] */
#define WF_PSE_TOP_FREEPG_HEAD_TAIL_FREEPG_TAIL_SHFT 16
#define WF_PSE_TOP_FREEPG_HEAD_TAIL_FREEPG_HEAD_MASK \
0x00000FFF /* FREEPG_HEAD[11..0] */
#define WF_PSE_TOP_FREEPG_HEAD_TAIL_FREEPG_HEAD_SHFT 0

#ifdef __cplusplus
}
#endif

#endif /* __WF_PSE_TOP_REGS_H__ */

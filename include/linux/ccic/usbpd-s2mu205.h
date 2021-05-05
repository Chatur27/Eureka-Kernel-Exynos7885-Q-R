/*
 * Copyright (C) 2018 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/ccic/pdic_notifier.h>
#include <linux/ccic/usbpd_msg.h>
#if defined(CONFIG_TYPEC)
#include <linux/usb/typec.h>
#endif
#include <linux/power_supply.h>

#ifndef __USBPD_S2MU205_H__
#define __USBPD_S2MU205_H__

#define USBPD_DEV_NAME    "usbpd-s2mu205"

/* message buffer */
#define S2MU205_MAX_NUM_MSG_OBJ (7)
/* INTERRUPT STATUS NUM */
#define S2MU205_MAX_NUM_INT_STATUS (7)

#define S2MU205_REG_TYPEC_DIS (1 << 2)

#define TA_WATER_CHK_DURATION_MS	5000

/* define timer */
#define S2MU205_ROLE_SWAP_TIME_MS		(1350)
#define S2MU205_HARD_RESET_DELAY_MS		(300)
#define S2MU205_WAIT_RD_DETACH_DELAY_MS		(200)
#define S2MU205_WAIT_ATTACH_DELAY_MS		(30)
#define DUAL_ROLE_SET_MODE_WAIT_MS		(2000)

#define S2MU205_WATER_CHK_INTERVAL_TIME		(300)
#define S2MU205_ATTACH_STATE_CHECK_TIME		(1000)

#define S2MU205_WATER_THRESHOLD_MV			(600)
#define S2MU205_WATER_DRY_THRESHOLD_MV		(800)

#define S2MU205_WATER_THRESHOLD_POST_MV		(300)

#define WATER_CHK_RETRY_CNT		2
#define IS_CC_WATER(cc1, cc2)		((cc1 != USBPD_Rp) && (cc2 != USBPD_Rp))
#define IS_CC_POST_WATER(cc1, cc2)	((cc1 != USBPD_Ra) && (cc2 != USBPD_Ra))
#define IS_CC_DRY(cc1, cc2)		((cc1 == USBPD_Rp) && (cc2 == USBPD_Rp))
#define IS_CC_POST_DRY(cc1, cc2)	((cc1 == USBPD_Ra) && (cc2 == USBPD_Ra))

/*****************************************/
/***********DEFINITION REGISTER***********/
/*****************************************/
#define S2MU205_RESET_REG_00        (0x00)

/* reg 0x00 VBUS WAKEUP CONTROL */
#define S2MU205_REG_BMC_RX_TF_TRIM_SHIFT (0)
#define S2MU205_REG_VBUS_WAKEUP_DIS_SHIFT (1)
#define S2MU205_REG_COMP_HYS_TRIM_SHIFT (2)

#define S2MU205_REG_BMC_RX_TF_TRIM  (0x1 << S2MU205_REG_BMC_RX_TF_TRIM) /* 0x01 */
#define S2MU205_REG_VBUS_WAKEUP_DIS  (0x1 << S2MU205_REG_VBUS_WAKEUP_DIS_SHIFT) /* 0x02 */
#define S2MU205_REG_COMP_HYS_TRIM  (0x3 << S2MU205_REG_VBUS_WAKEUP_DIS_SHIFT) /* 0x04 */


/* reg 0x01 LPM MODE ENABLE */
#define S2MU205_REG_LP_LDO_D_SHIFT (0)
#define S2MU205_REG_LPM_EN_SHIFT (1)
#define S2MU205_REG_LP_LDO_D \
        (1 << S2MU205_REG_LP_LDO_D_SHIFT) /* 0x01 */
#define S2MU205_REG_LPM_EN \
        (0x1 << S2MU205_REG_LPM_EN_SHIFT) /* 0x02 */

/* reg 0x02 */
#define S2MU205_REG_CC_OCP_SHIFT (4)
#define S2MU205_REG_CC_OCP_MASK  (0xf << S2MU205_REG_CC_OCP_SHIFT)

#define S2MU205_REG_IFG_SHIFT (4)
#define S2MU205_REG_IFG_MASK (0xf << S2MU205_REG_IFG_SHIFT) /* 0xf0 */

/* reg 0x04 */
#define S2MU205_REG_OTP_CC_PUB_SHIFT    (7)
#define S2MU205_REG_CC_PU_LPM_CTRL_DIS_SHIFT    (2)
#define S2MU205_REG_CC2_RS_SW_ON_SHIFT    (1)
#define S2MU205_REG_CC1_RS_SW_ON_SHIFT    (0)

#define S2MU205_REG_OTP_CC_PUB_MASK \
        (0x1 << S2MU205_REG_OTP_CC_PUB_SHIFT)
#define S2MU205_REG_CC_PU_LPM_CTRL_DIS_MASK \
        (0x1 << S2MU205_REG_CC_PU_LPM_CTRL_DIS_SHIFT)
#define S2MU205_REG_CC2_RS_SW_ON_MASK    (0x1 << S2MU205_REG_CC2_RS_SW_ON_SHIFT)
#define S2MU205_REG_CC1_RS_SW_ON_MASK    (0x1 << S2MU205_REG_CC1_RS_SW_ON_SHIFT)

/* reg 0x08 */
#define S2MU205_REG_LPMPUI_SEL_SHIFT        (2)
#define S2MU205_REG_LPMPUI_SEL_MASK            (0x3 << S2MU205_REG_LPMPUI_SEL_SHIFT)
#define S2MU205_REG_LPMPUI_SEL_250NA_MASK    (0x0 << S2MU205_REG_LPMPUI_SEL_SHIFT)
#define S2MU205_REG_LPMPUI_SEL_500NA_MASK    (0x1 << S2MU205_REG_LPMPUI_SEL_SHIFT)
#define S2MU205_REG_LPMPUI_SEL_1UA_MASK        (0x2 << S2MU205_REG_LPMPUI_SEL_SHIFT)
#define S2MU205_REG_LPMPUI_SEL_2UA_MASK        (0x3 << S2MU205_REG_LPMPUI_SEL_SHIFT)

/* reg 0x0A */
#define S2MU205_REG_OVP_ON_SHIFT        (7)
#define S2MU205_REG_OVP_ON            (0x1 << S2MU205_REG_OVP_ON_SHIFT)

/* reg 0x18 */
#define S2MU205_REG_PLUG_CTRL_MODE_SHIFT    (0)
#define S2MU205_REG_PLUG_CTRL_RP_SEL_SHIFT    (4)
#define S2MU205_REG_PLUG_CTRL_DETECT_BAT_DISABLE_SHIFT    (6)
#define S2MU205_REG_PLUG_CTRL_DETECT_OCP_DISABLE_SHIFT    (7)
#define S2MU205_REG_PLUG_CTRL_DFP \
        (0x1 << S2MU205_REG_PLUG_CTRL_MODE_SHIFT) /* 0x01 */
#define S2MU205_REG_PLUG_CTRL_UFP \
        (0x2 << S2MU205_REG_PLUG_CTRL_MODE_SHIFT) /* 0x02 */
#define S2MU205_REG_PLUG_CTRL_DRP \
        (0x3 << S2MU205_REG_PLUG_CTRL_MODE_SHIFT) /* 0x03 */
#define S2MU205_REG_PLUG_CTRL_RP0 \
        (0x0 << S2MU205_REG_PLUG_CTRL_RP_SEL_SHIFT) /* 0x00 */
#define S2MU205_REG_PLUG_CTRL_RP80 \
        (0x1 << S2MU205_REG_PLUG_CTRL_RP_SEL_SHIFT) /* 0x10 */
#define S2MU205_REG_PLUG_CTRL_RP180 \
        (0x2 << S2MU205_REG_PLUG_CTRL_RP_SEL_SHIFT) /* 0x20 */
#define S2MU205_REG_PLUG_CTRL_RP330 \
		(0x3 << S2MU205_REG_PLUG_CTRL_RP_SEL_SHIFT) /* 0x30 */
#define S2MU205_REG_PLUG_CTRL_MODE_MASK    \
        (0x3 << S2MU205_REG_PLUG_CTRL_MODE_SHIFT) /* 0x03 */
#define S2MU205_REG_PLUG_CTRL_RP_SEL_MASK \
        (0x3 << S2MU205_REG_PLUG_CTRL_RP_SEL_SHIFT)/* 0x30 */
#define S2MU205_REG_PLUG_CTRL_DETECT_BAT_DISABLE_MASK \
        (0x1 << S2MU205_REG_PLUG_CTRL_DETECT_BAT_DISABLE_SHIFT)/* 0x40 */
#define S2MU205_REG_PLUG_CTRL_DETECT_OCP_DISABLE_MASK \
        (0x1 << S2MU205_REG_PLUG_CTRL_DETECT_OCP_DISABLE_SHIFT)/* 0x80 */

/* reg 0x19 */
#define S2MU205_REG_MSG_DATA_ROLE_SHIFT        (5)
#define S2MU205_REG_MSG_POWER_ROLE_SHIFT    (6)
#define S2MU205_REG_MSG_DATA_ROLE_UFP \
        (0x0 << S2MU205_REG_MSG_DATA_ROLE_SHIFT) /* 0x00 */
#define S2MU205_REG_MSG_DATA_ROLE_DFP \
        (0x1 << S2MU205_REG_MSG_DATA_ROLE_SHIFT) /* 0x20 */
#define S2MU205_REG_MSG_DATA_ROLE_MASK \
        (0x1 << S2MU205_REG_MSG_DATA_ROLE_SHIFT) /* 0x20 */
#define S2MU205_REG_MSG_POWER_ROLE_SINK \
        (0x0 << S2MU205_REG_MSG_POWER_ROLE_SHIFT) /* 0x00 */
#define S2MU205_REG_MSG_POWER_ROLE_SOURCE \
        (0x1 << S2MU205_REG_MSG_POWER_ROLE_SHIFT) /* 0x40 */
#define S2MU205_REG_MSG_POWER_ROLE_MASK \
        (0x1 << S2MU205_REG_MSG_POWER_ROLE_SHIFT) /* 0x40 */

#define S2MU205_REG_RD_OR_VBUS_MUX_SEL_SHIFT    (4)
#define S2MU205_REG_RD_OR_VBUS_MUX_SEL    \
        (0x1 << S2MU205_REG_RD_OR_VBUS_MUX_SEL_SHIFT)

/* reg 0x22 */
#define S2MU205_REG_PLUG_CTRL_SET_MON_RD     (0x1 << 3)

/* reg 0x26 */
#define S2MU205_REG_PLUG_CTRL_CC_HOLD_BIT     (0x1)
#define S2MU205_REG_PLUG_CTRL_SUPPORT_ACC     (0x1 << 7)

/* reg 0x27 */
#define S2MU205_REG_PLUG_CTRL_FSM_MANUAL_EN_SHIFT    (2)
#define S2MU205_REG_PLUG_CTRL_RpRd_PLUG_SEL_SHIFT    (3)
#define S2MU205_REG_PLUG_CTRL_VCONN_MANUAL_EN_SHIFT    (4)
#define S2MU205_REG_PLUG_CTRL_RpRd_CC1_VCONN_SHIFT    (5)
#define S2MU205_REG_PLUG_CTRL_RpRd_CC2_VCONN_SHIFT    (6)
#define S2MU205_REG_PLUG_CTRL_RpRd_MANUAL_EN_SHIFT    (7)

#define S2MU205_REG_PLUG_CTRL_FSM_MANUAL_EN \
        (0x1 << S2MU205_REG_PLUG_CTRL_FSM_MANUAL_EN_SHIFT) /* 0x04 */
#define S2MU205_REG_PLUG_CTRL_RpRd_MANUAL_MASK \
        (0x1 << S2MU205_REG_PLUG_CTRL_RpRd_PLUG_SEL_SHIFT | \
            0x1 << S2MU205_REG_PLUG_CTRL_RpRd_MANUAL_EN_SHIFT) /* 0x88 */
#define S2MU205_REG_PLUG_CTRL_RpRd_Rp_Source_Mode \
        (0x1 << S2MU205_REG_PLUG_CTRL_RpRd_PLUG_SEL_SHIFT | \
            0x1 << S2MU205_REG_PLUG_CTRL_RpRd_MANUAL_EN_SHIFT) /* 0x88 */
#define S2MU205_REG_PLUG_CTRL_RpRd_Rd_Sink_Mode \
        (0x1 << S2MU205_REG_PLUG_CTRL_RpRd_MANUAL_EN_SHIFT) /* 0x80 */
#define S2MU205_REG_PLUG_CTRL_RpRd_MANUAL_EN_MASK \
        (0x1 << S2MU205_REG_PLUG_CTRL_RpRd_MANUAL_EN_SHIFT) /* 0x80 */
#define S2MU205_REG_PLUG_CTRL_VCONN_MANUAL_EN \
        (0x1 << S2MU205_REG_PLUG_CTRL_VCONN_MANUAL_EN_SHIFT) /* 0x10 */
#define S2MU205_REG_PLUG_CTRL_RpRd_CC1_VCONN \
        (0x1 << S2MU205_REG_PLUG_CTRL_RpRd_CC1_VCONN_SHIFT) /* 0x20 */
#define S2MU205_REG_PLUG_CTRL_RpRd_CC2_VCONN \
        (0x1 << S2MU205_REG_PLUG_CTRL_RpRd_CC2_VCONN_SHIFT) /* 0x40 */
#define S2MU205_REG_PLUG_CTRL_RpRd_VCONN_MASK \
        (0x1 << S2MU205_REG_PLUG_CTRL_VCONN_MANUAL_EN_SHIFT | \
        0x1 << S2MU205_REG_PLUG_CTRL_RpRd_CC1_VCONN_SHIFT | \
        0x1 << S2MU205_REG_PLUG_CTRL_RpRd_CC2_VCONN_SHIFT) /* 0x70 */

/* reg 0x28 */
#define S2MU205_REG_PLUG_CTRL_CC_MANUAL_EN_SHIFT    (4)
#define S2MU205_REG_PLUG_CTRL_CC1_MANUAL_EN_SHIFT    (5)
#define S2MU205_REG_PLUG_CTRL_CC2_MANUAL_EN_SHIFT    (6)

#define S2MU205_REG_PLUG_CTRL_FSM_MANUAL_INPUT_MASK    (0xf)
#define S2MU205_REG_PLUG_CTRL_FSM_ATTACHED_SNK        (2)
#define S2MU205_REG_PLUG_CTRL_FSM_ATTACHED_SRC        (6)
#define S2MU205_REG_PLUG_CTRL_CC_MANUAL_EN \
        (0x1 << S2MU205_REG_PLUG_CTRL_CC_MANUAL_EN_SHIFT) /* 0x10 */
#define S2MU205_REG_PLUG_CTRL_CC1_MANUAL_ON \
        (0x1 << S2MU205_REG_PLUG_CTRL_CC_MANUAL_EN_SHIFT | \
        0x1 << S2MU205_REG_PLUG_CTRL_CC1_MANUAL_EN_SHIFT) /* 0x30 */
#define S2MU205_REG_PLUG_CTRL_CC2_MANUAL_ON \
        (0x1 << S2MU205_REG_PLUG_CTRL_CC_MANUAL_EN_SHIFT | \
        0x1 << S2MU205_REG_PLUG_CTRL_CC2_MANUAL_EN_SHIFT) /* 0x50 */
#define S2MU205_REG_PLUG_CTRL_CC_MANUAL_MASK \
        (0x1 << S2MU205_REG_PLUG_CTRL_CC_MANUAL_EN_SHIFT | \
        0x1 << S2MU205_REG_PLUG_CTRL_CC1_MANUAL_EN_SHIFT | \
        0x1 << S2MU205_REG_PLUG_CTRL_CC2_MANUAL_EN_SHIFT) /* 0x70 */

/* reg 0x2E */
#define S2MU205_REG_PLUG_CTRL_SSM_DISABLE_SHIFT				(0)
#define S2MU205_REG_PLUG_CTRL_VDM_DISABLE_SHIFT				(1)
#define S2MU205_REG_PLUG_CTRL_OP_Mode_FORCE_EN_SHIFT		(2)
#define S2MU205_REG_PLUG_CTRL_VBUS_IND_FORCE_MODE_SHIFT		(3)
#define S2MU205_REG_PLUG_CTRL_VBUS_IND_FORCE_EN_SHIFT		(4)
#define S2MU205_REG_PLUG_CTRL_REG_UFP_ATTACH_OPT_EN_SHIFT	(5)
#define S2MU205_REG_PLUG_CTRL_ECO_SRC_CAP_RDY_SHIFT			(6)
#define S2MU205_REG_PLUG_CTRL_PD_RST_OPTION_SHIFT			(7)

#define S2MU205_REG_PLUG_CTRL_SSM_DISABLE \
		(0x1 << S2MU205_REG_PLUG_CTRL_SSM_DISABLE_SHIFT) /* 0x01 */
#define S2MU205_REG_PLUG_CTRL_VDM_DISABLE \
		(0x1 << S2MU205_REG_PLUG_CTRL_VDM_DISABLE_SHIFT) /* 0x02 */
#define S2MU205_REG_PLUG_CTRL_OP_Mode_FORCE_EN \
		(0x1 << S2MU205_REG_PLUG_CTRL_OP_Mode_FORCE_EN_SHIFT)	/* 0x04 */
#define S2MU205_REG_PLUG_CTRL_VBUS_IND_FORCE_MODE \
		(0x1 << S2MU205_REG_PLUG_CTRL_VBUS_IND_FORCE_MODE_SHIFT) /* 0x08 */

#define S2MU205_REG_PLUG_CTRL_VBUS_IND_FORCE_EN \
		(0x1 << S2MU205_REG_PLUG_CTRL_VBUS_IND_FORCE_EN_SHIFT)	/* 0x10 */
#define S2MU205_REG_PLUG_CTRL_REG_UFP_ATTACH_OPT_EN \
		(0x1 << S2MU205_REG_PLUG_CTRL_REG_UFP_ATTACH_OPT_EN_SHIFT)	/* 0x20 */
#define S2MU205_REG_PLUG_CTRL_ECO_SRC_CAP_RDY \
		(0x1 << S2MU205_REG_PLUG_CTRL_ECO_SRC_CAP_RDY_SHIFT)	/* 0x40 */
#define S2MU205_REG_PLUG_CTRL_PD_RST_OPTION \
		(0x1 << S2MU205_REG_PLUG_CTRL_PD_RST_OPTION_SHIFT)	/* 0x80 */

/* reg 0x90 (For S2MU205_REG_MSG_SEND_CON) */
#define S2MU205_REG_MSG_SEND_CON_SEND_MSG_EN_SHIFT    (0)
#define S2MU205_REG_MSG_SEND_CON_OP_MODE_SHIFT        (1)
#define S2MU205_REG_MSG_SEND_CON_SOP_SHIFT            (2)
#define S2MU205_REG_MSG_SEND_CON_HARD_EN_SHIFT        (6)

#define S2MU205_REG_MSG_SEND_CON_SEND_MSG_EN \
        (0x1 << S2MU205_REG_MSG_SEND_CON_SEND_MSG_EN_SHIFT) /* 0x01 */
#define S2MU205_REG_MSG_SEND_CON_OP_MODE \
        (0x1 << S2MU205_REG_MSG_SEND_CON_OP_MODE_SHIFT) /* 0x02 */
#define S2MU205_REG_MSG_SEND_CON_SOP \
        (0x0 << S2MU205_REG_MSG_SEND_CON_SOP_SHIFT) /* 0x00 */
#define S2MU205_REG_MSG_SEND_CON_SOP_Prime \
        (0x1 << S2MU205_REG_MSG_SEND_CON_SOP_SHIFT) /* 0x04 */
#define S2MU205_REG_MSG_SEND_CON_SOP_DPrime \
        (0x2 << S2MU205_REG_MSG_SEND_CON_SOP_SHIFT) /* 0x08 */
#define S2MU205_REG_MSG_SEND_CON_SOP_PDebug \
        (0x3 << S2MU205_REG_MSG_SEND_CON_SOP_SHIFT) /* 0x0C */
#define S2MU205_REG_MSG_SEND_CON_SOP_DPDebug \
        (0x4 << S2MU205_REG_MSG_SEND_CON_SOP_SHIFT) /* 0x10 */
#define S2MU205_REG_MSG_SEND_CON_SOP_HardRST \
        (0x5 << S2MU205_REG_MSG_SEND_CON_SOP_SHIFT) /* 0x14 */
#define S2MU205_REG_MSG_SEND_CON_SOP_CableRST \
        (0x6 << S2MU205_REG_MSG_SEND_CON_SOP_SHIFT) /* 0x18 */
#define S2MU205_REG_MSG_SEND_CON_HARD_EN \
        (0x1 << S2MU205_REG_MSG_SEND_CON_HARD_EN_SHIFT) /* 0x40 */

/* reg 0xB2 */
#define S2MU205_PDIC_RID_SHIFT        (5)
#define S2MU205_PDIC_RID_MASK        (0x7 << S2MU205_PDIC_RID_SHIFT) /* 0xE0 */

/* reg 0xB3 */
#define S2MU205_REG_CTRL_MON_CC1_SHIFT        (0)
#define S2MU205_REG_CTRL_MON_CC2_SHIFT        (3)
#define S2MU205_REG_CTRL_MON_CC1_MASK \
        (0x7 << S2MU205_REG_CTRL_MON_CC1_SHIFT) /* 0x07 */
#define S2MU205_REG_CTRL_MON_CC2_MASK \
        (0x7 << S2MU205_REG_CTRL_MON_CC2_SHIFT) /* 0x38 */

/* reg 0xB4 */
#define S2MU205_PDIC_PLUG_ATTACH_DONE_SHIFT    (1)
#define S2MU205_PDIC_SINK_SEL_MONITOR_SHIFT    (2)
#define S2MU205_PDIC_SOURCE_SEL_MONITOR_SHIFT    (3)

#define S2MU205_PDIC_SINK (1 << S2MU205_PDIC_SINK_SEL_MONITOR_SHIFT \
        | 1 << S2MU205_PDIC_PLUG_ATTACH_DONE_SHIFT) /* 0x06 */
#define S2MU205_PDIC_SOURCE (1 << S2MU205_PDIC_SOURCE_SEL_MONITOR_SHIFT \
        | 1 << S2MU205_PDIC_PLUG_ATTACH_DONE_SHIFT) /* 0x0A */
#define S2MU205_PDIC_ATTACH_MASK (1 << S2MU205_PDIC_PLUG_ATTACH_DONE_SHIFT) /* 0x02 */
#define S2MU205_PR_MASK (S2MU205_PDIC_SINK | S2MU205_PDIC_SOURCE) /* 0x0E */

/* reg 0xF7 */
#define S2MU205_REG_ETC_SOFT_RESET_EN_SHIFT    (1)
#define S2MU205_REG_ETC_SOFT_RESET_EN \
        (0x1 << S2MU205_REG_ETC_SOFT_RESET_EN_SHIFT) /* 0x02 */
#define S2MU205_REG_ETC_SOFT_RESET_DIS \
        (0x0 << S2MU205_REG_ETC_SOFT_RESET_EN_SHIFT) /* 0x00 */

/* reg 0xF8 */
#define S2MU205_REG_ID_MONITOR_MSG_ID_MASK    (0x07)


/*****************************************/
/***********DEFINITION INTERRUPT**********/
/*****************************************/
#define S2MU205_REG_INT_STATUS0_MSG_ACCEPT    (1<<0)
#define S2MU205_REG_INT_STATUS0_MSG_GOODCRC   (1<<1)
#define S2MU205_REG_INT_STATUS0_VDM_ATTENTION (1<<2)
#define S2MU205_REG_INT_STATUS0_VDM_EXIT      (1<<3)
#define S2MU205_REG_INT_STATUS0_VDM_ENTER     (1<<4)
#define S2MU205_REG_INT_STATUS0_VDM_DISCOVER_MODE (1<<5)
#define S2MU205_REG_INT_STATUS0_VDM_DISCOVER_SVID (1<<6)
#define S2MU205_REG_INT_STATUS0_VDM_DISCOVER_ID   (1<<7)

/* reg 0xE1 */
#define S2MU205_REG_INT_STATUS1_MSG_PING      (1<<7)
#define S2MU205_REG_INT_STATUS1_MSG_GOTOMIN   (1<<6)
#define S2MU205_REG_INT_STATUS1_MSG_REJECT    (1<<5)
#define S2MU205_REG_INT_STATUS1_MSG_PSRDY     (1<<4)
#define S2MU205_REG_INT_STATUS1_MSG_GETSRCCAP (1<<3)
#define S2MU205_REG_INT_STATUS1_MSG_GETSNKCAP (1<<2)
#define S2MU205_REG_INT_STATUS1_MSG_DR_SWAP   (1<<1)
#define S2MU205_REG_INT_STATUS1_MSG_PR_SWAP   (1<<0)

/* reg 0xE2 */
#define S2MU205_REG_INT_STATUS2_MSG_VCONN_SWAP (1<<7)
#define S2MU205_REG_INT_STATUS2_MSG_WAIT       (1<<6)
#define S2MU205_REG_INT_STATUS2_MSG_SRC_CAP    (1<<5)
#define S2MU205_REG_INT_STATUS2_MSG_SNK_CAP    (1<<4)
#define S2MU205_REG_INT_STATUS2_MSG_REQUEST    (1<<3)
#define S2MU205_REG_INT_STATUS2_MSG_SOFTRESET  (1<<2)
#define S2MU205_REG_INT_STATUS2_WAKEUP         (1<<0)

/* reg 0xE3 */
#define S2MU205_REG_INT_STATUS3_UNS_CMD_DATA   (1<<5)

/* reg 0xE4 */
#define S2MU205_REG_INT_STATUS4_CC12_DET_IRQ  (1<<6)
#define S2MU205_REG_INT_STATUS4_PLUG_IRQ      (1<<5)
#define S2MU205_REG_INT_STATUS4_USB_DETACH    (1<<4)
#define S2MU205_REG_INT_STATUS4_MSG_PASS      (1<<3)
#define S2MU205_REG_INT_STATUS4_MSG_SENT      (1<<2)
#define S2MU205_REG_INT_STATUS4_MSG_ERROR     (1<<1)

/* reg 0xE5 */
#define S2MU205_REG_INT_STATUS5_HARD_RESET     (1<<2)

/* interrupt for checking message */
#define ENABLED_INT_0    (S2MU205_REG_INT_STATUS0_MSG_GOODCRC |\
						S2MU205_REG_INT_STATUS0_MSG_ACCEPT)
#define ENABLED_INT_1    (S2MU205_REG_INT_STATUS1_MSG_PING |\
						S2MU205_REG_INT_STATUS1_MSG_REJECT |\
						S2MU205_REG_INT_STATUS1_MSG_PSRDY |\
						S2MU205_REG_INT_STATUS1_MSG_DR_SWAP |\
						S2MU205_REG_INT_STATUS1_MSG_PR_SWAP)
#define ENABLED_INT_2    (S2MU205_REG_INT_STATUS2_MSG_VCONN_SWAP |\
						S2MU205_REG_INT_STATUS2_MSG_WAIT |\
						S2MU205_REG_INT_STATUS2_MSG_REQUEST |\
						S2MU205_REG_INT_STATUS2_MSG_SOFTRESET)
#define ENABLED_INT_2_WAKEUP    (S2MU205_REG_INT_STATUS2_MSG_VCONN_SWAP |\
						S2MU205_REG_INT_STATUS2_MSG_WAIT |\
						S2MU205_REG_INT_STATUS2_MSG_SOFTRESET |\
						S2MU205_REG_INT_STATUS2_WAKEUP)
#define ENABLED_INT_3    S2MU205_REG_INT_STATUS3_UNS_CMD_DATA
#define ENABLED_INT_4    (S2MU205_REG_INT_STATUS4_USB_DETACH |\
				S2MU205_REG_INT_STATUS4_PLUG_IRQ |\
				S2MU205_REG_INT_STATUS4_MSG_PASS |\
				S2MU205_REG_INT_STATUS4_MSG_ERROR)
#define ENABLED_INT_5    (S2MU205_REG_INT_STATUS5_HARD_RESET)

/* S2MU205 I2C registers */
enum s2mu205_usbpd_reg {
    S2MU205_REG_PD_TRIM            = 0x00,
    S2MU205_REG_PD_CTRL            = 0x01,
    S2MU205_REG_PD_CTRL_2          = 0x02,
    S2MU205_REG_ANALOG_OTP_04      = 0x04,
    S2MU205_REG_ANALOG_OTP_08      = 0x08,
    S2MU205_REG_ANALOG_OTP_0A      = 0x0A,
    S2MU205_REG_PHY_CTRL_00        = 0x10,
    S2MU205_REG_PHY_CTRL_IFG       = 0x13,
    S2MU205_REG_BMC_CTRL	       = 0x14,
    S2MU205_REG_PLUG_CTRL_PORT     = 0x18,
    S2MU205_REG_PLUG_CTRL_MSG      = 0x19,
    S2MU205_REG_PLUG_CTRL_VBUS_MUX = 0x1B,
    S2MU205_REG_PLUG_CTRL_SET_RD_2 = 0x1C,
    S2MU205_REG_PLUG_CTRL_SET_RP_2 = 0x1D,
    S2MU205_REG_PLUG_CTRL_SET_RD   = 0x1E,
    S2MU205_REG_PLUG_CTRL_SET_RP   = 0x1F,
    S2MU205_REG_PLUG_CTRL_CC_TIMER1 = 0x20,
    S2MU205_REG_PLUG_CTRL_CC_TIMER2 = 0x21,
    S2MU205_REG_PLUG_CTRL_SET_MON  = 0x22,
    S2MU205_REG_PLUG_CTRL_CC_HOLD  = 0x26,
    S2MU205_REG_PLUG_CTRL_RpRd     = 0x27,
    S2MU205_REG_PLUG_CTRL_CC12     = 0x28,
    S2MU205_REG_PLUG_CTRL          = 0x2E,
    S2MU205_REG_CTRL               = 0x2F,

    S2MU205_REG_INT_MASK0          = 0x3E,
    S2MU205_REG_INT_MASK1          = 0x3F,
    S2MU205_REG_INT_MASK2          = 0x40,
    S2MU205_REG_INT_MASK3          = 0x41,
    S2MU205_REG_INT_MASK4          = 0x42,
    S2MU205_REG_INT_STATUS0        = 0xE0,
    S2MU205_REG_INT_STATUS1        = 0xE1,
    S2MU205_REG_INT_STATUS2        = 0xE2,
    S2MU205_REG_INT_STATUS3        = 0xE3,
    S2MU205_REG_INT_STATUS4        = 0xE4,
    S2MU205_REG_ADC_STATUS         = 0xB2,
    S2MU205_REG_PLUG_MON1          = 0xB3,
    S2MU205_REG_PLUG_MON2          = 0xB4,
    S2MU205_REG_PLUG_FSM_MON       = 0xB7,

    S2MU205_REG_MSG_SEND_CON       = 0x90,
    S2MU205_REG_MSG_TX_HEADER_L    = 0x91,
    S2MU205_REG_MSG_TX_HEADER_H    = 0x92,
    S2MU205_REG_MSG_TX_OBJECT0_0_L = 0x93,
    S2MU205_REG_MSG_TX_OBJECT0_0_H = 0x94,
    S2MU205_REG_MSG_TX_OBJECT0_1_L = 0x95,
    S2MU205_REG_MSG_TX_OBJECT0_1_H = 0x96,
    S2MU205_REG_MSG_TX_OBJECT1_0_L = 0x97,
    S2MU205_REG_MSG_TX_OBJECT1_0_H = 0x98,
    S2MU205_REG_MSG_TX_OBJECT1_1_L = 0x99,
    S2MU205_REG_MSG_TX_OBJECT1_1_H = 0x9A,
    S2MU205_REG_MSG_TX_OBJECT2_0_L = 0x9B,
    S2MU205_REG_MSG_TX_OBJECT2_0_H = 0x9C,
    S2MU205_REG_MSG_TX_OBJECT2_1_L = 0x9D,
    S2MU205_REG_MSG_TX_OBJECT2_1_H = 0x9E,
    S2MU205_REG_MSG_TX_OBJECT3_0_L = 0x9F,
    S2MU205_REG_MSG_TX_OBJECT3_0_H = 0xA0,
    S2MU205_REG_MSG_TX_OBJECT3_1_L = 0xA1,
    S2MU205_REG_MSG_TX_OBJECT3_1_H = 0xA2,
    S2MU205_REG_MSG_TX_OBJECT4_0_L = 0xA3,
    S2MU205_REG_MSG_TX_OBJECT4_0_H = 0xA4,
    S2MU205_REG_MSG_TX_OBJECT4_1_L = 0xA5,
    S2MU205_REG_MSG_TX_OBJECT4_1_H = 0xA6,
    S2MU205_REG_MSG_TX_OBJECT5_0_L = 0xA7,
    S2MU205_REG_MSG_TX_OBJECT5_0_H = 0xA8,
    S2MU205_REG_MSG_TX_OBJECT5_1_L = 0xA9,
    S2MU205_REG_MSG_TX_OBJECT5_1_H = 0xAA,
    S2MU205_REG_MSG_TX_OBJECT6_0_L = 0xAB,
    S2MU205_REG_MSG_TX_OBJECT6_0_H = 0xAC,
    S2MU205_REG_MSG_TX_OBJECT6_1_L = 0xAD,
    S2MU205_REG_MSG_TX_OBJECT6_1_H = 0xAE,

    S2MU205_REG_MSG_RX_HEADER_L    = 0xC1,
    S2MU205_REG_MSG_RX_HEADER_H    = 0xC2,
    S2MU205_REG_MSG_RX_OBJECT0_0_L = 0xC3,
    S2MU205_REG_MSG_RX_OBJECT0_0_H = 0xC4,
    S2MU205_REG_MSG_RX_OBJECT0_1_L = 0xC5,
    S2MU205_REG_MSG_RX_OBJECT0_1_H = 0xC6,
    S2MU205_REG_MSG_RX_OBJECT1_0_L = 0xC7,
    S2MU205_REG_MSG_RX_OBJECT1_0_H = 0xC8,
    S2MU205_REG_MSG_RX_OBJECT1_1_L = 0xC9,
    S2MU205_REG_MSG_RX_OBJECT1_1_H = 0xCA,
    S2MU205_REG_MSG_RX_OBJECT2_0_L = 0xCB,
    S2MU205_REG_MSG_RX_OBJECT2_0_H = 0xCC,
    S2MU205_REG_MSG_RX_OBJECT2_1_L = 0xCD,
    S2MU205_REG_MSG_RX_OBJECT2_1_H = 0xCE,
    S2MU205_REG_MSG_RX_OBJECT3_0_L = 0xCF,
    S2MU205_REG_MSG_RX_OBJECT3_0_H = 0xD0,
    S2MU205_REG_MSG_RX_OBJECT3_1_L = 0xD1,
    S2MU205_REG_MSG_RX_OBJECT3_1_H = 0xD2,
    S2MU205_REG_MSG_RX_OBJECT4_0_L = 0xD3,
    S2MU205_REG_MSG_RX_OBJECT4_0_H = 0xD4,
    S2MU205_REG_MSG_RX_OBJECT4_1_L = 0xD5,
    S2MU205_REG_MSG_RX_OBJECT4_1_H = 0xD6,
    S2MU205_REG_MSG_RX_OBJECT5_0_L = 0xD7,
    S2MU205_REG_MSG_RX_OBJECT5_0_H = 0xD8,
    S2MU205_REG_MSG_RX_OBJECT5_1_L = 0xD9,
    S2MU205_REG_MSG_RX_OBJECT5_1_H = 0xDA,
    S2MU205_REG_MSG_RX_OBJECT6_0_L = 0xDB,
    S2MU205_REG_MSG_RX_OBJECT6_0_H = 0xDC,
    S2MU205_REG_MSG_RX_OBJECT6_1_L = 0xDD,
    S2MU205_REG_MSG_RX_OBJECT6_1_H = 0xDE,

    S2MU205_REG_ETC                = 0xF7,
    S2MU205_REG_ID_MONITOR         = 0xF8,
    S2MU205_REG_ID_MONITOR2        = 0xF9
};

typedef enum {
    S2MU205_THRESHOLD_128MV = 2,
    S2MU205_THRESHOLD_171MV = 3,
    S2MU205_THRESHOLD_214MV = 4,
    S2MU205_THRESHOLD_257MV = 5,
    S2MU205_THRESHOLD_300MV = 6,
    S2MU205_THRESHOLD_342MV = 7,
    S2MU205_THRESHOLD_385MV = 8,
    S2MU205_THRESHOLD_428MV = 9,
    S2MU205_THRESHOLD_450MV = 10,
    S2MU205_THRESHOLD_471MV = 11,
    S2MU205_THRESHOLD_492MV = 12,
    S2MU205_THRESHOLD_514MV = 13,
    S2MU205_THRESHOLD_535MV = 14,
    S2MU205_THRESHOLD_557MV = 15,
    S2MU205_THRESHOLD_578MV = 16,
    S2MU205_THRESHOLD_600MV = 17,
    S2MU205_THRESHOLD_621MV = 18,
    S2MU205_THRESHOLD_642MV = 19,
    S2MU205_THRESHOLD_685MV = 20,
    S2MU205_THRESHOLD_1000MV = 27,

    S2MU205_THRESHOLD_1200MV = 32,
    S2MU205_THRESHOLD_1242MV = 33,
    S2MU205_THRESHOLD_1285MV = 34,
    S2MU205_THRESHOLD_1328MV = 35,
    S2MU205_THRESHOLD_1371MV = 36,
    S2MU205_THRESHOLD_1414MV = 37,
    S2MU205_THRESHOLD_1457MV = 38,
    S2MU205_THRESHOLD_1500MV = 39,
    S2MU205_THRESHOLD_1542MV = 40,
    S2MU205_THRESHOLD_1587MV = 41,
    S2MU205_THRESHOLD_1628MV = 42,
    S2MU205_THRESHOLD_1671MV = 43,
    S2MU205_THRESHOLD_1714MV = 44,
    S2MU205_THRESHOLD_1757MV = 45,
    S2MU205_THRESHOLD_1799MV = 46,
    S2MU205_THRESHOLD_1842MV = 47,
    S2MU205_THRESHOLD_1885MV = 48,
    S2MU205_THRESHOLD_1928MV = 49,
    S2MU205_THRESHOLD_1971MV = 50,
    S2MU205_THRESHOLD_2014MV = 51,
    S2MU205_THRESHOLD_2057MV = 52,
    S2MU205_THRESHOLD_2099MV = 53,
    S2MU205_THRESHOLD_2142MV = 54,
    S2MU205_THRESHOLD_2185MV = 55,
    S2MU205_THRESHOLD_2228MV = 56,
    S2MU205_THRESHOLD_2271MV = 57,

    S2MU205_THRESHOLD_MAX     = 63
} CCIC_THRESHOLD_SEL;

typedef enum {
    S2MU205_CC_OCP_255MV = 0,
    S2MU205_CC_OCP_262MV = 1,
    S2MU205_CC_OCP_273MV = 2,
    S2MU205_CC_OCP_282MV = 3,
    S2MU205_CC_OCP_301MV = 4,
    S2MU205_CC_OCP_311MV = 5,
    S2MU205_CC_OCP_327MV = 6,
    S2MU205_CC_OCP_339MV = 7,
    S2MU205_CC_OCP_375MV = 8,
    S2MU205_CC_OCP_390MV = 9,
    S2MU205_CC_OCP_415MV = 10,
    S2MU205_CC_OCP_433MV = 11,
    S2MU205_CC_OCP_478MV = 12,
    S2MU205_CC_OCP_502MV = 13,
    S2MU205_CC_OCP_542MV = 14,
    S2MU205_CC_OCP_575MV = 15,

    S2MU205_CC_OCP_MAX   = 16
} CCIC_CC_OCP_SEL;

typedef enum {
    S2MU205_PHY_IFG_25US = 0,
    S2MU205_PHY_IFG_30US = 1,
    S2MU205_PHY_IFG_35US = 2,
} CCIC_PHY_IFG_SEL;

enum s2mu205_power_role {
    PDIC_SINK,
    PDIC_SOURCE
};

enum s2mu205_pdic_rid {
    REG_RID_UNDF = 0x00,
    REG_RID_255K = 0x03,
    REG_RID_301K = 0x04,
    REG_RID_523K = 0x05,
    REG_RID_619K = 0x06,
    REG_RID_OPEN = 0x07,
    REG_RID_MAX  = 0x08,
};

typedef enum {
    CLIENT_OFF = 0,
    CLIENT_ON = 1,
} CCIC_DEVICE_REASON;

typedef enum {
    HOST_OFF = 0,
    HOST_ON = 1,
} CCIC_HOST_REASON;

typedef enum {
    VBUS_OFF = 0,
    VBUS_ON = 1,
} CCIC_VBUS_SEL;

typedef enum {
    DET_HARD_RESET = 0,
    DET_DETACH = 1,
} CCIC_DETACH_TYPE;

typedef enum {
    PLUG_CTRL_RD = 0,
    PLUG_CTRL_RP = 1,
} CCIC_RP_RD_SEL;

typedef enum {
    PD_LPM_MODE = 0,
    PD_NORMAL_MODE = 1,
} CCIC_LPM_MODE_SEL;

typedef enum {
    VBUS_WAKEUP_ENABLE = 0,
    VBUS_WAKEUP_DISABLE = 1,
} CCIC_VBUS_WAKEUP_SEL;

#if defined(CONFIG_CCIC_NOTIFIER)
struct ccic_state_work {
    struct work_struct ccic_work;
    int dest;
    int id;
    int attach;
    int event;
};
#endif

struct s2mu205_usbpd_data {
    struct device *dev;
    struct i2c_client *i2c;
#if defined(CONFIG_CCIC_NOTIFIER)
    struct workqueue_struct *ccic_wq;
#endif
    struct mutex _mutex;
    struct mutex poll_mutex;
    struct mutex lpm_mutex;
    struct mutex cc_mutex;
	struct mutex vbus_mutex;
	struct mutex water_mutex;
	const char *name;
    int vconn_en;
    int regulator_en;
    int irq_gpio;
    int irq;
    int vbus_discharging;
    int discharging_en;
    int power_role;
    int data_role;
    int vconn_source;
    msg_header_type header;
    data_obj_type obj[S2MU205_MAX_NUM_MSG_OBJ];
    u64 status_reg;
    bool lpm_mode;
    bool detach_valid;
    bool is_factory_mode;
    bool is_water_detect;
    bool is_muic_water_detect;
    bool is_otg_vboost;
    bool is_otg_reboost;
    bool is_pr_swap;
    bool is_muic_attached;
    bool vbus_short_check;
    bool pd_vbus_short_check;
    bool vbus_short;
    bool lpcharge_water;
    int vbus_short_check_cnt;
    int water_detect_cnt;
    int rid;
    int is_host;
    int is_client;
    int is_attached;
	int is_killer;
	u8 rp_threshold;
	u8 rd_threshold;
#if defined(CONFIG_DUAL_ROLE_USB_INTF)
    struct dual_role_phy_instance *dual_role;
    struct dual_role_phy_desc *desc;
    struct completion reverse_completion;
    int try_state_change;
    struct delayed_work role_swap_work;
    int data_role_dual; /* data_role for dual role swap */
    int power_role_dual; /* power_role for dual role swap */
#elif defined(CONFIG_TYPEC)
    struct typec_port *port;
    struct typec_partner *partner;
    struct usb_pd_identity partner_identity;
    struct typec_capability typec_cap;
    struct completion role_reverse_completion;
    int typec_power_role;
    int typec_data_role;
    int typec_try_state_change;
    struct delayed_work typec_role_swap_work;
#endif

	int rp_lvl;

    struct notifier_block type3_nb;
    struct workqueue_struct *pdic_queue;
    struct delayed_work plug_work;
    struct s2m_pdic_notifier_struct pdic_notifier;
    struct delayed_work water_detect_handler;
    struct delayed_work ta_water_detect_handler;
    struct delayed_work water_dry_handler;
    int pm_cc1;
    int pm_cc2;
    int pm_chgin;
    struct power_supply_desc ccic_desc;
    struct power_supply *psy_pm;
    struct power_supply *psy_ccic;
    struct power_supply *psy_chg;
    struct power_supply *psy_muic;
	int cc1_val;
	int cc2_val;
    struct regulator *regulator;
};

extern int s2mu205_usbpd_get_adc(void);
extern void s2mu205_usbpd_set_muic_type(int type);
#if defined(CONFIG_CCIC_NOTIFIER)
extern void s2mu205_control_option_command(struct s2mu205_usbpd_data *usbpd_data, int cmd);
#endif
#if defined(CONFIG_SEC_FACTORY)
extern int s2mu205_power_off_water_check(struct s2mu205_usbpd_data *pdic_data);
#endif
extern void s2mu205_rprd_mode_change(struct s2mu205_usbpd_data *usbpd_data, u8 mode);
extern void vbus_turn_on_ctrl(struct s2mu205_usbpd_data *usbpd_data, bool enable);
extern int s2mu205_set_lpm_mode(struct s2mu205_usbpd_data *pdic_data);
extern int s2mu205_set_normal_mode(struct s2mu205_usbpd_data *pdic_data);
#endif /* __USBPD_S2MU205_H__ */

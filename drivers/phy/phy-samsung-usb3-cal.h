#ifndef __PHY_SAMSUNG_USB3_FW_CAL_H__
#define __PHY_SAMSUNG_USB3_FW_CAL_H__

#define EXYNOS_USBCON_LINKSYSTEM	(0x04)
#define LINKSYSTEM_HOST_SYSTEM_ERR		(0x1 << 31)
#define LINKSYSTEM_PHY_POWER_DOWN		(0x1 << 30)
#define LINKSYSTEM_PHY_SW_RESET			(0x1 << 29)
#define LINKSYSTEM_LINK_SW_RESET		(0x1 << 28)
#define LINKSYSTEM_XHCI_VERSION_CONTROL		(0x1 << 27)
#define LINKSYSTEM_FLADJ_MASK			(0x3f << 1)
#define LINKSYSTEM_FLADJ(_x)			((_x) << 1)
#define LINKSYSTEM_FORCE_BVALID			(0x1 << 7)
#define LINKSYSTEM_FORCE_VBUSVALID		(0x1 << 8)

#define EXYNOS_USBCON_PHYUTMI		(0x08)
#define PHYUTMI_UTMI_SUSPEND_COM_N		(0x1 << 12)
#define PHYUTMI_UTMI_L1_SUSPEND_COM_N		(0x1 << 11)
#define PHYUTMI_VBUSVLDEXTSEL			(0x1 << 10)
#define PHYUTMI_VBUSVLDEXT			(0x1 << 9)
#define PHYUTMI_TXBITSTUFFENH			(0x1 << 8)
#define PHYUTMI_TXBITSTUFFEN			(0x1 << 7)
#define PHYUTMI_OTGDISABLE			(0x1 << 6)
#define PHYUTMI_IDPULLUP			(0x1 << 5)
#define PHYUTMI_DRVVBUS				(0x1 << 4)
#define PHYUTMI_DPPULLDOWN			(0x1 << 3)
#define PHYUTMI_DMPULLDOWN			(0x1 << 2)
#define PHYUTMI_FORCESUSPEND			(0x1 << 1)
#define PHYUTMI_FORCESLEEP			(0x1 << 0)

#define EXYNOS_USBCON_PHYCLKRST		(0x10)
#define PHYCLKRST_EN_UTMISUSPEND		(0x1 << 31)
#define PHYCLKRST_SSC_REFCLKSEL_MASK		(0xff << 23)
#define PHYCLKRST_SSC_REFCLKSEL(_x)		((_x) << 23)
#define PHYCLKRST_SSC_RANGE_MASK		(0x03 << 21)
#define PHYCLKRST_SSC_RANGE(_x)			((_x) << 21)
#define PHYCLKRST_SSC_EN			(0x1 << 20)
#define PHYCLKRST_REF_SSP_EN			(0x1 << 19)
#define PHYCLKRST_REF_CLKDIV2			(0x1 << 18)
#define PHYCLKRST_MPLL_MULTIPLIER_MASK		(0x7f << 11)
#define PHYCLKRST_MPLL_MULTIPLIER(_x)		((_x) << 11)
#define PHYCLKRST_FSV_OUT_EN(_x)		((_x) << 10) 	//for KATMAI
#define PHYCLKRST_FSEL_MASK			(0x3f << 5)
#define PHYCLKRST_FSEL(_x)			((_x) << 5)
#define PHYCLKRST_RETENABLEN			(0x1 << 4)
#define PHYCLKRST_REFCLKSEL_MASK		(0x03 << 2)
#define PHYCLKRST_REFCLKSEL(_x)			((_x) << 2)
#define PHYCLKRST_PORTRESET			(0x1 << 1)
#define PHYCLKRST_COMMONONN			(0x1 << 0)

#define EXYNOS_USBCON_PHYPIPE		(0x0c)
#define PHYPIPE_PHY_CLOCK_SEL		(0x1 << 4)

#define EXYNOS_USBCON_PHYREG0		(0x14)
#define PHYREG0_SSC_REFCLKSEL_MASK		(0x1ff << 23)
#define PHYREG0_SSC_REFCLKSEL(_x)		((_x) << 23)
#define PHYREG0_SSC_RANGE_MASK			(0x7 << 20)
#define PHYREG0_SSC_RAMGE(_x)			((_x) << 20)
#define PHYREG0_CR_WRITE			(1 << 19)
#define PHYREG0_CR_READ				(1 << 18)
#define PHYREG0_CR_DATA_MASK			(0xffff << 2)
#define PHYREG0_CR_DATA_IN(_x)			((_x) << 2)
#define PHYREG0_CR_CR_CAP_DATA			(1 << 1)
#define PHYREG0_CR_CR_CAP_ADDR			(1 << 0)

#define EXYNOS_USBCON_PHYREG1		(0x18)
#define PHYREG1_CR_DATA_OUT_MASK		(0xffff << 1)
#define PHYREG1_CR_DATA_OUT(_x)			((_x) << 1)
#define PHYREG1_CR_ACK				(1 << 0)

#define EXYNOS_USBCON_PHYPARAM0		(0x1c)
#define PHYPARAM0_REF_USE_PAD			(0x1 << 31)
#define PHYPARAM0_VDATAREFTUNE_MASK		(0x3 << 26)
#define PHYPARAM0_VDATAREFTUNE(_x)		((_x) << 26)
#define PHYPARAM0_REF_LOSLEVEL_MASK		(0x1f << 26)
#define PHYPARAM0_REF_LOSLEVEL_EXT(_x)		((_x & (0x1f << 26)) >> 26)
#define PHYPARAM0_REF_LOSLEVEL			(0x9 << 26)
#define PHYPARAM0_TXVREFTUNE_MASK		(0xf << 22)
#define PHYPARAM0_TXVREFTUNE_EXT(_x)		((_x & (0xf << 22)) >> 22)
#define PHYPARAM0_TXVREFTUNE(_x)		((_x) << 22)
#define PHYPARAM0_TXRISETUNE_MASK		(0x3 << 20)
#define PHYPARAM0_TXRISETUNE_EXT(_x)		((_x & (0x3 << 20)) >> 20)
#define PHYPARAM0_TXRISETUNE(_x)		((_x) << 20)
#define PHYPARAM0_TXRESTUNE_MASK		(0x3 << 18)
#define PHYPARAM0_TXRESTUNE_EXT(_x)		((_x & (0x3 << 18)) >> 18)
#define PHYPARAM0_TXRESTUNE(_x)			((_x) << 18)
#define PHYPARAM0_TXPREEMPPULSETUNE		(0x1 << 17)
#define PHYPARAM0_TXPREEMPPULSETUNE_EXT(_x)	((_x & (0x1 << 17)) >> 17)
#define PHYPARAM0_TXPREEMPAMPTUNE_MASK		(0x3 << 15)
#define PHYPARAM0_TXPREEMPAMPTUNE_EXT(_x)	((_x & (0x3 << 15)) >> 15)
#define PHYPARAM0_TXPREEMPAMPTUNE(_x)		((_x) << 15)
#define PHYPARAM0_TXHSXVTUNE_MASK		(0x3 << 13)
#define PHYPARAM0_TXHSXVTUNE_EXT(_x)		((_x & (0x3 << 13)) >> 13)
#define PHYPARAM0_TXHSXVTUNE(_x)		((_x) << 13)
#define PHYPARAM0_TXFSLSTUNE_MASK		(0xf << 9)
#define PHYPARAM0_TXFSLSTUNE_EXT(_x)		((_x & (0xf << 9)) >> 9)
#define PHYPARAM0_TXFSLSTUNE(_x)		((_x) << 9)
#define PHYPARAM0_SQRXTUNE_MASK			(0x7 << 6)
#define PHYPARAM0_SQRXTUNE_EXT(_x)		((_x & (0x7 << 6)) >> 6)
#define PHYPARAM0_SQRXTUNE(_x)			((_x) << 6)
#define PHYPARAM0_OTGTUNE_MASK			(0x7 << 3)
#define PHYPARAM0_OTGTUNE_EXT(_x)		((_x & (0x7 << 3)) >> 3)
#define PHYPARAM0_OTGTUNE(_x)			((_x) << 3)
#define PHYPARAM0_COMPDISTUNE_MASK		(0x7 << 0)
#define PHYPARAM0_COMPDISTUNE_EXT(_x)		(_x & (0x7 << 0))
#define PHYPARAM0_COMPDISTUNE(_x)		((_x) << 0)

#define EXYNOS_USBCON_PHYPARAM1	(0x20)
#define PHYPARAM1_PHY_RESET_SEL				(0x01 << 31)
#define PHYPARAM1_TX0_TERM_OFFSET_MASK		(0x1f << 26)
#define PHYPARAM1_TX0_TERM_OFFSET(_x)		((_x) << 26)
#define PHYPARAM1_PCS_TXSWING_FULL_MASK		(0x7f << 12)
#define PHYPARAM1_PCS_TXSWING_FULL(_x)		((_x) << 12)
#define PHYPARAM1_PCS_TXDEEMPH_6DB_MASK		(0x3f << 6)
#define PHYPARAM1_PCS_TXDEEMPH_6DB(_x)		((_x) << 6)
#define PHYPARAM1_PCS_TXDEEMPH_3P5DB_MASK	(0x3f << 0)
#define PHYPARAM1_PCS_TXDEEMPH_3P5DB(_x)	((_x) << 0)

#define EXYNOS_USBCON_PHYTEST		(0x28)
#define PHYTEST_POWERDOWN_SSP			(0x1 << 3)
#define PHYTEST_POWERDOWN_SSP_EXT(_x)	((_x & (0x1 << 3)) >> 3)
#define PHYTEST_POWERDOWN_HSP			(0x1 << 2)
#define PHYTEST_POWERDOWN_HSP_EXT(_x)	((_x & (0x1 << 2)) >> 2)

#define EXYNOS_USBCON_PHYPOWERDOWN		(0x28)
#define PHYPOWERDOWN_VATESTENB			(0x1 << 6)
#define PHYPOWERDOWN_TEST_BURNIN_MASK		(0x3 << 4)
#define PHYPOWERDOWN_TEST_BURNIN		((_x) << 4)

#define EXYNOS_USBCON_PHYBATCHG		(0x30)
#define PHYBATCHG_FSLS_SPEED_R		(0x1 << 14)			// FSLS_SPEED Read (Katmai)
#define PHYBATCHG_CHGDET			(0x1 << 10)
#define PHYBATCHG_RIDC				(0x1 << 9)
#define PHYBATCHG_RIDB				(0x1 << 8)
#define PHYBATCHG_RIDA				(0x1 << 7)
#define PHYBATCHG_RIDGND			(0x1 << 6)
#define PHYBATCHG_RIDFLOAT			(0x1 << 5)			//in case of read, RIDFLOAT
#define PHYBATCHG_FSLS_SPEED_W		(0x1 << 5)			//in case of write, FSLS_SPEED (Katmai)
#define PHYBATCHG_VDATSRCENB			(0x1 << 4)
#define PHYBATCHG_VDATDETENB			(0x1 << 3)
#define PHYBATCHG_CHRGSEL			(0x1 << 2)
#define PHYBATCHG_ACAENB			(0x1 << 1)
#define PHYBATCHG_DCDENB			(0x1 << 0)

#define EXYNOS_USBCON_PHYRESUME		(0x34)
#define PHYRESUME_DIS_BUSPEND_QACT		(0x1 << 14)
#define PHYRESUME_DIS_LINKGATE_QACT		(0x1 << 13)
#define PHYRESUME_DIS_ID0_QACT			(0x1 << 12)
#define PHYRESUME_DIS_VBUSVALID_QACT		(0x1 << 11)
#define PHYRESUME_DIS_BVALID_QACT		(0x1 << 10)
#define PHYRESUME_FORCE_QACT			(0x1 << 9)
#define PHYRESUME_FORCE_OPMODE_MASK		(0x3 << 7)
#define PHYRESUME_FORCE_OPMODE(_x)		((_x) << 7)
#define PHYRESUME_OPMODE_EN			(0x1 << 6)
#define PHYRESUME_AUTORESUME_EN			(0x1 << 5)
#define PHYRESUME_BYPASS_SEL			(0x1 << 4)
#define PHYRESUME_BYPASS_DM_EN			(0x1 << 3)
#define PHYRESUME_BYPASS_DP_EN			(0x1 << 2)
#define PHYRESUME_BYPASS_DM_DATA		(0x1 << 1)
#define PHYRESUME_BYPASS_DP_DATA		(0x1 << 0)

#define EXYNOS_USBCON_PHYPCSVAL		(0x3C)
#define PHYPCSVAL_PCS_RX_LOS_MASK_VAL_MASK	(0x3FF << 0)
#define PHYPCSVAL_PCS_RX_LOS_MASK_VAL(_x)	((_x & 0x3FF) << 0)

#define EXYNOS_USBCON_LINKPORT		(0x44)
#define LINKPORT_HOST_NUM_U3_PORT_MASK		(0xf << 13)
#define LINKPORT_HOST_NUM_U3_PORT(_x)		((_x) << 13)
#define LINKPORT_HOST_NUM_U2_PORT_MASK		(0xf << 9)
#define LINKPORT_HOST_NUM_U2_PORT(_x)	((_x) << 9)
#define LINKPORT_HOST_U3_PORT_DISABLE		(0x1 << 8)
#define LINKPORT_HOST_U2_PORT_DISABLE		(0x1 << 7)
#define LINKPORT_PORT_POWER_CONTROL		(0x1 << 6)
#define LINKPORT_HOST_PORT_OVCR_U3		(0x1 << 5)
#define LINKPORT_HOST_PORT_OVCR_U2		(0x1 << 4)
#define LINKPORT_HOST_PORT_OVCR_U3_SEL		(0x1 << 3)
#define LINKPORT_HOST_PORT_OVCR_U2_SEL		(0x1 << 2)
#define LINKPORT_PERM_ATTACH_U3			(0x << 1)
#define LINKPORT_PERM_ATTACH_U2			(0x1 << 0)

#define EXYNOS_USBCON_PHYPARAM2		(0x50)
#define PHYPARAM2_TX_VBOOST_LVL_MASK		(0x7 << 3)
#define PHYPARAM2_TX_VBOOST_LVL(_x)		((_x) << 3)
#define PHYPARAM2_LOS_BIAS_MASK			(0x7 << 0)
#define PHYPARAM2_LOS_BIAS(_x)			((_x) << 0)

#define EXYNOS_USBCON_HSPHYCTRL		(0x54)
#define HSPHYCTRL_PHYSWRSTALL			(0x1 << 31)
#define HSPHYCTRL_SIDDQ				(0x1 << 6)
#define HSPHYCTRL_SIDDQ_K1			(0x1 << 1)		//KATMAI_EVT0
#define HSPHYCTRL_PHYSWRST			(0x1 << 0)

#define EXYNOS_USBCON_HSPHYTESTIF	(0x58)
#define HSPHYTESTIF_LINESTATE_MASK		(0x3 << 20)
#define HSPHYTESTIF_DATAOUT_MASK		(0xf << 16)
#define HSPHYTESTIF_CLKEN			(0x1 << 13)
#define HSPHYTESTIF_DATAOUTSEL			(0x2 << 12)
#define HSPHYTESTIF_ADDR_MASK			(0xf << 8)
#define HSPHYTESTIF_ADDR(_x)			((_x) << 8)
#define HSPHYTESTIF_DATAIN_MASK			(0xff << 0)
#define HSPHYTESTIF_DATAIN(_x)			((_x) << 0)

//addr changed in Katmai
#define EXYNOS_USBCON_HSPHYPLLTUNE_K1	(0x5c)
#define HSPHYPLLTUNE_PLL_B_TUNE_K1			(0x1 << 6)
#define HSPHYPLLTUNE_PLL_I_TUNE_K1(_x)		((_x) << 4)
#define HSPHYPLLTUNE_PLL_I_TUNE_K1_MASK		(0x3 << 4)	
#define HSPHYPLLTUNE_PLL_P_TUNE_K1(_x)		((_x) << 0)
#define HSPHYPLLTUNE_PLL_P_TUNE_K1_MASK		(0xf << 0)

#define EXYNOS_USBCON_PHYSELECTION	(0x6c)
#define PHYSEL_U3RST				(0x1 << 5)
#define PHYSEL_UTMI_CLK				(0x1 << 4)
#define PHYSEL_PIPE_CLK				(0x1 << 3)
#define PHYSEL_UTMI				(0x1 << 2)
#define PHYSEL_PIPE				(0x1 << 1)
#define PHYSEL_SIDEBAND				(0x1 << 0)

#define EXYNOS_USBCON_HSPHYPLLTUNE	(0x70)
#define HSPHYPLLTUNE_PLL_B_TUNE			(0x1 << 6)
#define HSPHYPLLTUNE_PLL_I_TUNE(_x)		((_x) << 4)
#define HSPHYPLLTUNE_PLL_I_TUNE_MASK		(0x3 << 4)
#define HSPHYPLLTUNE_PLL_P_TUNE(_x)		((_x) << 0)
#define HSPHYPLLTUNE_PLL_P_TUNE_MASK		(0xf << 0)

#define EXYNOS_USBCON_EHCICTRL		(0x80)
#define EHCICTRL_EHCI64BITEN			(0x1 << 31)
#define EHCICTRL_EN_INCRX_ALIGN			(0x1 << 30)
#define EHCICTRL_EN_INCR4			(0x1 << 29)
#define EHCICTRL_EN_INCR8			(0x1 << 28)
#define EHCICTRL_EN_INCR16			(0x1 << 26)
#define EHCICTRL_EN_AUTO_PPDON_OVRCUR		(0x1 << 25)
#define EHCICTRL_FLADJVAL0_MASK			(0x3f << 19)
#define EHCICTRL_FLADJVAL0(_x)			((_x) << 19)
#define EHCICTRL_FLADJVAL1_MASK			(0x3f << 13)
#define EHCICTRL_FLADJVAL1(_x)			((_x) << 13)
#define EHCICTRL_FLADJVAL2_MASK			(0x3f << 7)
#define EHCICTRL_FLADJVAL2(_x)			((_x) << 7)
#define EHCICTRL_FLADJVALHOST_MASK		(0x3f << 1)
#define EHCICTRL_FLADJVALHOST(_x)		((_x) << 1)

#define EXYNOS_USBCON_OHCICTRL		(0x84)
#define OHCICTRL_RESET_PORT0			(0x1 << 29)
#define OHCICTRL_HUBSETUP_MIN			(0x1 << 4)
#define OHCICTRL_OHCISUSPLGCY			(0x1 << 3)
#define OHCICTRL_APPSTARTCLK			(0x1 << 2)
#define OHCICTRL_OHCICNTSELN			(0x1 << 1)
#define OHCICTRL_OHCICLKCKTRSTN			(0x1 << 0)

#define EXYNOS_USBCON_HOSTLINKCTRL	(0x88)
#define HOSTLINKCTRL_EN_SLEEP_HOST_P2		(0x1 << 31)
#define HOSTLINKCTRL_EN_SLEEP_HOST_P1		(0x1 << 30)
#define HOSTLINKCTRL_EN_SLEEP_HOST_P0		(0x1 << 29)
#define HOSTLINKCTRL_EN_SLEEP_OTG		(0x1 << 28)
#define HOSTLINKCTRL_EN_SUSPEND_HOST_P2		(0x1 << 27)
#define HOSTLINKCTRL_EN_SUSPEND_HOST_P1		(0x1 << 26)
#define HOSTLINKCTRL_EN_SUSPEND_HOST_P0		(0x1 << 25)
#define HOSTLINKCTRL_EN_SUSPEND_OTG		(0x1 << 24)
#define HOSTLINKCTRL_FORCE_HOST_OVRCUR_P2	(0x1 << 17)
#define HOSTLINKCTRL_FORCE_HOST_OVRCUR		(0x1 << 16)
#define HOSTLINKCTRL_SW_RESET_PORT2		(0x1 << 3)
#define HOSTLINKCTRL_SW_RESET_PORT1		(0x1 << 2)
#define HOSTLINKCTRL_SW_RESET_PORT0		(0x1 << 1)
#define HOSTLINKCTRL_LINKSWRST			(0x1 << 0)

#define EXYNOS_USBCON_OTGLINKCTRL	(0x8C)
#define OTGINKCTRL_AVALID			(0x1 << 14)
#define OTGINKCTRL_BVALID			(0x1 << 13)
#define OTGINKCTRL_IDDIG			(0x1 << 12)
#define OTGINKCTRL_VBUSDETECT			(0x1 << 11)
#define OTGINKCTRL_VBUSVLDSEL_MASK		(0x3 << 9)
#define OTGINKCTRL_VBUSVLDSEL(_x)		((_x) << 9)
#define OTGINKCTRL_LINK_PRST			(0x1 << 4)
#define OTGINKCTRL_SW_RESET_ALL			(0x1 << 3)
#define OTGINKCTRL_SW_RESET			(0x1 << 2)

void samsung_exynos_cal_usb3phy_enable(struct exynos_usbphy_info *usbphy_info);
void samsung_exynos_cal_usb3phy_late_enable(struct exynos_usbphy_info *usbphy_info);
void samsung_exynos_cal_usb3phy_disable(struct exynos_usbphy_info *usbphy_info);
void samsung_exynos_cal_usb3phy_tune_each(struct exynos_usbphy_info *usbphy_info,
	enum exynos_usbphy_tune_para para, int val);
void samsung_exynos_cal_usb3phy_hs_tune_extract(struct exynos_usbphy_info *usbphy_info);
void samsung_exynos_cal_usb3phy_tune_dev(struct exynos_usbphy_info *usbphy_info);
void samsung_exynos_cal_usb3phy_tune_host(struct exynos_usbphy_info *usbphy_info);
void samsung_exynos_cal_cr_write(struct exynos_usbphy_info *usbphy_info, u16 addr, u16 data);
u16 samsung_exynos_cal_cr_read(struct exynos_usbphy_info *usbphy_info, u16 addr);
void samsung_cal_usb3phy_tune(struct exynos_usbphy_info *usbphy_info);
void samsung_exynos_cal_usb3phy_config_host_mode(
		struct exynos_usbphy_info *usbphy_info);
void samsung_exynos_cal_usb3phy_enable_dp_pullup(
		struct exynos_usbphy_info *usbphy_info);
void samsung_exynos_cal_usb3phy_disable_dp_pullup(
		struct exynos_usbphy_info *usbphy_info);
void samsung_exynos_cal_usb3phy_dp_altmode_set_phy_enable(
		struct exynos_usbphy_info *usbphy_info, int dp_phy_port);
void samsung_exynos_cal_usb3phy_dp_altmode_clear_phy_enable(
		struct exynos_usbphy_info *usbphy_info, int dp_phy_port);
void samsung_exynos_cal_usb3phy_dp_altmode_set_ss_disable(
		struct exynos_usbphy_info *usbphy_info, int dp_phy_port);
void samsung_exynos_cal_usb3phy_dp_altmode_clear_ss_disable(
		struct exynos_usbphy_info *usbphy_info, int dp_phy_port);

#endif
